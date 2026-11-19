/* Upscaled textures. See upscaled.h. */
#include "upscaled.h"
#include "gamestate.h"
#include "camera.h"
#include "crusys.h"
#include "drawframe.h"
#include "enhancements.h"
#include "file.h"
#include "filespec.h"
#include "gamefile.h"
#include "gamepal.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "overlay.h"
#include "screen.h"
#include "shape.h"
#include "shaphand.h"
#include "type.h"
#include "utility.h"
#include "viewport.h"
#include "widesides.h"
#include "world.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

/* static\upscaled.dat (upscale.py): "UPSC", the scale and the count of
 * frames; each frame's shape, frame and size upscaled; then their pixels,
 * 0xRRGGBBAA. Read when first asked for, into the game's memory, and this
 * record of it in the arena: a snapshot or a rewind takes both back
 * together, read or not. */
struct UpFrame {
    u16 shape;
    u16 frame;
    u16 width;
    u16 height;
};
struct Upscaled {
    bool read;
    bool any;
    u16 scale;
    u16 count;
    UpFrame *frames;
    /* Each frame's first pixel. */
    u32 *starts;
    std::uint32_t *pixels;
};
Upscaled &upscaled = gameGlobal<Upscaled>();

void readUpscaled()
{
    upscaled.read = true;
    char *path = makePath(nullptr, staticDirectory, "upscaled.dat", nullptr);
    if (fileExists(path) == 0) {
        return;
    }
    File file;
    file.open(path, FILE_READ, 0);
    if (file.isOpen() == 0U) {
        return;
    }
    char magic[4];
    bool whole = file.read(magic, 4, 0) == 4 && std::memcmp(magic, "UPSC", 4) == 0 &&
                 file.read(&upscaled.scale, 2, -1) == 2 && file.read(&upscaled.count, 2, -1) == 2 &&
                 upscaled.scale == 2 && upscaled.count != 0;
    if (whole) {
        upscaled.frames = new UpFrame[upscaled.count];
        const auto size = static_cast<s32>(upscaled.count * sizeof(UpFrame));
        whole = file.read(upscaled.frames, size, -1) == size;
    }
    if (whole) {
        upscaled.starts = new u32[upscaled.count];
        u32 total = 0;
        for (u16 i = 0; i < upscaled.count; i++) {
            upscaled.starts[i] = total;
            total += static_cast<u32>(upscaled.frames[i].width) * upscaled.frames[i].height;
        }
        upscaled.pixels = new std::uint32_t[total];
        const auto bytes = static_cast<s32>(total * sizeof(std::uint32_t));
        whole = file.read(upscaled.pixels, bytes, -1) == bytes;
    }
    file.close();
    if (!whole) {
        releaseUpscaled();
        upscaled.read = true;
        return;
    }
    upscaled.any = true;
}

/* The upscaled frame of shape's frame, its index, -1 for none. */
int find(u16 shape, u16 frame)
{
    if (!upscaled.read) {
        readUpscaled();
    }
    if (!upscaled.any) {
        return -1;
    }
    for (u16 i = 0; i < upscaled.count; i++) {
        if (upscaled.frames[i].shape == shape && upscaled.frames[i].frame == frame) {
            return i;
        }
    }
    return -1;
}

/* The layer: the frame's pixels twice over (the wide screen's frame,
 * widesides.h: the screen in its middle, the sides round it), its box's
 * only. */
const int SCALE = 2;
const int LAYER_WIDTH = WIDE_FRAME_MOST * SCALE;
const int LAYER_HEIGHT = TALL_FRAME_MOST * SCALE;
std::uint32_t layer[static_cast<std::size_t>(LAYER_WIDTH) * LAYER_HEIGHT];

/* An item to draw: its frame upscaled, the original, where on the frame
 * its frame's top left corner lands, and mirrored. */
struct Drawn {
    int index;
    const u8 *frame;
    int left;
    int top;
    bool mirrored;
};
const int MOST_DRAWN = 512;
Drawn drawn[MOST_DRAWN];

}

bool upscaledFrame(u16 shape, u16 frame)
{
    return find(shape, frame) >= 0;
}

void releaseUpscaled()
{
    delete[] upscaled.frames;
    delete[] upscaled.starts;
    delete[] upscaled.pixels;
    upscaled = Upscaled{};
}

void drawUpscaled()
{
    if (!upscaledSetting() || !hardwareSetting() || worldActive == 0U || worldFrozen != 0U ||
        drawViewport == nullptr) {
        return;
    }
    if (!upscaled.read) {
        readUpscaled();
    }
    if (!upscaled.any) {
        return;
    }
    /* The frame: its columns left of the screen and rows above it. */
    const int frameWidth = std::min<int>(screenWideWidth(), WIDE_FRAME_MOST);
    const int frameHeight = std::min<int>(screenWideHeight(), TALL_FRAME_MOST);
    const int frameLeft = (frameWidth - SCREEN_WIDTH) / 2;
    const int frameTop = (frameHeight - SCREEN_HEIGHT) / 2;
    int sidesWidth = 0;
    int sidesHeight = 0;
    const u8 *sides = wideSidesPixels(&sidesWidth, &sidesHeight);
    const ViewportPixels screen = drawViewport->pixels();
    /* What the frame shows at its column and row: the screen's pixel or
     * the sides'; -1 for none. */
    const auto shown = [&](int column, int row) -> int {
        const int x = column - frameLeft;
        const int y = row - frameTop;
        if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
            if (x < screen.left || x > screen.right || y < screen.top || y > screen.bottom) {
                return -1;
            }
            return screen.pixels[screen.rows[y] + static_cast<u32>(x)];
        }
        if (sides == nullptr || column < 0 || row < 0 || column >= sidesWidth || row >= sidesHeight) {
            return -1;
        }
        return sides[(static_cast<std::size_t>(row) * sidesWidth) + column];
    };
    /* The items on the screen with an upscaled frame, and their box. */
    int count = 0;
    int boxLeft = frameWidth;
    int boxTop = frameHeight;
    int boxRight = -1;
    int boxBottom = -1;
    u16 middleX;
    u16 middleY;
    screenToMap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, &middleX, &middleY);
    const int column = middleX >> CHUNK_SHIFT;
    const int row = middleY >> CHUNK_SHIFT;
    for (int c = std::max(0, column - 5); c <= std::min(CHUNKS - 1, column + 5) && count < MOST_DRAWN; c++) {
        for (int r = std::max(0, row - 5); r <= std::min(CHUNKS - 1, row + 5) && count < MOST_DRAWN; r++) {
            for (u16 item = mapChunks[c][r]; item != 0U && count < MOST_DRAWN; item = itemNext[item]) {
                if (item < FIRST_ITEM || (itemFlags[item] & (ITEM_CONTAINED | ITEM_INVISIBLE)) != 0) {
                    continue;
                }
                const u16 frameNumber = Item(item).frame();
                const int index = find(itemShape[item], frameNumber);
                if (index < 0) {
                    continue;
                }
                const auto *frame = reinterpret_cast<const u8 *>(cachedShapeFrame(itemShape[item], frameNumber));
                if (frame == nullptr) {
                    continue;
                }
                /* DL_ItemNode::draw's place, scrolled, on the frame. */
                const i16 hx = static_cast<i16>(itemX[item] >> 1);
                const i16 hy = static_cast<i16>(itemY[item] >> 1);
                const int x = hx - hy - scrollX + frameLeft;
                const int y = (hx >> 1) + (hy >> 1) - itemZ[item] - scrollY + frameTop;
                const bool mirrored = (itemFlags[item] & ITEM_FLIPPED) != 0;
                const s32 width = drawframe::dword(frame + 4);
                const s32 height = drawframe::dword(frame + 8);
                const UpFrame &up = upscaled.frames[index];
                if (up.width != width * SCALE || up.height != height * SCALE) {
                    continue;
                }
                const int left = mirrored ? x - (width - 1 - drawframe::dword(frame + 0xc))
                                          : x - drawframe::dword(frame + 0xc);
                const int top = y - drawframe::dword(frame + 0x10);
                if (left + width <= 0 || top + height <= 0 || left >= frameWidth || top >= frameHeight) {
                    continue;
                }
                drawn[count++] = Drawn{index, frame, left, top, mirrored};
                boxLeft = std::min(boxLeft, std::max(0, left));
                boxTop = std::min(boxTop, std::max(0, top));
                boxRight = std::max(boxRight, std::min(frameWidth - 1, left + width - 1));
                boxBottom = std::max(boxBottom, std::min(frameHeight - 1, top + height - 1));
            }
        }
    }
    if (count == 0 || boxRight < boxLeft || boxBottom < boxTop) {
        return;
    }
    const int pitch = (boxRight - boxLeft + 1) * SCALE;
    const int rows = (boxBottom - boxTop + 1) * SCALE;
    std::fill_n(layer, static_cast<std::size_t>(pitch) * rows, 0U);
    /* The palette's fade of each entry (the screen's against the game's, of
     * 256), as the Silencer's colours have it. */
    u8 now[256 * 3];
    screenGetColors(0, 256, now);
    const RGB *palette = gamePalette->data.get();
    int fade[256][3];
    for (int entry = 0; entry < 256; entry++) {
        const u8 base[3] = {palette[entry].red, palette[entry].green, palette[entry].blue};
        for (int k = 0; k < 3; k++) {
            fade[entry][k] = base[k] == 0U ? 256 : std::min(256, ((now[(entry * 3) + k] & 0x3f) * 256) / base[k]);
        }
    }
    bool any = false;
    for (int d = 0; d < count; d++) {
        const Drawn &item = drawn[d];
        const UpFrame &up = upscaled.frames[item.index];
        const std::uint32_t *source = upscaled.pixels + upscaled.starts[item.index];
        const u8 *frame = item.frame;
        const s32 width = drawframe::dword(frame + 4);
        const s32 height = drawframe::dword(frame + 8);
        const bool fills = (drawframe::dword(frame) & 1) != 0;
        /* The frame's runs (drawframe.h); each pixel that still shows, its
         * upscaled pixels. */
        for (s32 r = 0; r < height; r++) {
            const int frameRow = item.top + r;
            if (frameRow < boxTop || frameRow > boxBottom) {
                continue;
            }
            const u8 *entry = frame + 0x14 + (4 * r);
            const u8 *p = entry + static_cast<u32>(drawframe::dword(entry));
            s32 c = 0;
            while (c < width) {
                c += *p++;
                if (c >= width) {
                    break;
                }
                s32 length = *p++;
                bool fill = false;
                if (fills) {
                    fill = (length & 1) != 0;
                    length >>= 1;
                }
                const u8 *colours = p;
                p += fill ? 1 : length;
                for (s32 i = c; i < std::min(c + length, width); i++) {
                    const int frameColumn = item.mirrored ? item.left + (width - 1 - i) : item.left + i;
                    if (frameColumn < boxLeft || frameColumn > boxRight) {
                        continue;
                    }
                    const u8 colour = fill ? *colours : colours[i - c];
                    if (shown(frameColumn, frameRow) != colour) {
                        continue;
                    }
                    for (int dy = 0; dy < SCALE; dy++) {
                        for (int dx = 0; dx < SCALE; dx++) {
                            /* Mirrored, the upscaled block's columns too. */
                            const int sourceX = item.mirrored ? (i * SCALE) + (SCALE - 1 - dx) : (i * SCALE) + dx;
                            const std::uint32_t pixel =
                                source[(static_cast<std::size_t>((r * SCALE) + dy) * up.width) + sourceX];
                            if ((pixel & 0xffU) == 0U) {
                                continue;
                            }
                            std::uint32_t faded = pixel & 0xffU;
                            for (int k = 0; k < 3; k++) {
                                const auto component = static_cast<int>((pixel >> (24 - (8 * k))) & 0xffU);
                                faded |= static_cast<std::uint32_t>((component * fade[colour][k]) / 256)
                                         << (24 - (8 * k));
                            }
                            const int lx = ((frameColumn - boxLeft) * SCALE) + dx;
                            const int ly = ((frameRow - boxTop) * SCALE) + dy;
                            layer[(static_cast<std::size_t>(ly) * pitch) + lx] = faded;
                            any = true;
                        }
                    }
                }
                c += length;
            }
        }
    }
    if (!any) {
        return;
    }
    overlayPictureAs(layer, pitch, rows, static_cast<float>(boxLeft - frameLeft), static_cast<float>(boxTop - frameTop),
                     static_cast<float>(boxRight - boxLeft + 1), static_cast<float>(boxBottom - boxTop + 1),
                     OverlayBlend::BLEND, false);
}
