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
#include "npc.h"
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

/* The map of the light: cells of CELL pixels, over the screen and the
 * wide screen's frame round it (SIDE cells each side, ABOVE above and
 * below, the most the frame has). */
const int CELL = 8;
const int SIDE = (WIDE_FRAME_MOST - SCREEN_WIDTH) / 2 / CELL;
const int ABOVE = (TALL_FRAME_MOST - SCREEN_HEIGHT) / 2 / CELL;
const int COLUMNS = (SCREEN_WIDTH / CELL) + (2 * SIDE);
const int ROWS = (SCREEN_HEIGHT / CELL) + (2 * ABOVE);
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
/* Each cell's light's colour (0xRRGGBB): its brightest source's. */
std::uint32_t tint[ROWS][COLUMNS];
std::uint32_t shade[ROWS * COLUMNS];
/* The hardware scene's: the light the screen is multiplied by, and the
 * glow added to it. */
std::uint32_t multiplied[ROWS * COLUMNS];
std::uint32_t added[ROWS * COLUMNS];
/* The Silencer's own light's colour, a warm white; how much of the light's
 * colour tints the light, and how much of it is added as a glow. */
const std::uint32_t OWN_TINT = 0xfff2e0;
const float TINTED = 0.3F;
const float GLOW = 0.18F;

/* The status display's parts, which are not the world's: where the screen
 * has them, or moved down to the frame's bottom (widesides.h). */
bool overStatus(int x, int y)
{
    if (statusShown == 0U) {
        return false;
    }
    const int down = wideStatusDown();
    for (const StatusPart &part : statusParts) {
        if (x >= part.area.left - CELL && x <= part.area.right && y >= part.area.top + down - CELL &&
            y <= part.area.bottom + down) {
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

/* A light of strength and colour at screen point x,y. */
void lightAt(float x, float y, float strength, std::uint32_t colour)
{
    if (x < static_cast<float>(-SIDE * CELL) || y < static_cast<float>(-ABOVE * CELL)) {
        return;
    }
    /* From the map's corner, not below 0: whole cells. */
    const int column = static_cast<int>(x + static_cast<float>(SIDE * CELL)) / CELL;
    const int row = static_cast<int>(y + static_cast<float>(ABOVE * CELL)) / CELL;
    if (column < 0 || column >= COLUMNS || row < 0 || row >= ROWS) {
        return;
    }
    if (std::min(1.0F, strength) > light[row][column]) {
        light[row][column] = std::min(1.0F, strength);
        tint[row][column] = colour;
    }
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
                if (x < -SIDE * CELL || x > SCREEN_WIDTH + (SIDE * CELL) || y < -(ABOVE * CELL) - 64 ||
                    y > SCREEN_HEIGHT + (ABOVE * CELL) + 64) {
                    continue;
                }
                const Glow &glow = glowOf(itemShape[item], Item(item).frame());
                if (glow.count < MOST_STRAY) {
                    continue;
                }
                const bool flipped = (itemFlags[item] & ITEM_FLIPPED) != 0;
                lightAt(x + static_cast<float>(flipped ? -glow.x : glow.x), y + glow.y,
                        static_cast<float>(glow.count) / GLOWING, glow.colour);
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
        lightAt(x + glow.x, y + glow.y, static_cast<float>(glow.count) / GLOWING, glow.colour);
    }
}

/* A cell's light its neighbour's faded, and its colour, when brighter. */
void take(int row, int column, int fromRow, int fromColumn, float fade)
{
    const float faded = light[fromRow][fromColumn] * fade;
    if (faded > light[row][column]) {
        light[row][column] = faded;
        tint[row][column] = tint[fromRow][fromColumn];
    }
}

/* The light spread from cell to cell, fading: down and right, then up and
 * left, each cell the brightest of its own and its neighbours' faded. */
void spread()
{
    for (int row = 0; row < ROWS; row++) {
        for (int column = 0; column < COLUMNS; column++) {
            if (column > 0) {
                take(row, column, row, column - 1, FADE);
            }
            if (row > 0) {
                take(row, column, row - 1, column, FADE);
                if (column > 0) {
                    take(row, column, row - 1, column - 1, FADE_DIAGONAL);
                }
                if (column < COLUMNS - 1) {
                    take(row, column, row - 1, column + 1, FADE_DIAGONAL);
                }
            }
        }
    }
    for (int row = ROWS - 1; row >= 0; row--) {
        for (int column = COLUMNS - 1; column >= 0; column--) {
            if (column < COLUMNS - 1) {
                take(row, column, row, column + 1, FADE);
            }
            if (row < ROWS - 1) {
                take(row, column, row + 1, column, FADE);
                if (column < COLUMNS - 1) {
                    take(row, column, row + 1, column + 1, FADE_DIAGONAL);
                }
                if (column > 0) {
                    take(row, column, row + 1, column - 1, FADE_DIAGONAL);
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
            const float dy = static_cast<float>(((row - ABOVE) * CELL) + (CELL / 2)) - y;
            /* The reach squared, without a square root. */
            const float near = 1 - (((dx * dx) + (dy * dy)) / (OWN_REACH * OWN_REACH));
            if (near > 0 && OWN_LIGHT * near > light[row][column]) {
                light[row][column] = OWN_LIGHT * near;
                tint[row][column] = OWN_TINT;
            }
        }
    }
}

}

namespace {

/* The hardware scene's lighting (enhancements.h): the screen multiplied by
 * the light, in its colour (the shade where nothing lights, the art's own
 * where the light is full), and the light's colour added over it as a glow,
 * by the graphics card; the status display's parts left as they are. */
void hardwareLight()
{
    const float dark = 1 - (SHADE / 255);
    for (int row = 0; row < ROWS; row++) {
        for (int column = 0; column < COLUMNS; column++) {
            const int x = (column - SIDE) * CELL;
            const int y = (row - ABOVE) * CELL;
            const std::size_t at = static_cast<std::size_t>(row * COLUMNS) + column;
            if (x >= 0 && x < SCREEN_WIDTH && overStatus(x, y)) {
                multiplied[at] = 0xffffffffU;
                added[at] = 0;
                continue;
            }
            const float lit = std::min(1.0F, light[row][column]);
            const float level = dark + ((1 - dark) * lit);
            std::uint32_t times = 0xffU;
            for (int k = 0; k < 3; k++) {
                const auto component = static_cast<float>((tint[row][column] >> (16 - (8 * k))) & 0xffU) / 255;
                const float factor = level * (1 - (lit * TINTED) + (lit * TINTED * component));
                times |= static_cast<std::uint32_t>(std::clamp(factor, 0.0F, 1.0F) * 255) << (24 - (8 * k));
            }
            multiplied[at] = times;
            added[at] = (tint[row][column] << 8) | static_cast<std::uint32_t>(lit * GLOW * 255);
        }
    }
    const auto left = static_cast<float>(-SIDE * CELL);
    const auto top = static_cast<float>(-ABOVE * CELL);
    overlayPictureAs(multiplied, COLUMNS, ROWS, left, top, static_cast<float>(COLUMNS * CELL),
                     static_cast<float>(ROWS * CELL), OverlayBlend::MULTIPLY, false);
    overlayPictureAs(added, COLUMNS, ROWS, left, top, static_cast<float>(COLUMNS * CELL),
                     static_cast<float>(ROWS * CELL), OverlayBlend::ADD, false);
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
    if (hardwareSetting()) {
        hardwareLight();
        return;
    }
    for (int row = 0; row < ROWS; row++) {
        for (int column = 0; column < COLUMNS; column++) {
            const int x = (column - SIDE) * CELL;
            const int y = (row - ABOVE) * CELL;
            float alpha = SHADE * (1 - std::min(1.0F, light[row][column]));
            if (x >= 0 && x < SCREEN_WIDTH && overStatus(x, y)) {
                alpha = 0;
            }
            shade[(row * COLUMNS) + column] = static_cast<std::uint32_t>(alpha);
        }
    }
    overlayPicture(shade, COLUMNS, ROWS, static_cast<float>(-SIDE * CELL), static_cast<float>(-ABOVE * CELL),
                   static_cast<float>(COLUMNS * CELL), static_cast<float>(ROWS * CELL));
}
