/* Shadows. See shadows.h. */
#include "shadows.h"
#include "gamestate.h"
#include "camera.h"
#include "colours.h"
#include "gamepal.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "npc.h"
#include "pointer.h"
#include "shape.h"
#include "type.h"
#include "viewport.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace {

/* The light's slant: a pixel h above the feet falls h * SLANT_ACROSS / 16
 * to the right and h * SLANT_DOWN / 16 down from them. */
const int SLANT_ACROSS = 8;
const int SLANT_DOWN = 4;
/* What is left of a colour in the shadow, in sixteenths. */
const int SHADE = 6;

/* Each colour darkened (the palette's nearest), 0 until worked out; the
 * map they were worked out for. */
u8 darkened[256];
u8 darkenedKnown[256];
u16 shadowMap = 0xffff;

/* The pixels a shadow has darkened, round its feet: each marked with the
 * shadow's number, so none is darkened twice. */
const int MARK_WIDTH = 512;
const int MARK_HEIGHT = 192;
u8 marks[MARK_HEIGHT][MARK_WIDTH];
u8 markNumber = 0;

u8 darkenedOf(u8 under)
{
    if (darkenedKnown[under] == 0U) {
        const RGB *colours = gamePalette->data.get();
        const int red = (colours[under].red * SHADE) / 16;
        const int green = (colours[under].green * SHADE) / 16;
        const int blue = (colours[under].blue * SHADE) / 16;
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
        darkened[under] = static_cast<u8>(best);
        darkenedKnown[under] = 1;
    }
    return darkened[under];
}

/* The colour darkened exactly (0xRRGGBB), for the hardware scene. */
std::uint32_t darkenedExactly(u8 under)
{
    const RGB *colours = gamePalette->data.get();
    const auto part = [](int component) { return static_cast<std::uint32_t>((component * SHADE * 255) / (16 * 63)); };
    return part(colours[under].red) << 16 | part(colours[under].green) << 8 | part(colours[under].blue);
}

/* Whether the item casts a shadow. */
bool castsShadow(u16 item)
{
    Npc npc(item);
    return pointerSetting() && npc.isNpc() && npc.isDead() == 0U && (itemFlags[item] & ITEM_INVISIBLE) == 0 &&
           !typeFlags.infos[itemShape[item]].translucent;
}

/* Its feet on the screen, not scrolled: the middle of its footprint,
 * DL_ItemNode::place's place of a point. */
void feetOf(u16 item, int &x, int &y)
{
    Npc npc(item);
    const i16 hx = static_cast<i16>(npc.centreX() >> 1);
    const i16 hy = static_cast<i16>(npc.centreY() >> 1);
    x = hx - hy;
    y = (hx >> 1) + (hy >> 1) - itemZ[item];
}

std::int32_t dword(const u8 *p)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
                                     static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24);
}

}

void paintShadow(u16 item, const ShapeFrame *frame, i16 x, i16 y, bool mirrored, Viewport &view)
{
    if (frame == nullptr || !castsShadow(item)) {
        return;
    }
    if (currentMap != shadowMap) {
        shadowMap = currentMap;
        std::fill(std::begin(darkenedKnown), std::end(darkenedKnown), 0);
    }
    if (++markNumber == 0) {
        for (auto &row : marks) {
            std::fill(std::begin(row), std::end(row), 0);
        }
        markNumber = 1;
    }
    int feetX = 0;
    int feetY = 0;
    feetOf(item, feetX, feetY);
    feetX -= scrollX;
    feetY -= scrollY;
    const int markLeft = feetX - (MARK_WIDTH / 2);
    const int markTop = feetY - (MARK_HEIGHT / 2);
    const Rect clip = view.bounds();
    /* The frame's runs (drawframe.h): a byte to skip, a byte of length,
     * the pixels or, in a frame that fills, one pixel for half the length
     * when it is odd. */
    const auto *bytes = reinterpret_cast<const u8 *>(frame);
    const std::int32_t width = dword(bytes + 4);
    const std::int32_t height = dword(bytes + 8);
    const std::int32_t left = mirrored ? x - (width - 1 - dword(bytes + 0xc)) : x - dword(bytes + 0xc);
    const std::int32_t top = y - dword(bytes + 0x10);
    const bool fills = (dword(bytes) & 1) != 0;
    for (std::int32_t row = 0; row < height; row++) {
        const u8 *entry = bytes + 0x14 + (4 * row);
        const u8 *p = entry + static_cast<std::uint32_t>(dword(entry));
        const int screenY = top + row;
        /* How high the row is above the feet: laid down slanted; the rows
         * at the feet and below stay. */
        const int above = std::max(0, feetY - screenY);
        const int shadowY = above > 0 ? feetY + ((above * SLANT_DOWN) / 16) : screenY;
        const int shift = (above * SLANT_ACROSS) / 16;
        std::int32_t column = 0;
        while (column < width) {
            column += *p++;
            if (column >= width) {
                break;
            }
            std::int32_t length = *p++;
            bool fill = false;
            if (fills) {
                fill = (length & 1) != 0;
                length >>= 1;
            }
            p += fill ? 1 : length;
            for (std::int32_t c = column; c < std::min(column + length, width); c++) {
                const int screenX = mirrored ? left + (width - 1 - c) : left + c;
                const int shadowX = screenX + shift;
                const int mx = shadowX - markLeft;
                const int my = shadowY - markTop;
                if (shadowX < clip.left || shadowX > clip.right || shadowY < clip.top || shadowY > clip.bottom ||
                    mx < 0 || mx >= MARK_WIDTH || my < 0 || my >= MARK_HEIGHT || marks[my][mx] == markNumber) {
                    continue;
                }
                marks[my][mx] = markNumber;
                u8 *pixel = view.address(static_cast<s16>(shadowX), static_cast<s16>(shadowY));
                const u8 under = *pixel;
                *pixel = darkenedOf(under);
                /* With the hardware scene, the darkening exactly (colours.h). */
                keepExact(view, pixel, *pixel, darkenedExactly(under));
            }
            column += length;
        }
    }
}

void shadowBounds(u16 item, i16 top, i16 &right, i16 &bottom)
{
    if (!castsShadow(item)) {
        return;
    }
    int feetX = 0;
    int feetY = 0;
    feetOf(item, feetX, feetY);
    const int above = std::max(0, feetY - top);
    right = static_cast<i16>(std::max<int>(right, right + ((above * SLANT_ACROSS) / 16) + 1));
    bottom = static_cast<i16>(std::max<int>(bottom, feetY + ((above * SLANT_DOWN) / 16) + 1));
}
