/* Lighting. See lighting.h. */
#include "lighting.h"
#include "gamestate.h"
#include "control.h"
#include "crusys.h"
#include "gamepal.h"
#include "hud.h"
#include "camera.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "shaphand.h"
#include "spmgr.h"
#include "npc.h"
#include "overlay.h"
#include "screen.h"
#include "shape.h"
#include "status.h"
#include "type.h"
#include "viewport.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

/* The map of the light: cells of CELL pixels, over the screen and the wide
 * screen's sides (SIDE cells each way, which only the light spreading from
 * the screen reaches). */
const int CELL = 8;
const int SIDE = 27;
const int COLUMNS = (SCREEN_WIDTH / CELL) + (2 * SIDE);
const int ROWS = SCREEN_HEIGHT / CELL;
/* The shade where nothing lights (of 255: about 80 % of the art); a
 * picture's light (GLOWING of its pixels glowing give the full light;
 * fewer than MOST_STRAY are stray pixels of the art, no light); the light
 * kept a cell further on (straight, and diagonally); the Silencer's: its
 * strength and reach in pixels. */
const float SHADE = 56;
const int GLOWING = 24;
const int MOST_STRAY = 6;
const float FADE = 0.93F;
const float FADE_DIAGONAL = 0.86F;
const float OWN_LIGHT = 0.6F;
const float OWN_REACH = 90;

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

float light[ROWS][COLUMNS];
std::uint32_t shade[ROWS * COLUMNS];

/* The status display's parts, which are not the world's. */
bool overStatus(int x, int y)
{
    if (statusShown == 0U) {
        return false;
    }
    for (const StatusPart &part : statusParts) {
        if (x >= part.area.left - CELL && x <= part.area.right && y >= part.area.top - CELL && y <= part.area.bottom) {
            return true;
        }
    }
    return false;
}

/* A frame's glow: how many of its pixels are of the light colours, and
 * their middle from its origin; worked out once a frame (drawn alone) and
 * kept. */
struct Glow {
    u32 key;
    i16 count;
    i16 x;
    i16 y;
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
    glow = Glow{key + 1, 0, 0, 0};
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
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const u8 pixel = scratch[(y * SCRATCH) + x];
            if (pixel != TRANSPARENT && LIGHT_COLOURS[pixel] != 0U) {
                glow.count++;
                sumX += x - image->xOffset;
                sumY += y - image->yOffset;
            }
        }
    }
    if (glow.count != 0) {
        glow.x = static_cast<i16>(sumX / glow.count);
        glow.y = static_cast<i16>(sumY / glow.count);
    }
    return glow;
}

/* A light of strength at screen point x,y. */
void lightAt(float x, float y, float strength)
{
    const int column = (static_cast<int>(x) / CELL) + SIDE;
    const int row = static_cast<int>(y) / CELL;
    if (column < 0 || column >= COLUMNS || row < 0 || row >= ROWS) {
        return;
    }
    light[row][column] = std::max(light[row][column], std::min(1.0F, strength));
}

/* What glows on the screen: the items but the floors and the NPCs, and the
 * sprites (shots, flashes, explosions), whose pictures have enough of the
 * light colours. */
void glows()
{
    for (auto &row : light) {
        std::fill(std::begin(row), std::end(row), 0.0F);
    }
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
                if (x < -SIDE * CELL || x > SCREEN_WIDTH + (SIDE * CELL) || y < -64 || y > SCREEN_HEIGHT + 64) {
                    continue;
                }
                const Glow &glow = glowOf(itemShape[item], Item(item).frame());
                if (glow.count < MOST_STRAY) {
                    continue;
                }
                const bool flipped = (itemFlags[item] & ITEM_FLIPPED) != 0;
                lightAt(x + static_cast<float>(flipped ? -glow.x : glow.x), y + glow.y,
                        static_cast<float>(glow.count) / GLOWING);
            }
        }
    }
    if (sprites == nullptr) {
        return;
    }
    for (u16 i = 0; i < spriteCount; i++) {
        const SpriteEntry &sprite = sprites[i];
        if (sprite.shape == 0U) {
            continue;
        }
        const Glow &glow = glowOf(sprite.shape, sprite.frame);
        if (glow.count < MOST_STRAY) {
            continue;
        }
        float x;
        float y;
        mapToOverlay(sprite.x, sprite.y, sprite.z, &x, &y);
        lightAt(x + glow.x, y + glow.y, static_cast<float>(glow.count) / GLOWING);
    }
}

/* The light spread from cell to cell, fading: down and right, then up and
 * left, each cell the brightest of its own and its neighbours' faded. */
void spread()
{
    for (int row = 0; row < ROWS; row++) {
        for (int column = 0; column < COLUMNS; column++) {
            float &here = light[row][column];
            if (column > 0) {
                here = std::max(here, light[row][column - 1] * FADE);
            }
            if (row > 0) {
                here = std::max(here, light[row - 1][column] * FADE);
                if (column > 0) {
                    here = std::max(here, light[row - 1][column - 1] * FADE_DIAGONAL);
                }
                if (column < COLUMNS - 1) {
                    here = std::max(here, light[row - 1][column + 1] * FADE_DIAGONAL);
                }
            }
        }
    }
    for (int row = ROWS - 1; row >= 0; row--) {
        for (int column = COLUMNS - 1; column >= 0; column--) {
            float &here = light[row][column];
            if (column < COLUMNS - 1) {
                here = std::max(here, light[row][column + 1] * FADE);
            }
            if (row < ROWS - 1) {
                here = std::max(here, light[row + 1][column] * FADE);
                if (column < COLUMNS - 1) {
                    here = std::max(here, light[row + 1][column + 1] * FADE_DIAGONAL);
                }
                if (column > 0) {
                    here = std::max(here, light[row + 1][column - 1] * FADE_DIAGONAL);
                }
            }
        }
    }
}

/* The soft light round the controlled NPC. */
void ownLight()
{
    Npc npc(controlledNpc.id);
    if (!npc.isValid()) {
        return;
    }
    float x;
    float y;
    mapToOverlay(npc.centreX(), npc.centreY(), static_cast<i16>(npc.z() + 20), &x, &y);
    for (int row = 0; row < ROWS; row++) {
        for (int column = 0; column < COLUMNS; column++) {
            const float dx = static_cast<float>(((column - SIDE) * CELL) + (CELL / 2)) - x;
            const float dy = static_cast<float>((row * CELL) + (CELL / 2)) - y;
            /* The reach squared, without a square root. */
            const float near = 1 - (((dx * dx) + (dy * dy)) / (OWN_REACH * OWN_REACH));
            if (near > 0) {
                light[row][column] = std::max(light[row][column], OWN_LIGHT * near);
            }
        }
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
    glows();
    spread();
    ownLight();
    for (int row = 0; row < ROWS; row++) {
        for (int column = 0; column < COLUMNS; column++) {
            const int x = (column - SIDE) * CELL;
            const int y = row * CELL;
            float alpha = SHADE * (1 - std::min(1.0F, light[row][column]));
            if (x >= 0 && x < SCREEN_WIDTH && overStatus(x, y)) {
                alpha = 0;
            }
            shade[(row * COLUMNS) + column] = static_cast<std::uint32_t>(alpha);
        }
    }
    overlayPicture(shade, COLUMNS, ROWS, static_cast<float>(-SIDE * CELL), 0, static_cast<float>(COLUMNS * CELL),
                   static_cast<float>(SCREEN_HEIGHT));
}
