/* An animation of No Regret's and its frames, in No Remorse. See imported.h. */
#include "imported.h"
#include "gamestate.h"
#include "actanim.h"
#include "astream.h"
#include "file.h"
#include "filespec.h"
#include "gamefile.h"
#include "shaphand.h"
#include "utility.h"

#include <algorithm>
#include <cstring>

namespace {

/* static\fwdroll.dat (forward_roll.py): "FWDR"; the shape and the
 * animation's number, its AnimInfo and its frames (frames x directions
 * AnimFrames, the shape's frames numbered past its own); the first of the
 * imported frames and their count, the size of each and their bytes, as
 * the shape file holds a frame. Read when first asked for, into the game's
 * memory (the frames drawn are the game's, as missingFrame is), and this
 * record of them in the arena: a snapshot or a rewind takes both back
 * together, read or not. */
struct Imported {
    bool read;
    bool any;
    u16 shape;
    u16 anim;
    AnimInfo info;
    u16 entries;
    u16 first;
    u16 count;
    AnimFrame *frames;
    /* Each frame's start in bytes, and the end after the last. */
    u32 *starts;
    u8 *bytes;
};
Imported &imported = gameGlobal<Imported>();

void readImported()
{
    imported.read = true;
    char *path = makePath(nullptr, staticDirectory, "fwdroll.dat", nullptr);
    if (fileExists(path) == 0) {
        return;
    }
    File file;
    file.open(path, FILE_READ, 0);
    if (file.isOpen() == 0U) {
        return;
    }
    char magic[4];
    if (file.read(magic, 4, 0) != 4 || std::memcmp(magic, "FWDR", 4) != 0) {
        return;
    }
    file.read(&imported.shape, 2, -1);
    file.read(&imported.anim, 2, -1);
    file.read(&imported.info, 4, -1);
    file.read(&imported.entries, 2, -1);
    imported.frames = new AnimFrame[imported.entries];
    file.read(imported.frames, static_cast<s32>(imported.entries * sizeof(AnimFrame)), -1);
    file.read(&imported.first, 2, -1);
    file.read(&imported.count, 2, -1);
    auto *sizes = new u32[imported.count];
    file.read(sizes, static_cast<s32>(imported.count * sizeof(u32)), -1);
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
    for (u16 i = 0; i < imported.count; i++) {
        file.read(imported.bytes + imported.starts[i], static_cast<s32>(sizes[i]), -1);
    }
    delete[] sizes;
    file.close();
    imported.any = true;
}

Imported *data()
{
    if (!imported.read) {
        readImported();
    }
    return imported.any ? &imported : nullptr;
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

void importAnims(u16 shape, ActorAnim *table)
{
    Imported *in = data();
    if (in == nullptr || shape != in->shape || table->actions[in->anim] != nullptr) {
        return;
    }
    auto *action = new AnimAction;
    action->info = in->info;
    action->allocate();
    std::memcpy(action->frameList, in->frames, std::min<std::size_t>(action->size(), in->entries * sizeof(AnimFrame)));
    table->setAction(static_cast<i16>(in->anim), action);
}

bool importedAnim(u16 shape, u16 anim)
{
    const Imported *in = data();
    return in != nullptr && shape == in->shape && anim == in->anim;
}

void releaseImported()
{
    delete[] imported.frames;
    delete[] imported.starts;
    delete[] imported.bytes;
    imported = Imported{};
}
