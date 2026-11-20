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
#include "kernel.h"
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

/* The Silencer as last drawn (upscaledDrawn). */
struct Drawing {
    bool any;
    u16 item;
    u16 shape;
    u16 frame;
    i16 x;
    i16 y;
    bool mirrored;
};
/* His last drawing and the one before: the last may not be on the screen
 * shown yet (drawn, not yet copied to the video memory). */
Drawing silencerDrawn{};
Drawing silencerBefore{};
/* Each item as last drawn (any, a guard walking, a fan turning: laid over
 * as the screen shows it, not as it is since), and the map they were drawn
 * on. */
struct Drawn1 {
    u16 shape;
    u16 frame;
    i16 x;
    i16 y;
    u8 mirrored;
    u8 any;
};
Drawn1 itemDrawn[ITEMS];
u16 drawnMap = 0xffff;

/* How many of a drawing's pixels (every seventh) the screen shows, in his
 * colours' entries; shown is the frame's pixel at a column and row. */
template <class Shown> int showing(const Drawing &drawing, int frameLeft, int frameTop, const Shown &shown)
{
    const auto *frame = reinterpret_cast<const u8 *>(cachedShapeFrame(drawing.shape, drawing.frame));
    if (frame == nullptr) {
        return 0;
    }
    const s32 width = drawframe::dword(frame + 4);
    const s32 height = drawframe::dword(frame + 8);
    const int x = drawing.x - scrollX + frameLeft;
    const int y = drawing.y - scrollY + frameTop;
    const int left = drawing.mirrored ? x - (width - 1 - drawframe::dword(frame + 0xc)) : x - drawframe::dword(frame + 0xc);
    const int top = y - drawframe::dword(frame + 0x10);
    const bool fills = (drawframe::dword(frame) & 1) != 0;
    int count = 0;
    int seen = 0;
    for (s32 r = 0; r < height; r++) {
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
                if ((seen++ % 7) != 0) {
                    continue;
                }
                const u8 colour = fill ? *colours : colours[i - c];
                const int column = drawing.mirrored ? left + (width - 1 - i) : left + i;
                count += shown(column, top + r) == silencerShownEntry(colour) ? 1 : 0;
            }
            c += length;
        }
    }
    return count;
}

/* The world's layer in tiles of TILE pixels square of the world (not the
 * screen: scrolled, a tile's pixels move with it, and stay its own), each
 * kept by the overlay (overlayPictureKept) and made again only when the
 * screen's pixels it covers have changed (an item moved over it, an
 * animation): what the screen showed there when it was made, its part of
 * the tile shown, whether it had any upscaled pixel, its version and the
 * frame it was last drawn in (the overlay forgets a picture not drawn for a
 * while: made again then). */
const int TILE = 64;
const int TILES = 1024;
struct Tile {
    bool used;
    int x;
    int y;
    int left;
    int top;
    int right;
    int bottom;
    bool empty;
    u32 version;
    u32 drawn;
    /* The frame it was made in, and the palette's entries it shows. */
    u32 made;
    std::uint64_t entries[4];
    u8 seen[TILE * TILE];
};
Tile tiles[TILES];
u32 tileVersion = 0;
u32 tileFrames = 0;
/* What all the tiles were made for: the map and the frame's size; all
 * made again when one changes. Each entry's fade as last seen and the
 * frame it last changed in: a tile showing one changed since it was made
 * is made again (the game turns some of its colours round each frame, the
 * lights' and the screens'; a fade turns all). */
u16 tilesMap = 0xffff;
int tilesWidth = 0;
int tilesHeight = 0;
int fadeSeen[256][3];
u32 fadeChanged[256];
const std::uint32_t TILE_KEYS = 0x55500000U;
/* A tile's pixels as made, and the frame's pixels shown (the video
 * memory's in the middle, the sides' round it). */
std::uint32_t tilePixels[TILE * SCALE * TILE * SCALE];
u8 frameShown[static_cast<std::size_t>(WIDE_FRAME_MOST) * TALL_FRAME_MOST];

/* The tile of the world's x, y: its slot, the oldest of its probes when it
 * has none (another's, forgotten). */
int tileSlot(int x, int y)
{
    const auto hash = static_cast<std::uint32_t>((x * 73856093) ^ (y * 19349663));
    int oldest = -1;
    for (int probe = 0; probe < 16; probe++) {
        const int slot = static_cast<int>((hash + static_cast<std::uint32_t>(probe)) & (TILES - 1));
        Tile &tile = tiles[slot];
        if (tile.used && tile.x == x && tile.y == y) {
            return slot;
        }
        if (oldest < 0 || !tile.used || (tiles[oldest].used && tile.drawn < tiles[oldest].drawn)) {
            oldest = slot;
        }
    }
    Tile &tile = tiles[oldest];
    tile.used = true;
    tile.x = x;
    tile.y = y;
    tile.left = -1;
    return oldest;
}

/* x divided by TILE, rounded down. */
int tileOf(int x)
{
    return x >= 0 ? x / TILE : -((-x + TILE - 1) / TILE);
}

/* A colour's brightness, 0-255 a component. */
int brightness(int red, int green, int blue)
{
    return ((red * 3) + (green * 6) + blue) / 10;
}

}

void upscaledDrawn(u16 item, u16 shape, u16 frame, i16 x, i16 y, bool mirrored)
{
    if (drawnMap != currentMap) {
        drawnMap = currentMap;
        std::fill(std::begin(itemDrawn), std::end(itemDrawn), Drawn1{});
    }
    if (item < ITEMS) {
        itemDrawn[item] = Drawn1{shape, frame, x, y, static_cast<u8>(mirrored ? 1 : 0), 1};
    }
    if (!Item(item).isSilencer()) {
        return;
    }
    const Drawing now{true, item, shape, frame, x, y, mirrored};
    if (now.frame != silencerDrawn.frame || now.x != silencerDrawn.x || now.y != silencerDrawn.y ||
        now.mirrored != silencerDrawn.mirrored) {
        silencerBefore = silencerDrawn;
    }
    silencerDrawn = now;
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
    /* The screen shown (the video memory, flat: what the window shows): the
     * screen drawn may be part way through a redraw (the Silencer taken
     * away from where he stood, not yet drawn where he is: he came and
     * went). */
    const u8 *shownScreen = screenSvgaMemory();
    const ViewportPixels screen = drawViewport->pixels();
    /* The world's: the frame's pixels shown, the sides' and the video
     * memory's in their middle, read once. */
    if (!silencer) {
        if (sides != nullptr && sidesWidth == frameWidth && sidesHeight == frameHeight) {
            std::memcpy(frameShown, sides, static_cast<std::size_t>(frameWidth) * frameHeight);
        } else {
            std::fill_n(frameShown, static_cast<std::size_t>(frameWidth) * frameHeight, static_cast<u8>(0));
        }
        for (int y = 0; y < SCREEN_HEIGHT && y + frameTop < frameHeight; y++) {
            const u8 *from = shownScreen != nullptr ? shownScreen + (y * SCREEN_WIDTH)
                                                    : screen.pixels + screen.rows[y];
            std::memcpy(frameShown + (static_cast<std::size_t>(y + frameTop) * frameWidth) + frameLeft, from,
                        SCREEN_WIDTH);
        }
    }
    /* What the frame shows at its column and row: the screen's pixel or
     * the sides'; -1 for none. */
    const auto shown = [&](int column, int row) -> int {
        if (!silencer) {
            return column >= 0 && row >= 0 && column < frameWidth && row < frameHeight
                       ? frameShown[(static_cast<std::size_t>(row) * frameWidth) + column]
                       : -1;
        }
        const int x = column - frameLeft;
        const int y = row - frameTop;
        if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
            if (x < screen.left || x > screen.right || y < screen.top || y > screen.bottom) {
                return -1;
            }
            if (shownScreen != nullptr) {
                return shownScreen[(y * SCREEN_WIDTH) + x];
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
        /* The frame it is drawn in (turns.h), at DL_ItemNode::draw's place,
         * scrolled, on the frame; as last drawn when it has been. */
        u16 shape = itemShape[item];
        u16 frameNumber = turnFrame(item, Item(item).frame());
        const i16 hx = static_cast<i16>(itemX[item] >> 1);
        const i16 hy = static_cast<i16>(itemY[item] >> 1);
        int x = hx - hy - scrollX + frameLeft;
        int y = (hx >> 1) + (hy >> 1) - itemZ[item] - scrollY + frameTop;
        bool mirrored = (itemFlags[item] & ITEM_FLIPPED) != 0;
        if (!silencer && item < ITEMS && itemDrawn[item].any != 0U && drawnMap == currentMap) {
            const Drawn1 &last = itemDrawn[item];
            shape = last.shape;
            frameNumber = last.frame;
            x = last.x - scrollX + frameLeft;
            y = last.y - scrollY + frameTop;
            mirrored = last.mirrored != 0U;
        }
        if (silencer && silencerDrawn.any && silencerDrawn.item == item) {
            /* Whichever of his last two drawings the screen shows more
             * of. */
            const Drawing &use = (silencerBefore.any && silencerBefore.item == item &&
                                  showing(silencerBefore, frameLeft, frameTop, shown) >
                                      showing(silencerDrawn, frameLeft, frameTop, shown))
                                     ? silencerBefore
                                     : silencerDrawn;
            shape = use.shape;
            frameNumber = use.frame;
            x = use.x - scrollX + frameLeft;
            y = use.y - scrollY + frameTop;
            mirrored = use.mirrored;
        }
        const int index = find(shape, frameNumber);
        if (index < 0) {
            return;
        }
        const auto *frame = reinterpret_cast<const u8 *>(cachedShapeFrame(shape, frameNumber));
        if (frame == nullptr) {
            return;
        }
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
    if (silencer && (count == 0 || boxRight < boxLeft || boxBottom < boxTop)) {
        return;
    }
    /* The palette's fade of each entry (the screen's against the game's, of
     * 256), as the Silencer's colours have it; his colours' entries in
     * those (colours.h), 0 for the others. */
    u8 now[256 * 3];
    screenGetColors(0, 256, now);
    const RGB *palette = gamePalette->data.get();
    int fade[256][3];
    /* Each entry unfaded (the screen's palette the game's, as mostly):
     * its upscaled pixels as they are. */
    bool unfaded[256];
    /* An entry the game shows in another colour than the one its upscaled
     * pixels were made in, faded (its colours turned round, the floor's
     * marks' and the lights': the game changes its palette itself): those
     * are not drawn; the game's show. The colours they were made in are
     * gamepal.pal's (upscale.py's). */
    bool other[256];
    std::size_t artSize = 0;
    const u8 *art = fileHeld(makePath(nullptr, staticDirectory, "gamepal.pal", nullptr), &artSize);
    if (artSize < 256 * 3) {
        art = nullptr;
    }
    for (int entry = 0; entry < 256; entry++) {
        const u8 base[3] = {art != nullptr ? art[entry * 3] : palette[entry].red,
                            art != nullptr ? art[(entry * 3) + 1] : palette[entry].green,
                            art != nullptr ? art[(entry * 3) + 2] : palette[entry].blue};
        unfaded[entry] = true;
        int least = 1 << 20;
        int most = 0;
        for (int k = 0; k < 3; k++) {
            const int shownPart = now[(entry * 3) + k] & 0x3f;
            const int ratio = base[k] == 0U ? (shownPart == 0 ? 256 : 1 << 16) : (shownPart * 256) / base[k];
            fade[entry][k] = std::min(256, ratio);
            unfaded[entry] = unfaded[entry] && fade[entry][k] == 256;
            if (base[k] >= 4U || shownPart >= 4) {
                least = std::min(least, ratio);
                most = std::max(most, ratio);
            }
        }
        other[entry] = !unfaded[entry] && (most > 264 || most - least > 24);
    }
    std::uint32_t his[256] = {};
    if (silencer) {
        silencerColours(his);
    }
    /* The items' pixels within clipLeft..clipRight, clipTop..clipBottom of
     * the frame into out (pitch a row), its top left corner the box's; whether
     * any. */
    const auto paint = [&](int clipLeft, int clipTop, int clipRight, int clipBottom, std::uint32_t *out, int pitch) {
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
            if (frameRow < clipTop || frameRow > clipBottom || item.left > clipRight ||
                item.left + width - 1 < clipLeft) {
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
                    if (frameColumn < clipLeft || frameColumn > clipRight) {
                        continue;
                    }
                    const u8 colour = fill ? *colours : colours[i - c];
                    /* His pixels: the screen shows them in his colours'
                     * entries (colours.h). */
                    if (other[colour] ||
                        shown(frameColumn, frameRow) != (silencer ? silencerShownEntry(colour) : colour)) {
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
                            } else if (unfaded[colour]) {
                                made = pixel;
                            } else {
                                for (int k = 0; k < 3; k++) {
                                    const auto component = static_cast<int>((pixel >> (24 - (8 * k))) & 0xffU);
                                    made |= static_cast<std::uint32_t>((component * fade[colour][k]) / 256)
                                            << (24 - (8 * k));
                                }
                            }
                            const int lx = ((frameColumn - clipLeft) * SCALE) + dx;
                            const int ly = ((frameRow - clipTop) * SCALE) + dy;
                            out[(static_cast<std::size_t>(ly) * pitch) + lx] = made;
                            any = true;
                        }
                    }
                }
                c += length;
            }
        }
    }
    return any;
    };
    if (silencer) {
        const int pitch = (boxRight - boxLeft + 1) * SCALE;
        const int rows = (boxBottom - boxTop + 1) * SCALE;
        std::fill_n(layer, static_cast<std::size_t>(pitch) * rows, 0U);
        if (paint(boxLeft, boxTop, boxRight, boxBottom, layer, pitch)) {
            overlayPictureAs(layer, pitch, rows, static_cast<float>(boxLeft - frameLeft),
                             static_cast<float>(boxTop - frameTop), static_cast<float>(boxRight - boxLeft + 1),
                             static_cast<float>(boxBottom - boxTop + 1), OverlayBlend::BLEND, false);
        }
        return;
    }
    /* The world's tiles: all made again for another map or frame. */
    if (tilesMap != currentMap || tilesWidth != frameWidth || tilesHeight != frameHeight) {
        for (Tile &tile : tiles) {
            tile.used = false;
        }
        tilesMap = currentMap;
        tilesWidth = frameWidth;
        tilesHeight = frameHeight;
    }
    tileFrames++;
    for (int entry = 0; entry < 256; entry++) {
        if (fadeSeen[entry][0] != fade[entry][0] || fadeSeen[entry][1] != fade[entry][1] ||
            fadeSeen[entry][2] != fade[entry][2]) {
            fadeSeen[entry][0] = fade[entry][0];
            fadeSeen[entry][1] = fade[entry][1];
            fadeSeen[entry][2] = fade[entry][2];
            fadeChanged[entry] = tileFrames;
        }
    }
    const int firstX = tileOf(scrollX - frameLeft);
    const int lastX = tileOf(frameWidth - 1 - frameLeft + scrollX);
    const int firstY = tileOf(scrollY - frameTop);
    const int lastY = tileOf(frameHeight - 1 - frameTop + scrollY);
    for (int ty = firstY; ty <= lastY; ty++) {
        for (int tx = firstX; tx <= lastX; tx++) {
            /* The tile's top left corner on the frame, and its part shown. */
            const int x = (tx * TILE) - scrollX + frameLeft;
            const int y = (ty * TILE) - scrollY + frameTop;
            const int left = std::max(0, x);
            const int top = std::max(0, y);
            const int right = std::min(frameWidth - 1, x + TILE - 1);
            const int bottom = std::min(frameHeight - 1, y + TILE - 1);
            if (right < left || bottom < top) {
                continue;
            }
            const int slot = tileSlot(tx, ty);
            Tile &tile = tiles[slot];
            const int across = right - left + 1;
            bool same = tile.left == left - x && tile.top == top - y && tile.right == right - x &&
                        tile.bottom == bottom - y && tileFrames - tile.drawn < 60;
            for (int word = 0; same && word < 4; word++) {
                for (std::uint64_t bits = tile.entries[word]; same && bits != 0U; bits &= bits - 1) {
                    same = fadeChanged[(word * 64) + __builtin_ctzll(bits)] <= tile.made;
                }
            }
            for (int row = top; same && row <= bottom; row++) {
                same = std::memcmp(frameShown + (static_cast<std::size_t>(row) * frameWidth) + left,
                                   tile.seen + ((row - y) * TILE) + (left - x), static_cast<std::size_t>(across)) == 0;
            }
            if (!same) {
                for (int row = top; row <= bottom; row++) {
                    std::memcpy(tile.seen + ((row - y) * TILE) + (left - x),
                                frameShown + (static_cast<std::size_t>(row) * frameWidth) + left,
                                static_cast<std::size_t>(across));
                }
                tile.left = left - x;
                tile.top = top - y;
                tile.right = right - x;
                tile.bottom = bottom - y;
                tile.made = tileFrames;
                std::fill(std::begin(tile.entries), std::end(tile.entries), 0U);
                for (int row = top; row <= bottom; row++) {
                    for (int column = left; column <= right; column++) {
                        const u8 entry = tile.seen[((row - y) * TILE) + (column - x)];
                        tile.entries[entry >> 6] |= std::uint64_t{1} << (entry & 63);
                    }
                }
                std::fill(std::begin(tilePixels), std::end(tilePixels), 0U);
                tile.empty = !paint(left, top, right, bottom,
                                    tilePixels + ((static_cast<std::size_t>(top - y) * SCALE) * (TILE * SCALE)) +
                                        ((left - x) * SCALE),
                                    TILE * SCALE);
                tile.version = ++tileVersion;
            }
            tile.drawn = tileFrames;
            if (!tile.empty) {
                overlayPictureKept(TILE_KEYS + static_cast<std::uint32_t>(slot), tile.version,
                                   same ? nullptr : tilePixels, TILE * SCALE, TILE * SCALE,
                                   static_cast<float>(x - frameLeft), static_cast<float>(y - frameTop),
                                   static_cast<float>(TILE), static_cast<float>(TILE), OverlayBlend::BLEND, false);
            }
        }
    }
}
