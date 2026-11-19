/* Animations of No Regret's and their frames, in No Remorse. See imported.h. */
#include "imported.h"
#include "gamestate.h"
#include "actanim.h"
#include "astream.h"
#include "file.h"
#include "filespec.h"
#include "animfile.h"
#include "animfrm.h"
#include "animprim.h"
#include "gamefile.h"
#include "pointer.h"
#include "shaphand.h"
#include "utility.h"
#include "wpncache.h"

#include <algorithm>
#include <cstring>

namespace {

/* static\imported.dat (import_anims.py): "IMPA"; the shape and the count of
 * animations; each animation's number, its AnimInfo and its frames (frames x
 * directions AnimFrames, the shape's frames numbered past its own, unknown0d
 * their 13th bit), the count of overlay types and the weapon overlays of
 * each; then the first of the imported frames and their count, the size of
 * each and their bytes, as the shape file holds a frame. Read when first
 * asked for, into the game's memory (the frames drawn are the game's, as
 * missingFrame is), and this record of them in the arena: a snapshot or a
 * rewind takes both back together, read or not. */
constexpr u16 MOST_ANIMS = 4;
struct ImportedAnim {
    u16 anim;
    AnimInfo info;
    u16 entries;
    AnimFrame *frames;
    u16 overlayTypes;
    OverlayFrame *overlays;
};
struct Imported {
    bool read;
    bool any;
    u16 shape;
    u16 anims;
    ImportedAnim anim[MOST_ANIMS];
    u16 first;
    u16 count;
    /* Each frame's start in bytes, and the end after the last. */
    u32 *starts;
    u8 *bytes;
};
Imported &imported = gameGlobal<Imported>();

bool readAnim(File &file, ImportedAnim &anim)
{
    if (file.read(&anim.anim, 2, -1) != 2 || anim.anim >= ANIM_ACTIONS || file.read(&anim.info, 4, -1) != 4 ||
        file.read(&anim.entries, 2, -1) != 2) {
        return false;
    }
    const auto size = static_cast<s32>(anim.entries * sizeof(AnimFrame));
    anim.frames = new AnimFrame[anim.entries];
    if (file.read(anim.frames, size, -1) != size || file.read(&anim.overlayTypes, 2, -1) != 2) {
        return false;
    }
    if (anim.overlayTypes != 0) {
        const auto overlays = static_cast<s32>(static_cast<u32>(anim.entries) * anim.overlayTypes * sizeof(OverlayFrame));
        anim.overlays = new OverlayFrame[static_cast<u32>(anim.entries) * anim.overlayTypes];
        if (file.read(anim.overlays, overlays, -1) != overlays) {
            return false;
        }
    }
    return true;
}

bool readFrames(File &file)
{
    if (file.read(&imported.first, 2, -1) != 2 || file.read(&imported.count, 2, -1) != 2) {
        return false;
    }
    auto *sizes = new u32[imported.count];
    const auto sizeBytes = static_cast<s32>(imported.count * sizeof(u32));
    if (file.read(sizes, sizeBytes, -1) != sizeBytes) {
        delete[] sizes;
        return false;
    }
    /* Each frame's start, kept to 16 bytes as an allocation's (the
     * ShapeFrame after the FrameBlock as the cache's are aligned). */
    imported.starts = new u32[imported.count + 1];
    u32 total = 0;
    for (u16 i = 0; i < imported.count; i++) {
        imported.starts[i] = total;
        total += (sizes[i] + 15) & ~15U;
    }
    imported.starts[imported.count] = total;
    imported.bytes = new u8[total];
    bool whole = true;
    for (u16 i = 0; i < imported.count && whole; i++) {
        whole = file.read(imported.bytes + imported.starts[i], static_cast<s32>(sizes[i]), -1) ==
                static_cast<s32>(sizes[i]);
    }
    delete[] sizes;
    return whole;
}

void readImported()
{
    imported.read = true;
    char *path = makePath(nullptr, staticDirectory, "imported.dat", nullptr);
    if (fileExists(path) == 0) {
        return;
    }
    File file;
    file.open(path, FILE_READ, 0);
    if (file.isOpen() == 0U) {
        return;
    }
    char magic[4];
    bool whole = file.read(magic, 4, 0) == 4 && std::memcmp(magic, "IMPA", 4) == 0 &&
                 file.read(&imported.shape, 2, -1) == 2 && file.read(&imported.anims, 2, -1) == 2 &&
                 imported.anims <= MOST_ANIMS;
    for (u16 a = 0; whole && a < imported.anims; a++) {
        whole = readAnim(file, imported.anim[a]);
    }
    whole = whole && readFrames(file);
    file.close();
    if (!whole) {
        releaseImported();
        imported.read = true;
        return;
    }
    imported.any = true;
}

Imported *data()
{
    if (!imported.read) {
        readImported();
    }
    return imported.any ? &imported : nullptr;
}

const ImportedAnim *find(u16 shape, u16 anim)
{
    Imported *in = data();
    if (in == nullptr || shape != in->shape) {
        return nullptr;
    }
    for (u16 a = 0; a < in->anims; a++) {
        if (in->anim[a].anim == anim) {
            return &in->anim[a];
        }
    }
    return nullptr;
}

}

ShapeFrame *importedFrame(u16 shape, u16 frame)
{
    Imported *in = data();
    if (in == nullptr || shape != in->shape || frame < in->first || frame >= in->first + in->count) {
        return nullptr;
    }
    /* A frame's bytes start with the room the cache keeps its FrameBlock
     * in (ShapeCacheHandler::loadFrame); the ShapeFrame is after it. */
    return reinterpret_cast<ShapeFrame *>(in->bytes + in->starts[frame - in->first] + sizeof(FrameBlock));
}

u16 animShapeFrame(const AnimFrame &frame)
{
    return (frame.unknown0d != 0U && data() != nullptr) ? static_cast<u16>(frame.frame | 0x1000U) : frame.frame;
}

void importAnims(u16 shape, ActorAnim *table)
{
    Imported *in = data();
    if (in == nullptr || shape != in->shape) {
        return;
    }
    for (u16 a = 0; a < in->anims; a++) {
        const ImportedAnim &from = in->anim[a];
        if (table->actions[from.anim] != nullptr) {
            continue;
        }
        auto *action = new AnimAction;
        action->info = from.info;
        action->allocate();
        std::memcpy(action->frameList, from.frames,
                    std::min<std::size_t>(action->size(), from.entries * sizeof(AnimFrame)));
        table->setAction(static_cast<i16>(from.anim), action);
    }
}

const OverlayFrame *importedOverlays(u16 anim, u16 type)
{
    const Imported *in = data();
    const ImportedAnim *from = (in != nullptr) ? find(in->shape, anim) : nullptr;
    if (from == nullptr || from->overlays == nullptr || type >= from->overlayTypes) {
        return nullptr;
    }
    return from->overlays + (static_cast<u32>(type) * from->entries);
}

bool importedAnim(u16 shape, u16 anim)
{
    return find(shape, anim) != nullptr;
}

namespace {

/* The animations made a frame, and the frames each had. */
const u16 QUICK_ANIMS[] = {ANIM_READY_SMALL, ANIM_UNREADY_SMALL};
u8 quickFrames[std::size(QUICK_ANIMS)] = {};

}

void quickenAnims(u16 shape, ActorAnim *table)
{
    if (shape != 1 || !pointerSetting()) {
        return;
    }
    for (std::size_t q = 0; q < std::size(QUICK_ANIMS); q++) {
        AnimAction *old = table->actions[QUICK_ANIMS[q]];
        if (old == nullptr || old->info.frames <= 1) {
            continue;
        }
        const u8 frames = old->info.frames;
        const int directions = old->info.sixteenDirections ? 16 : 8;
        auto *quick = new AnimAction;
        quick->info = old->info;
        quick->info.frames = 1;
        quick->allocate();
        for (int d = 0; d < directions; d++) {
            quick->frameList[d] = old->frameList[(d * frames) + frames - 1];
        }
        delete old;
        table->setAction(static_cast<i16>(QUICK_ANIMS[q]), quick);
        quickFrames[q] = frames;
    }
}

u8 quickenedFrames(u16 shape, u16 anim)
{
    for (std::size_t q = 0; q < std::size(QUICK_ANIMS); q++) {
        if (shape == 1 && anim == QUICK_ANIMS[q] && pointerSetting()) {
            return quickFrames[q];
        }
    }
    return 0;
}

void releaseImported()
{
    for (ImportedAnim &anim : imported.anim) {
        delete[] anim.frames;
        delete[] anim.overlays;
    }
    delete[] imported.starts;
    delete[] imported.bytes;
    imported = Imported{};
}
