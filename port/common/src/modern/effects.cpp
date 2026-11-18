/* The hardware scene's fires, explosions and muzzle flashes. See effects.h. */
#include "effects.h"
#include "gamestate.h"
#include "enhancements.h"
#include "crusys.h"
#include "drawframe.h"
#include "gamepal.h"
#include "hud.h"
#include "camera.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "overlay.h"
#include "screen.h"
#include "profile.h"
#include "shaphand.h"
#include "spmgr.h"
#include "systimer.h"
#include "type.h"
#include "viewport.h"
#include "widesides.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

/* The flames a blast leaves, and how long they burn on and a frame of
 * theirs lasts (in the system timer's ticks, sixtieths of a second). */
const u16 LEFT_FIRE = 0x44a;
const u32 FIRE_LINGERS = 360;
const u32 FIRE_FRAME = 5;
/* A muzzle flash's ticks. */
const u32 FLASH_TICKS = 6;

/* The shapes drawn as effects: the explosions, flashes and puffs of smoke,
 * the jets of flame and the flames a blast leaves (No Remorse's). */
bool isEffect(u16 shape)
{
    return (shape >= 0x31c && shape <= 0x327) || shape == 0x438 || shape == 0x439 || shape == 0x43b ||
           shape == 0x43c || shape == 0x50a || shape == LEFT_FIRE;
}

bool on()
{
    return hardwareSetting() && worldActive != 0U && std::strcmp(PROFILE_NAME, "remorse") == 0;
}

/* The flames left burning, and the flashes: outside the arena, pictures
 * only (a rewind leaves them to go out). */
struct Lingering {
    u16 x;
    u16 y;
    u8 z;
    u16 frame;
    u32 since;
};
Lingering lingering[16];
int lingeringCount = 0;
struct Flash {
    u16 x;
    u16 y;
    u8 z;
    u32 at;
};
Flash flashes[32];
int flashCount = 0;

u32 now()
{
    return systemTimer->time();
}

/* A frame made a picture in the palette's colours, its covered pixels
 * opaque. */
const int MOST_SIDE = 256;
u8 indexes[MOST_SIDE * MOST_SIDE];
u8 covered[MOST_SIDE * MOST_SIDE];
u32 rows[MOST_SIDE];
std::uint32_t picture[MOST_SIDE * MOST_SIDE];

/* Whether the game's frame shows colour at screen pixel x,y: the screen's
 * (screenViewport) or the wide screen's sides'. What stands in front of an
 * effect (the Silencer before a fire) has drawn over it there. */
bool frameShows(int x, int y, u8 colour)
{
    if (x >= 0 && y >= 0 && x < SCREEN_WIDTH && y < SCREEN_HEIGHT) {
        if (screenViewport == nullptr) {
            return true;
        }
        const ViewportPixels screen = screenViewport->pixels();
        if (x < screen.left || x > screen.right || y < screen.top || y > screen.bottom) {
            return true;
        }
        return screen.pixels[screen.rows[y] + static_cast<u32>(x)] == colour;
    }
    int width = 0;
    int height = 0;
    const u8 *sides = wideSidesPixels(&width, &height);
    const int column = x + ((width - SCREEN_WIDTH) / 2);
    const int row = y + ((height - SCREEN_HEIGHT) / 2);
    if (sides == nullptr || column < 0 || row < 0 || column >= width || row >= height) {
        return true;
    }
    return sides[(row * width) + column] == colour;
}

/* The picture of frame with its top left corner at screen pixel left,top;
 * hidden, its pixels the game's frame does not show (in front of it). */
bool makePicture(const u8 *frame, bool mirrored, int left, int top, bool hidden, int *width, int *height)
{
    const s32 w = drawframe::dword(frame + 4);
    const s32 h = drawframe::dword(frame + 8);
    if (w <= 0 || h <= 0 || w > MOST_SIDE || h > MOST_SIDE) {
        return false;
    }
    std::fill_n(covered, w * h, static_cast<u8>(0));
    for (int y = 0; y < h; y++) {
        rows[y] = static_cast<u32>(y * w);
    }
    const ViewportPixels pixels{indexes, rows, 0, 0, w - 1, h - 1};
    /* The origin where the whole frame lands from 0,0. */
    const s32 originX = mirrored ? w - 1 - drawframe::dword(frame + 0xc) : drawframe::dword(frame + 0xc);
    const s32 originY = drawframe::dword(frame + 0x10);
    const auto put = [](u8 &out, u8 in) {
        out = in;
        covered[&out - indexes] = 1;
    };
    if (mirrored) {
        drawframe::draw<true>(pixels, originX, originY, frame, put);
    } else {
        drawframe::draw<false>(pixels, originX, originY, frame, put);
    }
    const RGB *colours = gamePalette->data.get();
    for (int i = 0; i < w * h; i++) {
        if (covered[i] == 0U || (hidden && !frameShows(left + (i % w), top + (i / w), indexes[i]))) {
            picture[i] = 0;
            continue;
        }
        const RGB &c = colours[indexes[i]];
        picture[i] = (static_cast<std::uint32_t>((c.red * 255) / 63) << 24) |
                     (static_cast<std::uint32_t>((c.green * 255) / 63) << 16) |
                     (static_cast<std::uint32_t>((c.blue * 255) / 63) << 8) | 0xffU;
    }
    *width = w;
    *height = h;
    return true;
}

/* An effect's picture at its origin x,y on the screen: a glow of its own
 * shape twice, larger and added on, flickering, then the picture, smooth
 * (added on too when translucent: the jets of flame). */
void drawEffect(u16 shape, u16 frameNumber, float x, float y, bool mirrored, bool translucent)
{
    ShapeFrame *image = cachedShapeFrame(shape, frameNumber);
    if (image == nullptr) {
        return;
    }
    const auto *frame = reinterpret_cast<const u8 *>(image);
    const s32 originX = mirrored ? drawframe::dword(frame + 4) - 1 - drawframe::dword(frame + 0xc)
                                 : drawframe::dword(frame + 0xc);
    const float left = x - static_cast<float>(originX);
    const float top = y - static_cast<float>(drawframe::dword(frame + 0x10));
    /* Only where the game's frame shows it: what is in front hides it, the
     * glows too. The translucent ones, which the game blends with what is
     * under them, are light and go over all. */
    int w = 0;
    int h = 0;
    if (!makePicture(frame, mirrored, static_cast<int>(left + 0.5F), static_cast<int>(top + 0.5F),
                     !translucent, &w, &h)) {
        return;
    }
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    const float cx = left + (fw / 2);
    const float cy = top + (fh / 2);
    const float t = static_cast<float>(now()) / 60.0F;
    static std::uint32_t glow[MOST_SIDE * MOST_SIDE];
    for (const auto [grow, alpha] : {std::pair{1.35F, 70U}, std::pair{1.12F, 90U}}) {
        for (int i = 0; i < w * h; i++) {
            glow[i] = (picture[i] & 0xffffff00U) | ((picture[i] & 0xffU) * alpha / 255);
        }
        /* sin(17 t + 5 grow), by the game's sines (hud.h): 17 radians a
         * second are 974 degrees, 5 radians 286. */
        i16 cosine = 0;
        i16 sine = 0;
        alongAngle(static_cast<i16>(static_cast<int>((t * 974.0F) + (grow * 286.0F)) % 360), 1000, &cosine, &sine);
        const float flicker = 1.0F + (0.04F * static_cast<float>(sine) / 1000.0F);
        const float gw = fw * grow * flicker;
        const float gh = fh * grow * flicker;
        overlayPictureAs(glow, w, h, cx - (gw / 2), cy - (gh / 2), gw, gh, OverlayBlend::ADD, false);
    }
    overlayPictureAs(picture, w, h, left, top, fw, fh, translucent ? OverlayBlend::ADD : OverlayBlend::BLEND, false);
}

/* A soft round blob, white, its alpha fading out from the middle. */
const int BLOB = 32;
std::uint32_t blob[BLOB * BLOB];

void flash(float x, float y, float strength)
{
    for (const auto &[radius, colour] : {std::pair{22.0F, 0xffaa4600U}, std::pair{9.0F, 0xfffae600U}}) {
        for (int py = 0; py < BLOB; py++) {
            for (int px = 0; px < BLOB; px++) {
                const float dx = ((static_cast<float>(px) + 0.5F) / (BLOB / 2.0F)) - 1;
                const float dy = ((static_cast<float>(py) + 0.5F) / (BLOB / 2.0F)) - 1;
                const float k = std::clamp(1.0F - std::sqrt((dx * dx) + (dy * dy)), 0.0F, 1.0F);
                blob[(py * BLOB) + px] = colour | static_cast<std::uint32_t>(255.0F * k * k * strength);
            }
        }
        const float side = radius * (0.7F + (0.3F * strength)) * 2;
        overlayPictureAs(blob, BLOB, BLOB, x - (side / 2), y - (side / 2), side, side, OverlayBlend::ADD, false);
    }
}

}

void effectsShot(u16 x, u16 y, u8 z)
{
    if (!on()) {
        return;
    }
    if (flashCount == static_cast<int>(std::size(flashes))) {
        std::copy(flashes + 1, flashes + flashCount, flashes);
        flashCount--;
    }
    flashes[flashCount++] = {x, y, z, now()};
}

void effectsGone(u16 item)
{
    if (!on() || itemShape[item] != LEFT_FIRE || (itemFlags[item] & ITEM_CONTAINED) != 0) {
        return;
    }
    if (lingeringCount == static_cast<int>(std::size(lingering))) {
        return;
    }
    lingering[lingeringCount++] = {itemX[item], itemY[item], itemZ[item], Item(item).frame(), now()};
}

void drawEffects()
{
    if (!on()) {
        lingeringCount = 0;
        flashCount = 0;
        return;
    }
    const u32 time = now();
    /* The items: the jets of flame and the flames left. */
    u16 middleX;
    u16 middleY;
    screenToMap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, &middleX, &middleY);
    const int column = middleX >> CHUNK_SHIFT;
    const int row = middleY >> CHUNK_SHIFT;
    for (int c = std::max(0, column - 3); c <= std::min(CHUNKS - 1, column + 3); c++) {
        for (int r = std::max(0, row - 3); r <= std::min(CHUNKS - 1, row + 3); r++) {
            for (u16 item = mapChunks[c][r]; item != 0U; item = itemNext[item]) {
                if (item < FIRST_ITEM || !isEffect(itemShape[item]) ||
                    (itemFlags[item] & (ITEM_CONTAINED | ITEM_INVISIBLE)) != 0) {
                    continue;
                }
                float x;
                float y;
                mapToOverlay(itemX[item], itemY[item], itemZ[item], &x, &y);
                drawEffect(itemShape[item], Item(item).frame(), x, y, (itemFlags[item] & ITEM_FLIPPED) != 0,
                           typeFlags.infos[itemShape[item]].translucent != 0U);
            }
        }
    }
    /* The sprites: explosions, flashes, smoke. */
    if (sprites != nullptr) {
        for (u16 i = 0; i < spriteCount; i++) {
            const SpriteEntry &sprite = sprites[i];
            if (sprite.shape == 0U || (sprite.flags & SPRITE_UNSEEN) != 0 || !isEffect(sprite.shape)) {
                continue;
            }
            float x;
            float y;
            mapToOverlay(sprite.x, sprite.y, sprite.z, &x, &y);
            drawEffect(sprite.shape, sprite.frame, x, y, false, typeFlags.infos[sprite.shape].translucent != 0U);
        }
    }
    /* The flames left, burning on, their frames going round. */
    u16 frames = 1;
    u16 width = 0;
    u16 height = 0;
    shapeCache->shapeHeader(LEFT_FIRE, &width, &height, &frames);
    int kept = 0;
    for (int i = 0; i < lingeringCount; i++) {
        Lingering &fire = lingering[i];
        const u32 since = time - fire.since;
        if (since > FIRE_LINGERS) {
            continue;
        }
        float x;
        float y;
        mapToOverlay(fire.x, fire.y, fire.z, &x, &y);
        const auto frame = static_cast<u16>((fire.frame + (since / FIRE_FRAME)) % std::max<u16>(1, frames));
        drawEffect(LEFT_FIRE, frame, x, y, false, typeFlags.infos[LEFT_FIRE].translucent != 0U);
        lingering[kept++] = fire;
    }
    lingeringCount = kept;
    /* The muzzle flashes, fading. */
    kept = 0;
    for (int i = 0; i < flashCount; i++) {
        const u32 since = time - flashes[i].at;
        if (since >= FLASH_TICKS) {
            continue;
        }
        float x;
        float y;
        mapToOverlay(flashes[i].x, flashes[i].y, flashes[i].z, &x, &y);
        flash(x, y, 1.0F - (static_cast<float>(since) / FLASH_TICKS));
        flashes[kept++] = flashes[i];
    }
    flashCount = kept;
}
