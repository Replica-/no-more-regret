/* Lighting. See lighting.h. */
#include "lighting.h"
#include "gamestate.h"
#include "control.h"
#include "enhancements.h"
#include "crusys.h"
#include "gamepal.h"
#include "hud.h"
#include "camera.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "shaphand.h"
#include "spmgr.h"
#include "overlay.h"
#include "screen.h"
#include "shape.h"
#include "status.h"
#include "type.h"
#include "viewport.h"
#include "widesides.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

/* The light, a pixel of the frame each: the screen and the wide screen's
 * frame round it (WIDE_FRAME_MOST x TALL_FRAME_MOST at most). The shade
 * where nothing lights (of 255: about 80 % of the art); a picture's light
 * (GLOWING of its pixels glowing give the full light; fewer than
 * MOST_STRAY are stray pixels of the art, no light) and its reach in
 * pixels, NEAREST for the faintest and FARTHEST for the full one, half as
 * far up and down (the floor seen at its angle, as the isometric view has
 * it). */
const int MOST_PIXELS = WIDE_FRAME_MOST * TALL_FRAME_MOST;
const float SHADE = 56;
const int GLOWING = 24;
const int MOST_STRAY = 6;
const float NEAREST = 14;
const float FARTHEST = 54;

/* The palette's light colours: 1-7 (yellows to red, which the palette
 * cycles), 8-14 (the translucent glows) and the lone bright entries
 * (lamps, LEDs, screens); 65, 67 and 69, the dim halves of the blue, red
 * and green pairs, shade railings and pipes and are left out. */
constexpr std::array<u8, 256> LIGHT_COLOURS = [] {
    std::array<u8, 256> table{};
    for (int i = 1; i <= 14; i++) {
        table[static_cast<std::size_t>(i)] = 1;
    }
    for (const int i : {15, 63, 64, 66, 68, 70, 71, 95, 160, 175, 223, 251, 252, 254, 255}) {
        table[static_cast<std::size_t>(i)] = 1;
    }
    return table;
}();

bool &lightingOn = gameSetting<bool>(false);

/* Each pixel's light (0-255) and its colour (0xRRGGBB, its brightest
 * source's); the frame's size and its columns left of the screen and rows
 * above it. */
u8 light[MOST_PIXELS];
std::uint32_t tint[MOST_PIXELS];
int frameWidth = SCREEN_WIDTH;
int frameHeight = SCREEN_HEIGHT;
int frameLeft = 0;
int frameTop = 0;
/* The pictures laid over: the shade (blended), or with the hardware
 * scene the light the screen is multiplied by and the glow added to it. */
std::uint32_t multiplied[MOST_PIXELS];
std::uint32_t added[MOST_PIXELS];
/* How much of the light's colour tints the light (of 256), and how much of
 * it is added as a glow (of 255). */
const int TINTED = 77;
const int GLOW = 46;

/* The status display's parts on the frame, which are not the world's:
 * where the screen has them, or moved down to the frame's bottom
 * (widesides.h); count of them. */
Rect statusBoxes[STATUS_PARTS];
int statusCount = 0;

void findStatus()
{
    statusCount = 0;
    if (statusShown == 0U) {
        return;
    }
    const int down = wideStatusDown();
    for (const StatusPart &part : statusParts) {
        statusBoxes[statusCount++] = Rect(static_cast<i16>(part.area.left + frameLeft),
                                          static_cast<i16>(part.area.top + down + frameTop),
                                          static_cast<i16>(part.area.right + frameLeft),
                                          static_cast<i16>(part.area.bottom + down + frameTop));
    }
}

/* A frame's glow: how many of its pixels are of the light colours, and
 * their middle from its origin; worked out once a frame (drawn alone) and
 * kept. */
struct Glow {
    u32 key;
    i16 count;
    i16 x;
    i16 y;
    /* The glowing pixels' colour on average, 0xRRGGBB. */
    std::uint32_t colour;
};
const int GLOWS = 4096;
Glow glowCache[GLOWS];
const int SCRATCH = 512;
u8 scratch[SCRATCH * SCRATCH];

const Glow &glowOf(u16 shape, u16 frame)
{
    const u32 key = (static_cast<u32>(shape) << 16) | frame;
    u32 slot = (key * 2654435761U) % GLOWS;
    for (int probe = 0; probe < 8; probe++) {
        const Glow &found = glowCache[(slot + probe) % GLOWS];
        if (found.key == key + 1) {
            return found;
        }
    }
    /* Not kept: drawn alone and counted, in its first slot. */
    Glow &glow = glowCache[slot];
    glow = Glow{key + 1, 0, 0, 0, 0};
    ShapeFrame *image = cachedShapeFrame(shape, frame);
    if (image == nullptr) {
        return glow;
    }
    /* Drawn with its top left corner at the scratch's, and counted over
     * its box. */
    const int width = std::clamp<int>(image->width, 0, SCRATCH);
    const int height = std::clamp<int>(image->height, 0, SCRATCH);
    for (int y = 0; y < height; y++) {
        std::fill_n(&scratch[y * SCRATCH], width, static_cast<u8>(TRANSPARENT));
    }
    Viewport view(VIEWPORT_LARGE, 0, 0, SCRATCH - 1, SCRATCH - 1, scratch);
    view.drawShape(static_cast<i16>(image->xOffset), static_cast<i16>(image->yOffset), image);
    std::int32_t sumX = 0;
    std::int32_t sumY = 0;
    std::int32_t sums[3] = {0, 0, 0};
    const RGB *palette = gamePalette->data.get();
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const u8 pixel = scratch[(y * SCRATCH) + x];
            if (pixel != TRANSPARENT && LIGHT_COLOURS[pixel] != 0U) {
                glow.count++;
                sums[0] += palette[pixel].red;
                sums[1] += palette[pixel].green;
                sums[2] += palette[pixel].blue;
                sumX += x - image->xOffset;
                sumY += y - image->yOffset;
            }
        }
    }
    if (glow.count != 0) {
        glow.x = static_cast<i16>(sumX / glow.count);
        glow.y = static_cast<i16>(sumY / glow.count);
        for (int k = 0; k < 3; k++) {
            const auto average = static_cast<std::uint32_t>(((sums[k] / glow.count) * 255) / 63);
            glow.colour |= average << (16 - (8 * k));
        }
    }
    return glow;
}

/* A light of strength and colour round screen point x,y: as far as the
 * strength's reach, fading smoothly (the square of what is left of the
 * reach squared), each pixel the brightest light's. */
void lightAt(float x, float y, float strength, std::uint32_t colour, float reach)
{
    strength = std::min(1.0F, strength);
    const float centreX = x + static_cast<float>(frameLeft);
    const float centreY = y + static_cast<float>(frameTop);
    const float across = reach;
    const float down = reach / 2;
    const int left = std::max(0, static_cast<int>(centreX - across));
    const int right = std::min(frameWidth - 1, static_cast<int>(centreX + across));
    const int top = std::max(0, static_cast<int>(centreY - down));
    const int bottom = std::min(frameHeight - 1, static_cast<int>(centreY + down));
    for (int py = top; py <= bottom; py++) {
        const float dy = (static_cast<float>(py) + 0.5F - centreY) / down;
        for (int px = left; px <= right; px++) {
            const float dx = (static_cast<float>(px) + 0.5F - centreX) / across;
            const float left2 = 1 - ((dx * dx) + (dy * dy));
            if (left2 <= 0) {
                continue;
            }
            const auto value = static_cast<u8>(strength * left2 * left2 * 255);
            const std::size_t at = (static_cast<std::size_t>(py) * frameWidth) + px;
            if (value > light[at]) {
                light[at] = value;
                tint[at] = colour;
            }
        }
    }
}

/* A picture's light of count glowing pixels. */
void glowAt(float x, float y, i16 count, std::uint32_t colour)
{
    const float strength = static_cast<float>(count) / GLOWING;
    lightAt(x, y, strength, colour, NEAREST + ((FARTHEST - NEAREST) * std::min(1.0F, strength)));
}

/* What glows on the screen: the items but the floors and the NPCs, and the
 * sprites (shots, flashes, explosions), whose pictures have enough of the
 * light colours. */
void glows()
{
    std::fill_n(light, static_cast<std::size_t>(frameWidth) * frameHeight, static_cast<u8>(0));
    u16 middleX;
    u16 middleY;
    screenToMap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, &middleX, &middleY);
    const int column = middleX >> CHUNK_SHIFT;
    const int row = middleY >> CHUNK_SHIFT;
    for (int c = std::max(0, column - 3); c <= std::min(CHUNKS - 1, column + 3); c++) {
        for (int r = std::max(0, row - 3); r <= std::min(CHUNKS - 1, row + 3); r++) {
            for (u16 item = mapChunks[c][r]; item != 0U; item = itemNext[item]) {
                if (item < FIRST_ITEM ||
                    (itemFlags[item] & (ITEM_CONTAINED | ITEM_ETHEREAL | ITEM_INVISIBLE)) != 0) {
                    continue;
                }
                const ShapeInfo &info = typeFlags.infos[itemShape[item]];
                if (info.land != 0U || info.editor != 0U) {
                    continue;
                }
                float x;
                float y;
                mapToOverlay(itemX[item], itemY[item], itemZ[item], &x, &y);
                if (x < static_cast<float>(-frameLeft - 64) ||
                    x > static_cast<float>(frameWidth - frameLeft + 64) || y < static_cast<float>(-frameTop - 64) ||
                    y > static_cast<float>(frameHeight - frameTop + 64)) {
                    continue;
                }
                const Glow &glow = glowOf(itemShape[item], Item(item).frame());
                if (glow.count < MOST_STRAY) {
                    continue;
                }
                const bool flipped = (itemFlags[item] & ITEM_FLIPPED) != 0;
                glowAt(x + static_cast<float>(flipped ? -glow.x : glow.x), y + glow.y, glow.count, glow.colour);
            }
        }
    }
    if (sprites == nullptr) {
        return;
    }
    for (u16 i = 0; i < spriteCount; i++) {
        const SpriteEntry &sprite = sprites[i];
        if (sprite.shape == 0U || (sprite.flags & SPRITE_UNSEEN) != 0) {
            continue;
        }
        const Glow &glow = glowOf(sprite.shape, sprite.frame);
        if (glow.count < MOST_STRAY) {
            continue;
        }
        float x;
        float y;
        mapToOverlay(sprite.x, sprite.y, sprite.z, &x, &y);
        glowAt(x + glow.x, y + glow.y, glow.count, glow.colour);
    }
}

/* The pictures over the frame: with the hardware scene (enhancements.h)
 * the screen multiplied by the light in its colour (the shade where
 * nothing lights, the art's own where the light is full) and the light's
 * colour added over it as a glow, by the graphics card; else the shade,
 * blended. The status display's parts left as they are. */
void layLight()
{
    const bool hardware = hardwareSetting();
    const auto dark = static_cast<int>(255 - SHADE);
    const std::uint32_t unlit = hardware ? (static_cast<std::uint32_t>(dark) * 0x01010100U) | 0xffU
                                         : static_cast<std::uint32_t>(SHADE);
    const std::size_t pixels = static_cast<std::size_t>(frameWidth) * frameHeight;
    for (std::size_t at = 0; at < pixels; at++) {
        const int lit = light[at];
        if (lit == 0) {
            multiplied[at] = unlit;
            added[at] = 0;
            continue;
        }
        if (!hardware) {
            multiplied[at] = static_cast<std::uint32_t>(SHADE * static_cast<float>(255 - lit) / 255);
            continue;
        }
        const int level = dark + (((255 - dark) * lit) / 255);
        const std::uint32_t colour = tint[at];
        std::uint32_t times = 0xffU;
        for (int k = 0; k < 3; k++) {
            const auto component = static_cast<int>((colour >> (16 - (8 * k))) & 0xffU);
            const int mixed = 255 - ((lit * TINTED * (255 - component)) / (255 * 256));
            times |= static_cast<std::uint32_t>((level * mixed) / 255) << (24 - (8 * k));
        }
        multiplied[at] = times;
        added[at] = (colour << 8) | static_cast<std::uint32_t>((lit * GLOW) / 255);
    }
    for (int i = 0; i < statusCount; i++) {
        const Rect &box = statusBoxes[i];
        for (int y = std::max<int>(0, box.top); y <= std::min(frameHeight - 1, static_cast<int>(box.bottom)); y++) {
            for (int x = std::max<int>(0, box.left); x <= std::min(frameWidth - 1, static_cast<int>(box.right)); x++) {
                const std::size_t at = (static_cast<std::size_t>(y) * frameWidth) + x;
                multiplied[at] = hardware ? 0xffffffffU : 0U;
                added[at] = 0;
            }
        }
    }
    const auto left = static_cast<float>(-frameLeft);
    const auto top = static_cast<float>(-frameTop);
    const auto width = static_cast<float>(frameWidth);
    const auto height = static_cast<float>(frameHeight);
    if (hardware) {
        overlayPictureAs(multiplied, frameWidth, frameHeight, left, top, width, height, OverlayBlend::MULTIPLY, true);
        overlayPictureAs(added, frameWidth, frameHeight, left, top, width, height, OverlayBlend::ADD, true);
    } else {
        overlayPictureAs(multiplied, frameWidth, frameHeight, left, top, width, height, OverlayBlend::BLEND, true);
    }
}

}

bool lightingSetting()
{
    return lightingOn;
}

void setLightingSetting(bool on)
{
    lightingOn = on;
}

void drawLighting()
{
    if (!lightingOn || worldActive == 0U) {
        return;
    }
    frameWidth = std::min<int>(screenWideWidth(), WIDE_FRAME_MOST);
    frameHeight = std::min<int>(screenWideHeight(), TALL_FRAME_MOST);
    frameLeft = (frameWidth - SCREEN_WIDTH) / 2;
    frameTop = (frameHeight - SCREEN_HEIGHT) / 2;
    findStatus();
    glows();
    layLight();
}
