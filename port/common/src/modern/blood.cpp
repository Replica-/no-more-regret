/* Blood. See blood.h. */
#include "blood.h"
#include "gamestate.h"
#include "camera.h"
#include "crusys.h"
#include "dlitem.h"
#include "gamepal.h"
#include "hud.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "overlay.h"
#include "pointer.h"
#include "pfloat.h"
#include "systimer.h"
#include "type.h"
#include "viewport.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace {

/* A drop in flight (or the mist), in map units, per tick of 60 a second. */
struct Drop {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float ground;
    float size;
    u16 age;
    u16 life;
    u8 mist;
};
/* A splat: where on the floor, its size, and the floor it lies on. */
struct Splat {
    u16 x;
    u16 y;
    u8 z;
    u8 size;
    u16 floor;
};

const int MOST_DROPS = 600;
const int MOST_SPLATS = 500;
const float GRAVITY = 0.18F;

Drop drops[MOST_DROPS];
int dropCount = 0;
/* The splats as a ring, the oldest at first when it is full. */
Splat splats[MOST_SPLATS];
int splatCount = 0;
int splatFirst = 0;
/* The splats on each item, for the display list's quick test. */
u8 onItem[ITEMS];
/* Each colour reddened (the palette's nearest), 0 until worked out. */
u8 reddened[256];
u8 reddenedKnown[256];
u16 bloodMap = 0xffff;
u32 lastTime = 0;
std::uint32_t seed = 12345;

/* The drawing's own random numbers (the game's are kept for its
 * decisions). */
float random(float low, float high)
{
    seed = (seed * 1664525U) + 1013904223U;
    return low + ((high - low) * static_cast<float>((seed >> 8) & 0xffffU) / 65535.0F);
}

float cosineOf(i16 degrees)
{
    PFloat angle(static_cast<i16>(((degrees % 360) + 360) % 360), 0);
    return static_cast<float>(cosine(angle).value) / 65536;
}

float sineOf(i16 degrees)
{
    PFloat angle(static_cast<i16>(((degrees % 360) + 360) % 360), 0);
    return static_cast<float>(sine(angle).value) / 65536;
}

void forget()
{
    dropCount = 0;
    splatCount = 0;
    splatFirst = 0;
    std::fill(std::begin(onItem), std::end(onItem), 0);
    std::fill(std::begin(reddenedKnown), std::end(reddenedKnown), 0);
}

/* The floor at x,y whose top is z: an item of the land with the point in
 * its footprint; 0 for none. */
u16 floorAt(u16 x, u16 y, u8 z)
{
    const int column = x >> CHUNK_SHIFT;
    const int row = y >> CHUNK_SHIFT;
    u16 found = 0;
    /* An item's place is its box's far corner: one in the next chunk may
     * reach back over the point. */
    for (int c = column; c <= std::min(column + 1, CHUNKS - 1); c++) {
        for (int r = row; r <= std::min(row + 1, CHUNKS - 1); r++) {
            for (u16 item = mapChunks[c][r]; item != 0U; item = itemNext[item]) {
                if (item < FIRST_ITEM || (itemFlags[item] & (ITEM_CONTAINED | ITEM_ETHEREAL | ITEM_INVISIBLE)) != 0) {
                    continue;
                }
                const ShapeInfo &info = typeFlags.infos[itemShape[item]];
                if (info.land == 0U || std::abs(itemZ[item] + (info.z * 8) - z) > 2) {
                    continue;
                }
                i16 width;
                i16 depth;
                i16 height;
                info.box(width, depth, height);
                if ((itemFlags[item] & ITEM_FLIPPED) != 0) {
                    std::swap(width, depth);
                }
                if (x <= itemX[item] && x > itemX[item] - width && y <= itemY[item] && y > itemY[item] - depth) {
                    found = item;
                }
            }
        }
    }
    return found;
}

void addSplat(const Drop &drop)
{
    const auto x = static_cast<u16>(drop.x);
    const auto y = static_cast<u16>(drop.y);
    const auto z = static_cast<u8>(drop.ground);
    const u16 floor = floorAt(x, y, z);
    if (floor == 0U) {
        return;
    }
    if (splatCount == MOST_SPLATS) {
        /* The oldest goes. */
        Splat &old = splats[splatFirst];
        if (onItem[old.floor] != 0U) {
            onItem[old.floor]--;
        }
        redrawItem(old.floor);
        splatFirst = (splatFirst + 1) % MOST_SPLATS;
        splatCount--;
    }
    splats[(splatFirst + splatCount) % MOST_SPLATS] =
        Splat{x, y, z, static_cast<u8>(random(14, 32)), floor};
    splatCount++;
    if (onItem[floor] < 255) {
        onItem[floor]++;
    }
    redrawItem(floor);
}

/* A tick of the drops: they fly, fall and land; the mist slows and fades. */
void step()
{
    int kept = 0;
    for (int i = 0; i < dropCount; i++) {
        Drop drop = drops[i];
        drop.age++;
        drop.x += drop.vx;
        drop.y += drop.vy;
        drop.z += drop.vz;
        if (drop.mist != 0U) {
            drop.vx *= 0.9F;
            drop.vy *= 0.9F;
            drop.vz *= 0.9F;
        } else {
            drop.vz -= GRAVITY;
            if (drop.z <= drop.ground) {
                drop.z = drop.ground;
                addSplat(drop);
                continue;
            }
        }
        if (drop.age >= drop.life) {
            continue;
        }
        drops[kept++] = drop;
    }
    dropCount = kept;
}

u8 reddenedOf(u8 under)
{
    if (reddenedKnown[under] == 0U) {
        const RGB *colours = gamePalette->data.get();
        /* A fifth of the colour, four fifths of the blood's (6 bits a
         * component). */
        const int red = ((colours[under].red * 2) + (50 * 8)) / 10;
        const int green = ((colours[under].green * 2) + (3 * 8)) / 10;
        const int blue = ((colours[under].blue * 2) + (5 * 8)) / 10;
        int best = 0;
        int bestDistance = 1 << 30;
        for (int i = 0; i < 256; i++) {
            const int dr = colours[i].red - red;
            const int dg = colours[i].green - green;
            const int db = colours[i].blue - blue;
            const int distance = (dr * dr) + (dg * dg) + (db * db);
            if (distance < bestDistance) {
                best = i;
                bestDistance = distance;
            }
        }
        reddened[under] = static_cast<u8>(best);
        reddenedKnown[under] = 1;
    }
    return reddened[under];
}

}

void noteBlood(u16 victim, u16 hitter, u8 direction, i16 damage)
{
    /* Part of the port's controls (pointer.h's setting), which the tests'
     * original controls turn off. */
    if (!pointerSetting()) {
        return;
    }
    if (currentMap != bloodMap) {
        bloodMap = currentMap;
        forget();
    }
    const Item body(victim);
    const Item from(hitter);
    /* The way the shot went (the game's 16 directions, 0 up the map and on
     * clockwise), else away from the shooter through the victim, else any
     * way. */
    i16 angle = static_cast<i16>(random(0, 359));
    if (direction < 16) {
        angle = static_cast<i16>(270 + ((direction * 45) / 2));
    } else if (hitter != 0U && hitter != victim && from.isValid()) {
        const std::int32_t dx = body.centreX() - from.centreX();
        const std::int32_t dy = body.centreY() - from.centreY();
        if (dx != 0 || dy != 0) {
            angle = angleOf(dx, dy);
        }
    }
    const int amount = std::clamp(8 + (damage / 3), 8, 40);
    for (int i = 0; i < amount && dropCount < MOST_DROPS; i++) {
        Drop &drop = drops[dropCount++];
        /* A cone about the shot's way. */
        const auto way = static_cast<i16>(angle + random(-28, 28));
        const float speed = random(1.5F, 5.0F);
        drop.x = body.centreX() + (cosineOf(angle) * 6);
        drop.y = body.centreY() + (sineOf(angle) * 6);
        drop.z = body.z() + random(22, 40);
        drop.vx = cosineOf(way) * speed;
        drop.vy = sineOf(way) * speed;
        drop.vz = random(0.3F, 3.2F);
        drop.ground = body.z();
        drop.size = random(1.2F, 2.4F);
        drop.age = 0;
        drop.life = 240;
        drop.mist = 0;
        /* A third of it a fine mist that fades quickly. */
        if (i % 3 == 0) {
            drop.mist = 1;
            drop.life = static_cast<u16>(random(15, 30));
            drop.size = random(3, 6);
        }
    }
}

void drawBlood()
{
    if (currentMap != bloodMap) {
        bloodMap = currentMap;
        forget();
    }
    const u32 now = systemTimer->time();
    if (worldActive == 0U) {
        lastTime = now;
        return;
    }
    const u32 ticks = std::min<u32>(now - lastTime, 60);
    lastTime = now;
    for (u32 i = 0; i < ticks; i++) {
        step();
    }
    for (int i = 0; i < dropCount; i++) {
        const Drop &drop = drops[i];
        float x;
        float y;
        mapToOverlay(static_cast<u16>(drop.x), static_cast<u16>(drop.y), static_cast<i16>(drop.z), &x, &y);
        if (drop.mist != 0U) {
            const float fade = 1 - (static_cast<float>(drop.age) / drop.life);
            overlayDisc(x, y, drop.size, 0x9a101000U | static_cast<std::uint32_t>(140 * fade));
            continue;
        }
        /* A short streak behind it. */
        float x0;
        float y0;
        mapToOverlay(static_cast<u16>(drop.x - (drop.vx * 2)), static_cast<u16>(drop.y - (drop.vy * 2)),
                     static_cast<i16>(drop.z - (drop.vz * 2)), &x0, &y0);
        overlayLine(x0, y0, x, y, drop.size, 0x8a0a0af2U);
    }
}

bool bloodOn(u16 item)
{
    return item < ITEMS && onItem[item] != 0U;
}

void paintBlood(u16 item, Viewport &view)
{
    const Rect clip = view.bounds();
    for (int n = 0; n < splatCount; n++) {
        const Splat &splat = splats[(splatFirst + n) % MOST_SPLATS];
        if (splat.floor != item) {
            continue;
        }
        /* DL_ItemNode::draw's place of a point, scrolled. */
        const i16 hx = static_cast<i16>(splat.x >> 1);
        const i16 hy = static_cast<i16>(splat.y >> 1);
        const i16 sx = static_cast<i16>(hx - hy - scrollX);
        const i16 sy = static_cast<i16>((hx >> 1) + (hy >> 1) - splat.z - scrollY);
        /* An ellipse twice as wide as high, its size in tenths of a pixel. */
        const int rx = (splat.size * 17) / 100;
        const int ry = (splat.size * 9) / 100;
        for (int dy = -ry; dy <= ry; dy++) {
            for (int dx = -rx; dx <= rx; dx++) {
                if ((dx * dx * ry * ry) + (dy * dy * rx * rx) > rx * rx * ry * ry) {
                    continue;
                }
                const int x = sx + dx;
                const int y = sy + dy;
                if (x < clip.left || x > clip.right || y < clip.top || y > clip.bottom) {
                    continue;
                }
                u8 *pixel = view.address(static_cast<s16>(x), static_cast<s16>(y));
                *pixel = reddenedOf(*pixel);
            }
        }
    }
}
