/* Upscaled textures. See upscaled.h. */
#include "upscaled.h"
#include "gamestate.h"
#include "camera.h"
#include "colours.h"
#include "control.h"
#include "crusys.h"
#include "drawframe.h"
#include "enhancements.h"
#include "files.h"
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
#include "turns.h"
#include "type.h"
#include "viewport.h"
#include "widesides.h"
#include "world.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

/* static\upscaled.dat (upscale.py): "UPS2", the scale (u16) and the count
 * of frames (u32); each frame's shape, frame and size upscaled (u16 each)
 * and its first pixel's place (u32), in the order of shape and frame; then
 * the pixels, 0xRRGGBBAA. Held whole outside the arena (files.h's
 * fileHeld: a hundred megabytes with the Silencer's), read when first asked
 * for; this record of it in the arena. */
const std::size_t HEADER = 10;
const std::size_t ENTRY = 12;
struct Upscaled {
    bool read;
    bool any;
    u32 count;
    const u8 *table;
    const u8 *pixels;
};
Upscaled &upscaled = gameGlobal<Upscaled>();

u16 word(const u8 *at)
{
    return static_cast<u16>(at[0] | (at[1] << 8));
}

u32 dword(const u8 *at)
{
    u32 value = 0;
    std::memcpy(&value, at, 4);
    return value;
}

void readUpscaled()
{
    upscaled.read = true;
    std::size_t size = 0;
    const u8 *data = fileHeld(makePath(nullptr, staticDirectory, "upscaled.dat", nullptr), &size);
    if (data == nullptr || size < HEADER || std::memcmp(data, "UPS2", 4) != 0 || word(data + 4) != 2) {
        return;
    }
    const u32 count = dword(data + 6);
    if (count == 0 || size < HEADER + (count * ENTRY)) {
        return;
    }
    const u8 *last = data + HEADER + ((count - 1) * ENTRY);
    const std::size_t pixels = (static_cast<std::size_t>(dword(last + 8)) + (word(last + 4) * word(last + 6))) * 4;
    if (size < HEADER + (count * ENTRY) + pixels) {
        return;
    }
    upscaled.count = count;
    upscaled.table = data + HEADER;
    upscaled.pixels = data + HEADER + (count * ENTRY);
    upscaled.any = true;
}

/* The upscaled frame of shape's frame, its index, -1 for none: the table
 * is in the order of shape and frame. */
int find(u16 shape, u16 frame)
{
    if (!upscaled.read) {
        readUpscaled();
    }
    if (!upscaled.any) {
        return -1;
    }
    const u32 key = (static_cast<u32>(shape) << 16) | frame;
    u32 low = 0;
    u32 high = upscaled.count;
    while (low < high) {
        const u32 middle = (low + high) / 2;
        const u8 *entry = upscaled.table + (middle * ENTRY);
        const u32 here = (static_cast<u32>(word(entry)) << 16) | word(entry + 2);
        if (here == key) {
            return static_cast<int>(middle);
        }
        if (here < key) {
            low = middle + 1;
        } else {
            high = middle;
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

/* A colour's brightness, 0-255 a component. */
int brightness(int red, int green, int blue)
{
    return ((red * 3) + (green * 6) + blue) / 10;
}

}

bool upscaledFrame(u16 shape, u16 frame)
{
    return find(shape, frame) >= 0;
}

void releaseUpscaled()
{
    /* The bytes stay held (fileHeld): only the record goes. */
    upscaled = Upscaled{};
}

void drawUpscaled(bool silencer)
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
    /* The items on the screen with an upscaled frame, and their box: the
     * Silencer alone, or all but him. */
    int count = 0;
    int boxLeft = frameWidth;
    int boxTop = frameHeight;
    int boxRight = -1;
    int boxBottom = -1;
    const auto note = [&](u16 item) {
        /* The frame it is drawn in (turns.h). */
        const u16 frameNumber = turnFrame(item, Item(item).frame());
        const int index = find(itemShape[item], frameNumber);
        if (index < 0) {
            return;
        }
        const auto *frame = reinterpret_cast<const u8 *>(cachedShapeFrame(itemShape[item], frameNumber));
        if (frame == nullptr) {
            return;
        }
        /* DL_ItemNode::draw's place, scrolled, on the frame. */
        const i16 hx = static_cast<i16>(itemX[item] >> 1);
        const i16 hy = static_cast<i16>(itemY[item] >> 1);
        const int x = hx - hy - scrollX + frameLeft;
        const int y = (hx >> 1) + (hy >> 1) - itemZ[item] - scrollY + frameTop;
        const bool mirrored = (itemFlags[item] & ITEM_FLIPPED) != 0;
        const s32 width = drawframe::dword(frame + 4);
        const s32 height = drawframe::dword(frame + 8);
        const u8 *entry = upscaled.table + (static_cast<std::size_t>(index) * ENTRY);
        if (word(entry + 4) != width * SCALE || word(entry + 6) != height * SCALE) {
            return;
        }
        const int left = mirrored ? x - (width - 1 - drawframe::dword(frame + 0xc)) : x - drawframe::dword(frame + 0xc);
        const int top = y - drawframe::dword(frame + 0x10);
        if (left + width <= 0 || top + height <= 0 || left >= frameWidth || top >= frameHeight) {
            return;
        }
        drawn[count++] = Drawn{index, frame, left, top, mirrored};
        boxLeft = std::min(boxLeft, std::max(0, left));
        boxTop = std::min(boxTop, std::max(0, top));
        boxRight = std::max(boxRight, std::min(frameWidth - 1, left + width - 1));
        boxBottom = std::max(boxBottom, std::min(frameHeight - 1, top + height - 1));
    };
    if (silencer) {
        const Item him(controlledActor);
        if (him.isValid() && him.isSilencer() && (itemFlags[him.id] & (ITEM_CONTAINED | ITEM_INVISIBLE)) == 0) {
            note(him.id);
        }
    } else {
        u16 middleX;
        u16 middleY;
        screenToMap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, &middleX, &middleY);
        const int column = middleX >> CHUNK_SHIFT;
        const int row = middleY >> CHUNK_SHIFT;
        for (int c = std::max(0, column - 5); c <= std::min(CHUNKS - 1, column + 5) && count < MOST_DRAWN; c++) {
            for (int r = std::max(0, row - 5); r <= std::min(CHUNKS - 1, row + 5) && count < MOST_DRAWN; r++) {
                for (u16 item = mapChunks[c][r]; item != 0U && count < MOST_DRAWN; item = itemNext[item]) {
                    if (item < FIRST_ITEM || (itemFlags[item] & (ITEM_CONTAINED | ITEM_INVISIBLE)) != 0 ||
                        Item(item).isSilencer()) {
                        continue;
                    }
                    note(item);
                }
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
     * 256), as the Silencer's colours have it; his colours' entries in
     * those (colours.h), 0 for the others. */
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
    std::uint32_t his[256] = {};
    if (silencer) {
        silencerColours(his);
    }
    bool any = false;
    for (int d = 0; d < count; d++) {
        const Drawn &item = drawn[d];
        const u8 *entry = upscaled.table + (static_cast<std::size_t>(item.index) * ENTRY);
        const int upWidth = word(entry + 4);
        const u8 *source = upscaled.pixels + (static_cast<std::size_t>(dword(entry + 8)) * 4);
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
            const u8 *rowEntry = frame + 0x14 + (4 * r);
            const u8 *p = rowEntry + static_cast<u32>(drawframe::dword(rowEntry));
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
                    /* His colours' entries: the chosen colour (faded), as
                     * bright as the upscaled pixel is against the entry. */
                    const std::uint32_t chosen = his[colour];
                    const int entryBrightness =
                        std::max(1, brightness((palette[colour].red * 255) / 63, (palette[colour].green * 255) / 63,
                                               (palette[colour].blue * 255) / 63));
                    for (int dy = 0; dy < SCALE; dy++) {
                        for (int dx = 0; dx < SCALE; dx++) {
                            /* Mirrored, the upscaled block's columns too. */
                            const int sourceX = item.mirrored ? (i * SCALE) + (SCALE - 1 - dx) : (i * SCALE) + dx;
                            std::uint32_t pixel = 0;
                            std::memcpy(&pixel,
                                        source + ((((static_cast<std::size_t>((r * SCALE) + dy) * upWidth)) + sourceX) * 4),
                                        4);
                            if ((pixel & 0xffU) == 0U) {
                                continue;
                            }
                            std::uint32_t made = pixel & 0xffU;
                            if (chosen != 0U) {
                                const int light = brightness(static_cast<int>((pixel >> 24) & 0xffU),
                                                             static_cast<int>((pixel >> 16) & 0xffU),
                                                             static_cast<int>((pixel >> 8) & 0xffU));
                                for (int k = 0; k < 3; k++) {
                                    const auto component = static_cast<int>((chosen >> (24 - (8 * k))) & 0xffU);
                                    made |= static_cast<std::uint32_t>(
                                                std::min(255, (component * light) / entryBrightness))
                                            << (24 - (8 * k));
                                }
                            } else {
                                for (int k = 0; k < 3; k++) {
                                    const auto component = static_cast<int>((pixel >> (24 - (8 * k))) & 0xffU);
                                    made |= static_cast<std::uint32_t>((component * fade[colour][k]) / 256)
                                            << (24 - (8 * k));
                                }
                            }
                            const int lx = ((frameColumn - boxLeft) * SCALE) + dx;
                            const int ly = ((frameRow - boxTop) * SCALE) + dy;
                            layer[(static_cast<std::size_t>(ly) * pitch) + lx] = made;
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
