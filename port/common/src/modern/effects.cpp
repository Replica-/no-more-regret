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

/* The fires: the jets of flame and the flames a blast leaves. */
bool isFire(u16 shape)
{
    return shape == 0x438 || shape == 0x439 || shape == 0x43b || shape == 0x43c || shape == 0x50a || shape == LEFT_FIRE;
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
    u16 key;
};
u16 nextLingeringKey = 0;
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


/* What was under a fire the game drew: the frame's box on the map's screen
 * (the screen's pixels with the scroll), what was there before the frame and
 * after it, which pixels of the box were drawn (known), and which of the
 * frame's pixels show now (seen: 1 shown, 2 hidden by what is in front). The
 * screen's and the wide screen's sides' apart: the sides draw it again at
 * each frame, a part at a time. */
const int COVER_SIDE = 192;
const u32 COVER_KEEP = 60;
struct Cover {
    u16 item;
    bool sides;
    bool used;
    u16 frame;
    int left;
    int top;
    int width;
    int height;
    u32 at;
    u8 under[COVER_SIDE * COVER_SIDE];
    u8 drawn[COVER_SIDE * COVER_SIDE];
    u8 known[COVER_SIDE * COVER_SIDE];
    u8 seen[COVER_SIDE * COVER_SIDE];
};
Cover covers[24];
Cover *drawing = nullptr;

/* The frame's box at the origin x,y: its top left corner and size. */
bool frameBox(u16 shape, u16 frame, bool mirrored, int x, int y, int *left, int *top, int *width, int *height)
{
    ShapeFrame *image = cachedShapeFrame(shape, frame);
    if (image == nullptr) {
        return false;
    }
    const auto *bytes = reinterpret_cast<const u8 *>(image);
    const s32 w = drawframe::dword(bytes + 4);
    const s32 h = drawframe::dword(bytes + 8);
    const s32 originX = mirrored ? w - 1 - drawframe::dword(bytes + 0xc) : drawframe::dword(bytes + 0xc);
    *left = x - originX;
    *top = y - drawframe::dword(bytes + 0x10);
    *width = w;
    *height = h;
    return w > 0 && h > 0 && w <= COVER_SIDE && h <= COVER_SIDE;
}

Cover *coverOf(u16 item, bool sides)
{
    Cover *unused = nullptr;
    Cover *oldest = &covers[0];
    for (Cover &cover : covers) {
        if (cover.used && cover.item == item && cover.sides == sides) {
            return &cover;
        }
        if (!cover.used && unused == nullptr) {
            unused = &cover;
        }
        if (cover.at < oldest->at) {
            oldest = &cover;
        }
    }
    Cover *cover = unused != nullptr ? unused : oldest;
    cover->used = true;
    cover->item = item;
    cover->sides = sides;
    cover->width = 0;
    return cover;
}

/* The pixels of view's within its bounds in the cover's box (its top left
 * corner at viewLeft,viewTop on view), into out; marked known, when mark. */
void copyBox(Viewport &view, Cover &cover, int viewLeft, int viewTop, u8 *out, bool mark)
{
    const ViewportPixels pixels = view.pixels();
    for (int row = 0; row < cover.height; row++) {
        const int y = viewTop + row;
        if (y < pixels.top || y > pixels.bottom) {
            continue;
        }
        for (int column = 0; column < cover.width; column++) {
            const int x = viewLeft + column;
            if (x < pixels.left || x > pixels.right) {
                continue;
            }
            const int i = (row * cover.width) + column;
            out[i] = pixels.pixels[pixels.rows[y] + static_cast<u32>(x)];
            if (mark) {
                cover.known[i] = 1;
            }
        }
    }
}

/* A random number from 0 to 1, the scene's own (not the game's). */
std::uint32_t seed = 0x9e3779b9U;
float random01()
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<float>(seed & 0xffffffU) / static_cast<float>(0x1000000);
}
float between(float low, float high)
{
    return low + ((high - low) * random01());
}

/* Smooth noise from 0 to 1 at x,y. */
float hash(int x, int y)
{
    std::uint32_t h = (static_cast<std::uint32_t>(x) * 374761393U) + (static_cast<std::uint32_t>(y) * 668265263U);
    h = (h ^ (h >> 13)) * 1274126177U;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffU) / 65535.0F;
}
float noise(float x, float y)
{
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    float u = x - fx;
    float v = y - fy;
    u = u * u * (3 - (2 * u));
    v = v * v * (3 - (2 * v));
    const float top = hash(ix, iy) + ((hash(ix + 1, iy) - hash(ix, iy)) * u);
    const float bottom = hash(ix, iy + 1) + ((hash(ix + 1, iy + 1) - hash(ix, iy + 1)) * u);
    return top + ((bottom - top) * v);
}

/* A fire's colour at heat (0 cold and dark, 1 white hot), 0-255 each. */
void fireColour(float heat, float *red, float *green, float *blue)
{
    struct Stop {
        float at;
        float r;
        float g;
        float b;
    };
    static const Stop stops[] = {{0.0F, 0, 0, 0},
                                 {0.25F, 150, 28, 6},
                                 {0.5F, 235, 95, 15},
                                 {0.75F, 255, 185, 50},
                                 {1.0F, 255, 245, 200}};
    heat = std::clamp(heat, 0.0F, 1.0F);
    for (std::size_t i = 1; i < std::size(stops); i++) {
        if (heat <= stops[i].at) {
            const Stop &a = stops[i - 1];
            const Stop &b = stops[i];
            const float k = (heat - a.at) / (b.at - a.at);
            *red = a.r + ((b.r - a.r) * k);
            *green = a.g + ((b.g - a.g) * k);
            *blue = a.b + ((b.b - a.b) * k);
            return;
        }
    }
    *red = 255;
    *green = 245;
    *blue = 200;
}

/* What a fire of the scene's gives off, on the screen from its origin:
 * flames rise and cool, embers fly up and wander, smoke drifts up and
 * spreads. */
enum class Kind : std::uint8_t { FLAME, EMBER, SMOKE };
struct Particle {
    float x;
    float y;
    float vx;
    float vy;
    float age;
    float life;
    float size;
    float heat;
    Kind kind;
};
const int MOST_PARTICLES = 220;
/* A fire: key's (the item's, or a flame left's), its point on the map, its
 * last tick, what it has given off. */
struct Fire {
    u16 key;
    bool used;
    bool live;
    u16 mapX;
    u16 mapY;
    i16 mapZ;
    u32 tick;
    int count;
    Particle particles[MOST_PARTICLES];
};
Fire fires[24];

/* The fire's frame this frame on the screen: its box, each pixel's heat
 * (0 none) and whether it shows. */
struct Shape {
    int left;
    int top;
    int width;
    int height;
    int shown;
    u8 heat[MOST_SIDE * MOST_SIDE];
    u8 visible[MOST_SIDE * MOST_SIDE];
};
Shape shapeNow;

Fire *fireOf(u16 key)
{
    Fire *unused = nullptr;
    for (Fire &fire : fires) {
        if (fire.used && fire.key == key) {
            return &fire;
        }
        if (!fire.used && unused == nullptr) {
            unused = &fire;
        }
    }
    if (unused != nullptr) {
        unused->used = true;
        unused->live = false;
        unused->key = key;
        unused->count = 0;
        unused->tick = now();
    }
    return unused;
}

/* The fire's frame's pixels from the origin x,y, and which show (by its
 * cover, when the game drew it there: what is in front hides them). */
bool shapeFire(u16 shape, u16 frameNumber, bool mirrored, float x, float y, const Cover *cover)
{
    ShapeFrame *image = cachedShapeFrame(shape, frameNumber);
    if (image == nullptr) {
        return false;
    }
    const auto *frame = reinterpret_cast<const u8 *>(image);
    int w = 0;
    int h = 0;
    if (!makePicture(frame, mirrored, 0, 0, false, &w, &h)) {
        return false;
    }
    const s32 originX = mirrored ? w - 1 - drawframe::dword(frame + 0xc) : drawframe::dword(frame + 0xc);
    shapeNow.left = static_cast<int>(std::floor(x + 0.5F)) - originX;
    shapeNow.top = static_cast<int>(std::floor(y + 0.5F)) - drawframe::dword(frame + 0x10);
    shapeNow.width = w;
    shapeNow.height = h;
    shapeNow.shown = 0;
    const bool byCover = cover != nullptr && cover->frame == frameNumber && cover->width == w && cover->height == h;
    for (int i = 0; i < w * h; i++) {
        shapeNow.heat[i] = 0;
        shapeNow.visible[i] = 0;
        if ((picture[i] & 0xffU) == 0) {
            continue;
        }
        const auto r = static_cast<float>(picture[i] >> 24);
        const auto g = static_cast<float>((picture[i] >> 16) & 0xffU);
        const auto b = static_cast<float>((picture[i] >> 8) & 0xffU);
        const float light = ((0.3F * r) + (0.59F * g) + (0.11F * b)) / 255;
        shapeNow.heat[i] = static_cast<u8>(std::clamp(0.3F + (0.85F * light), 0.01F, 1.0F) * 255);
        if (!byCover || cover->seen[i] != 2) {
            shapeNow.visible[i] = 1;
            shapeNow.shown++;
        }
    }
    return true;
}

/* A pixel of the frame's that shows, picked at random, its row in the
 * frame's top part (the fraction most of its height). */
bool pickPixel(float most, int *column, int *row)
{
    if (shapeNow.shown == 0) {
        return false;
    }
    const int tall = std::max(1, static_cast<int>(static_cast<float>(shapeNow.height) * most));
    for (int tries = 0; tries < 12; tries++) {
        const int c = static_cast<int>(random01() * static_cast<float>(shapeNow.width));
        const int r = static_cast<int>(random01() * static_cast<float>(tall));
        if (c < shapeNow.width && r < shapeNow.height && shapeNow.visible[(r * shapeNow.width) + c] != 0U) {
            *column = c;
            *row = r;
            return true;
        }
    }
    return false;
}

void addParticle(Fire &fire, const Particle &particle)
{
    if (fire.count < MOST_PARTICLES) {
        fire.particles[fire.count++] = particle;
    }
}

/* A tick of the fire's: what it gives off while it burns, and how what it
 * gave off moves. Places from the fire's origin, ox,oy. */
void tickFire(Fire &fire, bool burning, float ox, float oy)
{
    if (burning && shapeNow.shown > 0) {
        const float size = std::clamp(static_cast<float>(shapeNow.shown) / 300.0F, 0.4F, 2.5F);
        const int flames = std::clamp(static_cast<int>(static_cast<float>(shapeNow.shown) / 30.0F), 3, 14);
        int column = 0;
        int row = 0;
        for (int n = 0; n < flames; n++) {
            if (!pickPixel(1.0F, &column, &row)) {
                break;
            }
            const float heat = static_cast<float>(shapeNow.heat[(row * shapeNow.width) + column]) / 255;
            addParticle(fire, {static_cast<float>(shapeNow.left + column) + random01() - ox,
                               static_cast<float>(shapeNow.top + row) + random01() - oy, between(-0.12F, 0.12F),
                               -between(0.3F, 0.8F), 0, between(9, 22), between(1.4F, 2.8F), heat, Kind::FLAME});
        }
        if (random01() < 0.12F * size && pickPixel(0.5F, &column, &row)) {
            addParticle(fire, {static_cast<float>(shapeNow.left + column) - ox,
                               static_cast<float>(shapeNow.top + row) - oy, between(-0.45F, 0.45F),
                               -between(0.8F, 1.6F), 0, between(35, 75), between(0.55F, 0.9F), 1, Kind::EMBER});
        }
        if (random01() < 0.2F * size && pickPixel(0.3F, &column, &row)) {
            addParticle(fire, {static_cast<float>(shapeNow.left + column) - ox,
                               static_cast<float>(shapeNow.top + row) - oy - 2, between(-0.1F, 0.1F),
                               -between(0.25F, 0.45F), 0, between(70, 110), between(2.5F, 4.5F), 0, Kind::SMOKE});
        }
    }
    int kept = 0;
    for (int i = 0; i < fire.count; i++) {
        Particle p = fire.particles[i];
        p.age += 1;
        if (p.age >= p.life) {
            continue;
        }
        switch (p.kind) {
        case Kind::FLAME:
            p.vy -= 0.025F;
            p.vx += (noise(p.x * 0.2F, p.age * 0.15F) - 0.5F) * 0.08F;
            break;
        case Kind::EMBER:
            p.vx += (noise((p.x * 0.1F) + 17, p.age * 0.2F) - 0.5F) * 0.12F;
            p.vy *= 0.985F;
            break;
        case Kind::SMOKE:
            p.vx += 0.004F;
            p.vy *= 0.995F;
            break;
        }
        p.x += p.vx;
        p.y += p.vy;
        fire.particles[kept++] = p;
    }
    fire.count = kept;
}

/* The fire's pictures: its light added on, its smoke blended, CANVAS_SCALE
 * of their pixels to the screen's. */
const int CANVAS_SCALE = 2;
const int CANVAS_SIDE = 384;
float lightRed[CANVAS_SIDE * CANVAS_SIDE];
float lightGreen[CANVAS_SIDE * CANVAS_SIDE];
float lightBlue[CANVAS_SIDE * CANVAS_SIDE];
float smoke[CANVAS_SIDE * CANVAS_SIDE];
std::uint32_t canvas[CANVAS_SIDE * CANVAS_SIDE];

/* Where the pictures are on the screen, and their size in their pixels. */
struct Canvas {
    float left;
    float top;
    int width;
    int height;
};

/* A soft spot of radius (the screen's pixels) at the screen's x,y: its
 * colour added on, or its smoke (alpha) when smoky. */
void splat(const Canvas &at, float x, float y, float radius, float red, float green, float blue, float alpha,
           bool smoky)
{
    const float cx = (x - at.left) * CANVAS_SCALE;
    const float cy = (y - at.top) * CANVAS_SCALE;
    const float r = std::max(0.75F, radius * CANVAS_SCALE);
    const int x0 = std::max(0, static_cast<int>(cx - r));
    const int x1 = std::min(at.width - 1, static_cast<int>(cx + r));
    const int y0 = std::max(0, static_cast<int>(cy - r));
    const int y1 = std::min(at.height - 1, static_cast<int>(cy + r));
    const float inverse = 1.0F / (r * r);
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            const float dx = (static_cast<float>(px) + 0.5F) - cx;
            const float dy = (static_cast<float>(py) + 0.5F) - cy;
            float k = 1.0F - (((dx * dx) + (dy * dy)) * inverse);
            if (k <= 0) {
                continue;
            }
            k *= k;
            const int i = (py * at.width) + px;
            if (smoky) {
                smoke[i] += alpha * k;
            } else {
                lightRed[i] += red * k;
                lightGreen[i] += green * k;
                lightBlue[i] += blue * k;
            }
        }
    }
}

/* Whether the screen's x,y is on a pixel of the fire's frame that what is
 * in front hides. */
bool hiddenAt(float x, float y)
{
    const int column = static_cast<int>(std::floor(x)) - shapeNow.left;
    const int row = static_cast<int>(std::floor(y)) - shapeNow.top;
    if (column < 0 || row < 0 || column >= shapeNow.width || row >= shapeNow.height) {
        return false;
    }
    const int i = (row * shapeNow.width) + column;
    return shapeNow.heat[i] != 0U && shapeNow.visible[i] == 0U;
}

/* The fire drawn from its origin ox,oy: its body (its frame's pixels that
 * show, their heat flickering), its flames, embers and smoke. */
void drawFire(const Fire &fire, bool burning, float ox, float oy)
{
    float minX = burning ? static_cast<float>(shapeNow.left) : 1e9F;
    float minY = burning ? static_cast<float>(shapeNow.top) : 1e9F;
    float maxX = burning ? static_cast<float>(shapeNow.left + shapeNow.width) : -1e9F;
    float maxY = burning ? static_cast<float>(shapeNow.top + shapeNow.height) : -1e9F;
    for (int i = 0; i < fire.count; i++) {
        const Particle &p = fire.particles[i];
        const float reach = p.kind == Kind::SMOKE ? p.size * 2.5F : p.size;
        minX = std::min(minX, ox + p.x - reach);
        minY = std::min(minY, oy + p.y - reach);
        maxX = std::max(maxX, ox + p.x + reach);
        maxY = std::max(maxY, oy + p.y + reach);
    }
    if (minX > maxX) {
        return;
    }
    Canvas at{std::floor(minX) - 2, std::floor(minY) - 2, 0, 0};
    at.width = std::min(CANVAS_SIDE, static_cast<int>((maxX - at.left + 2) * CANVAS_SCALE));
    at.height = std::min(CANVAS_SIDE, static_cast<int>((maxY - at.top + 2) * CANVAS_SCALE));
    if (at.width <= 0 || at.height <= 0) {
        return;
    }
    const int area = at.width * at.height;
    std::fill_n(lightRed, area, 0.0F);
    std::fill_n(lightGreen, area, 0.0F);
    std::fill_n(lightBlue, area, 0.0F);
    std::fill_n(smoke, area, 0.0F);
    const float t = static_cast<float>(now()) / 60.0F;
    float red = 0;
    float green = 0;
    float blue = 0;
    if (burning) {
        for (int row = 0; row < shapeNow.height; row++) {
            for (int column = 0; column < shapeNow.width; column++) {
                const int i = (row * shapeNow.width) + column;
                if (shapeNow.visible[i] == 0U) {
                    continue;
                }
                const float flicker =
                    noise(static_cast<float>(column) * 0.3F, (static_cast<float>(row) * 0.25F) + (t * 6.0F));
                const float heat = (static_cast<float>(shapeNow.heat[i]) / 255) * (0.6F + (0.6F * flicker));
                fireColour(heat, &red, &green, &blue);
                splat(at, static_cast<float>(shapeNow.left + column) + 0.5F,
                      static_cast<float>(shapeNow.top + row) + 0.5F, 1.1F, red * 0.45F, green * 0.45F, blue * 0.45F,
                      0, false);
            }
        }
    }
    for (int i = 0; i < fire.count; i++) {
        const Particle &p = fire.particles[i];
        const float x = ox + p.x;
        const float y = oy + p.y;
        if (hiddenAt(x, y)) {
            continue;
        }
        const float left = 1.0F - (p.age / p.life);
        switch (p.kind) {
        case Kind::FLAME:
            fireColour((p.heat * left * (2 - left)) + 0.05F, &red, &green, &blue);
            splat(at, x, y, p.size * (0.5F + (0.5F * left)), red * 0.6F, green * 0.6F, blue * 0.6F, 0, false);
            break;
        case Kind::EMBER: {
            const float twinkle = 0.6F + (0.4F * noise(p.age * 0.5F, p.x));
            fireColour(0.55F + (0.45F * left), &red, &green, &blue);
            splat(at, x, y, p.size, red * twinkle, green * twinkle, blue * twinkle, 0, false);
            break;
        }
        case Kind::SMOKE: {
            const float grown = p.size * (1.0F + (1.5F * (1.0F - left)));
            const float rise = std::min(1.0F, (p.age / p.life) * 6.0F);
            splat(at, x, y, grown, 0, 0, 0, 0.16F * left * rise, true);
            break;
        }
        }
    }
    /* The smoke first, grey and blended; the light added on over it. */
    for (int i = 0; i < area; i++) {
        canvas[i] = 0x3a342e00U | static_cast<std::uint32_t>(std::min(0.55F, smoke[i]) * 255);
    }
    const float w = static_cast<float>(at.width) / CANVAS_SCALE;
    const float h = static_cast<float>(at.height) / CANVAS_SCALE;
    overlayPictureAs(canvas, at.width, at.height, at.left, at.top, w, h, OverlayBlend::BLEND, false);
    const auto channel = [](float value) { return static_cast<std::uint32_t>(std::clamp(value, 0.0F, 255.0F)); };
    for (int i = 0; i < area; i++) {
        canvas[i] = (channel(lightRed[i]) << 24) | (channel(lightGreen[i]) << 16) | (channel(lightBlue[i]) << 8) | 0xffU;
    }
    overlayPictureAs(canvas, at.width, at.height, at.left, at.top, w, h, OverlayBlend::ADD, false);
}

/* A fire's ticks and pictures: key's, at the map's point, burning frame of
 * shape; with a glow round it, flickering, as large as it shows. */
void burn(u16 key, u16 mapX, u16 mapY, i16 mapZ, u16 shape, u16 frame, bool mirrored, const Cover *cover)
{
    Fire *fire = fireOf(key);
    if (fire == nullptr) {
        return;
    }
    fire->live = true;
    fire->mapX = mapX;
    fire->mapY = mapY;
    fire->mapZ = mapZ;
    float x;
    float y;
    mapToOverlay(mapX, mapY, mapZ, &x, &y);
    if (!shapeFire(shape, frame, mirrored, x, y, cover)) {
        return;
    }
    const u32 time = now();
    for (u32 steps = std::min<u32>(time - fire->tick, 6); steps > 0; steps--) {
        tickFire(*fire, true, x, y);
    }
    fire->tick = time;
    if (shapeNow.shown > 0) {
        const float strength = std::clamp(static_cast<float>(shapeNow.shown) / 600.0F, 0.25F, 1.0F) *
                               (0.8F + (0.2F * noise(static_cast<float>(time) * 0.15F, static_cast<float>(key))));
        flash(static_cast<float>(shapeNow.left) + (static_cast<float>(shapeNow.width) / 2),
              static_cast<float>(shapeNow.top) + (static_cast<float>(shapeNow.height) / 2), strength * 0.45F);
    }
    drawFire(*fire, true, x, y);
}

/* The cover of the item the game drew on the screen lately. */
const Cover *freshCover(u16 item)
{
    for (const Cover &cover : covers) {
        if (cover.used && !cover.sides && cover.item == item && now() - cover.at <= COVER_KEEP) {
            return &cover;
        }
    }
    return nullptr;
}

}

bool effectsReplaces(u16 shape)
{
    return isFire(shape) && on();
}

void effectsItemDrawing(Viewport &view, u16 item, u16 frame, bool before, bool sides, i16 x, i16 y, i16 toScreenX,
                        i16 toScreenY)
{
    if (!before) {
        if (drawing != nullptr) {
            copyBox(view, *drawing, drawing->left - scrollX - toScreenX, drawing->top - scrollY - toScreenY,
                    drawing->drawn, true);
            drawing->at = now();
        }
        drawing = nullptr;
        return;
    }
    drawing = nullptr;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    if (!frameBox(itemShape[item], frame, (itemFlags[item] & ITEM_FLIPPED) != 0, x, y, &left, &top, &width, &height)) {
        return;
    }
    /* On the map's screen: the screen's pixels with the scroll. */
    const int mapLeft = left + toScreenX + scrollX;
    const int mapTop = top + toScreenY + scrollY;
    Cover *cover = coverOf(item, sides);
    if (cover->frame != frame || cover->left != mapLeft || cover->top != mapTop || cover->width != width ||
        cover->height != height) {
        cover->frame = frame;
        cover->left = mapLeft;
        cover->top = mapTop;
        cover->width = width;
        cover->height = height;
        std::fill_n(cover->known, width * height, static_cast<u8>(0));
        std::fill_n(cover->seen, width * height, static_cast<u8>(0));
    }
    copyBox(view, *cover, left, top, cover->under, false);
    drawing = cover;
}

void drawEffectCovers()
{
    if (!on()) {
        for (Cover &cover : covers) {
            cover.used = false;
        }
        return;
    }
    const u32 time = now();
    const RGB *colours = gamePalette->data.get();
    for (Cover &cover : covers) {
        if (!cover.used) {
            continue;
        }
        if (time - cover.at > COVER_KEEP || !isFire(itemShape[cover.item]) ||
            (itemFlags[cover.item] & ITEM_CONTAINED) != 0) {
            cover.used = false;
            continue;
        }
        const int left = cover.left - scrollX;
        const int top = cover.top - scrollY;
        for (int i = 0; i < cover.width * cover.height; i++) {
            picture[i] = 0;
            cover.seen[i] = 0;
            if (cover.known[i] == 0U || cover.drawn[i] == cover.under[i]) {
                continue;
            }
            if (!frameShows(left + (i % cover.width), top + (i / cover.width), cover.drawn[i])) {
                cover.seen[i] = 2;
                continue;
            }
            cover.seen[i] = 1;
            const RGB &c = colours[cover.under[i]];
            picture[i] = (static_cast<std::uint32_t>((c.red * 255) / 63) << 24) |
                         (static_cast<std::uint32_t>((c.green * 255) / 63) << 16) |
                         (static_cast<std::uint32_t>((c.blue * 255) / 63) << 8) | 0xffU;
        }
        overlayPictureAs(picture, cover.width, cover.height, static_cast<float>(left), static_cast<float>(top),
                         static_cast<float>(cover.width), static_cast<float>(cover.height), OverlayBlend::BLEND, true);
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
    lingering[lingeringCount++] = {itemX[item], itemY[item], itemZ[item], Item(item).frame(), now(),
                                   static_cast<u16>(0x8000U | (nextLingeringKey++ & 0x7fffU))};
}

void drawEffects()
{
    if (!on()) {
        lingeringCount = 0;
        flashCount = 0;
        for (Fire &fire : fires) {
            fire.used = false;
        }
        return;
    }
    for (Fire &fire : fires) {
        fire.live = false;
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
                if (isFire(itemShape[item])) {
                    burn(item, itemX[item], itemY[item], itemZ[item], itemShape[item], Item(item).frame(),
                         (itemFlags[item] & ITEM_FLIPPED) != 0, freshCover(item));
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
        const auto frame = static_cast<u16>((fire.frame + (since / FIRE_FRAME)) % std::max<u16>(1, frames));
        burn(fire.key, fire.x, fire.y, fire.z, LEFT_FIRE, frame, false, nullptr);
        lingering[kept++] = fire;
    }
    lingeringCount = kept;
    /* The fires gone out (their items taken away, off the screen): what
     * they gave off goes on till it is spent. */
    for (Fire &fire : fires) {
        if (!fire.used || fire.live) {
            continue;
        }
        if (fire.count == 0) {
            fire.used = false;
            continue;
        }
        float x;
        float y;
        mapToOverlay(fire.mapX, fire.mapY, fire.mapZ, &x, &y);
        shapeNow.shown = 0;
        shapeNow.width = 0;
        shapeNow.height = 0;
        for (u32 steps = std::min<u32>(time - fire.tick, 6); steps > 0; steps--) {
            tickFire(fire, false, x, y);
        }
        fire.tick = time;
        drawFire(fire, false, x, y);
    }
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
