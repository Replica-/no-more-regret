/* Smooth turns. See turns.h. */
#include "turns.h"
#include "gamestate.h"
#include "animcach.h"
#include "animfrm.h"
#include "control.h"
#include "enhancements.h"
#include "file.h"
#include "filespec.h"
#include "gamefile.h"
#include "hud.h"
#include "imported.h"
#include "item.h"
#include "npc.h"
#include "pointer.h"
#include "shaphand.h"
#include "utility.h"

#include <cstring>

namespace {

/* static\turns.dat (smooth_turns.py): "TURN", the shape, the first frame's
 * number and the count of animations; each animation's number and its 16
 * directions' 3 frames' numbers; the count of frames, each one's size and
 * its bytes as the shape file holds a frame. Read when first asked for,
 * into the game's memory, and this record in the arena. */
const int DIRECTIONS = 16;
const int BETWEEN = 3;
const int MOST_ANIMS = 4;
struct Turns {
    bool read;
    bool any;
    u16 shape;
    u16 first;
    u16 anims;
    u16 anim[MOST_ANIMS];
    u16 frames[MOST_ANIMS][DIRECTIONS * BETWEEN];
    u16 count;
    /* Each frame's start in bytes, and the end after the last. */
    u32 *starts;
    u8 *bytes;
    /* The frame the Silencer was drawn in last (turnsTick). */
    u16 shown;
};
Turns &turns = gameGlobal<Turns>();

void readTurns()
{
    turns.read = true;
    char *path = makePath(nullptr, staticDirectory, "turns.dat", nullptr);
    if (fileExists(path) == 0) {
        return;
    }
    File file;
    file.open(path, FILE_READ, 0);
    if (file.isOpen() == 0U) {
        return;
    }
    char magic[4];
    bool whole = file.read(magic, 4, 0) == 4 && std::memcmp(magic, "TURN", 4) == 0 &&
                 file.read(&turns.shape, 2, -1) == 2 && file.read(&turns.first, 2, -1) == 2 &&
                 file.read(&turns.anims, 2, -1) == 2 && turns.anims <= MOST_ANIMS;
    for (u16 a = 0; whole && a < turns.anims; a++) {
        whole = file.read(&turns.anim[a], 2, -1) == 2 &&
                file.read(turns.frames[a], sizeof(turns.frames[a]), -1) == static_cast<s32>(sizeof(turns.frames[a]));
    }
    whole = whole && file.read(&turns.count, 2, -1) == 2 && turns.count != 0;
    u32 *sizes = nullptr;
    if (whole) {
        sizes = new u32[turns.count];
        const auto bytes = static_cast<s32>(turns.count * sizeof(u32));
        whole = file.read(sizes, bytes, -1) == bytes;
    }
    if (whole) {
        /* Each frame's start, kept to 16 bytes as an allocation's (the
         * ShapeFrame after the FrameBlock as the cache's are aligned). */
        turns.starts = new u32[turns.count + 1];
        u32 total = 0;
        for (u16 i = 0; i < turns.count; i++) {
            turns.starts[i] = total;
            total += (sizes[i] + 15) & ~15U;
        }
        turns.starts[turns.count] = total;
        turns.bytes = new u8[total];
        for (u16 i = 0; whole && i < turns.count; i++) {
            whole = file.read(turns.bytes + turns.starts[i], static_cast<s32>(sizes[i]), -1) ==
                    static_cast<s32>(sizes[i]);
        }
    }
    delete[] sizes;
    file.close();
    if (!whole) {
        releaseTurns();
        turns.read = true;
        return;
    }
    turns.any = true;
}

bool loaded()
{
    if (!turns.read) {
        readTurns();
    }
    return turns.any;
}

/* The way from the NPC to the pointer in quarters of the 16 directions
 * (0-63, 0 up the map and on clockwise, as fineDirection's), -1 for none:
 * fineDirection's directions are not even (their edges at slopes of 1/4,
 * 3/4, 4/3 and 4), so the angle is laid between their middles. */
int pointerQuarter(Npc &npc)
{
    ItemPoint aim;
    if (!pointerAimPoint(&aim)) {
        return -1;
    }
    const std::int32_t dx = static_cast<std::int32_t>(aim.x) - npc.centreX();
    const std::int32_t dy = static_cast<std::int32_t>(aim.y) - npc.centreY();
    if (std::abs(dx) + std::abs(dy) < 32) {
        return -1;
    }
    /* 0 along x (direction 4), 90 along y (direction 8). */
    const i16 angle = angleOf(dx, dy);
    const int quadrant = angle / 90;
    const float within = static_cast<float>(angle % 90);
    /* The directions' middles in a quadrant, in degrees. */
    const float middles[5] = {0, 25.46F, 45, 64.54F, 90};
    float place = 4;
    for (int i = 0; i < 4; i++) {
        if (within <= middles[i + 1]) {
            place = static_cast<float>(i) + ((within - middles[i]) / (middles[i + 1] - middles[i]));
            break;
        }
    }
    const float direction = 4 + static_cast<float>(quadrant * 4) + place;
    return static_cast<int>((direction * 4) + 0.5F) & 63;
}

}

u16 turnFrame(u16 item, u16 frame)
{
    if (!upscaledSetting() || !hardwareSetting() || !pointerSetting() || !loaded()) {
        return frame;
    }
    Npc npc(item);
    if (!npc.isSilencer() || itemShape[item] != turns.shape) {
        return frame;
    }
    const u16 anim = npc.lastAnimSet();
    int a = 0;
    while (a < turns.anims && turns.anim[a] != anim) {
        a++;
    }
    if (a == turns.anims) {
        return frame;
    }
    /* Standing in it: his frame the stand's of his direction. */
    const int direction = npc.direction() & 0xf;
    AnimFrame standing;
    getAnimFrame(itemShape[item], anim, static_cast<u8>(direction), 0, &standing);
    if (animShapeFrame(standing) != frame) {
        return frame;
    }
    const int quarter = pointerQuarter(npc);
    if (quarter < 0) {
        return frame;
    }
    /* How far the pointer's way is from his, in quarters (-32 to 31). */
    const int off = ((quarter - (direction * 4) + 96) % 64) - 32;
    if (off == 0 || off > 2 || off < -2) {
        return frame;
    }
    if (off > 0) {
        return turns.frames[a][(direction * BETWEEN) + (off - 1)];
    }
    const int before = (direction + DIRECTIONS - 1) % DIRECTIONS;
    return turns.frames[a][(before * BETWEEN) + (4 + off - 1)];
}

ShapeFrame *turnsFrame(u16 shape, u16 frame)
{
    if (!turns.any || shape != turns.shape || frame < turns.first || frame >= turns.first + turns.count) {
        return nullptr;
    }
    /* A frame's bytes start with the room the cache keeps its FrameBlock
     * in (ShapeCacheHandler::loadFrame); the ShapeFrame is after it. */
    return reinterpret_cast<ShapeFrame *>(turns.bytes + turns.starts[frame - turns.first] + sizeof(FrameBlock));
}

void turnsTick()
{
    if (!turns.read && !loaded()) {
        return;
    }
    if (!turns.any) {
        return;
    }
    Item silencer(controlledActor);
    if (!silencer.isValid()) {
        return;
    }
    const u16 shown = turnFrame(silencer.id, silencer.frame());
    if (shown != turns.shown) {
        turns.shown = shown;
        silencer.touch();
    }
}

void releaseTurns()
{
    delete[] turns.starts;
    delete[] turns.bytes;
    turns = Turns{};
}
