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
#include "npc.h"
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

/* The flames a blast leaves, and how long they burn on (in the system
 * timer's ticks, sixtieths of a second). */
const u16 LEFT_FIRE = 0x44a;
const u32 FIRE_LINGERS = 360;
/* A muzzle flash's ticks. */
const u32 FLASH_TICKS = 6;

/* The square root of value (0 or more), by Newton's steps: no C library's
 * function for the game (tools/boundary.py). */
float root(float value)
{
    if (value <= 0) {
        return 0;
    }
    float guess = value > 1 ? value / 2 : 1;
    for (int step = 0; step < 8; step++) {
        guess = 0.5F * (guess + (value / guess));
    }
    return guess;
}

/* The shapes drawn as effects: the explosions, flashes and puffs of smoke,
 * the jets of flame and the flames a blast leaves (No Remorse's). */
bool isEffect(u16 shape)
{
    return (shape >= 0x31c && shape <= 0x327) || (shape >= 0x438 && shape <= 0x43c) || shape == 0x4cb ||
           shape == 0x50a || shape == 0x518 || shape == LEFT_FIRE;
}

/* The fires: the jets of flame and the flames a blast leaves. */
bool isFire(u16 shape)
{
    return (shape >= 0x438 && shape <= 0x43c) || shape == 0x4cb || shape == 0x50a || shape == 0x518 ||
           shape == LEFT_FIRE;
}

/* The jets of flame out of a pipe (FLAMEBOX's): their box lies long along
 * the floor, the pipe at its far end (its least x or y), the flame shooting
 * along it to the item's own corner, as their art's does. Each burns as
 * one shape while it is on (43Bh along y, 50Ah along x), and opens (43Ah,
 * 518h) and shuts (43Ch, 4CBh) as others at the same point: frames of the
 * flame longer, or shorter, out of the same pipe. */
bool isJet(u16 shape)
{
    return shape == 0x43a || shape == 0x43b || shape == 0x43c || shape == 0x4cb || shape == 0x50a || shape == 0x518;
}
/* The burning shape a jet's opening or shutting is of: its box. */
u16 jetBody(u16 shape)
{
    return (shape == 0x43a || shape == 0x43b || shape == 0x43c) ? 0x43b : 0x50a;
}
/* The width of the burning jet's frames: a frame of the opening or the
 * shutting reaches as far along as its width is of it. */
const float JET_WIDTH = 124;
/* How high up its box a jet comes out of the pipe (0-1). */
const float JET_HEIGHT = 0.25F;

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
        /* The video memory, flat (what the window shows): screenViewport's
         * pixels are its bank's window. */
        const u8 *shown = screenSvgaMemory();
        if (shown == nullptr) {
            return true;
        }
        return shown[(y * SCREEN_WIDTH) + x] == colour;
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

void fireColour(float heat, float *red, float *green, float *blue);

/* The explosions' fireballs (the grenades' and rockets' blasts): the
 * palette's dull maroons and browns, drawn as a fire's colours, by their
 * light. */
bool isBlast(u16 shape)
{
    return shape == 0x320 || shape == 0x321 || shape == 0x323 || shape == 0x324 || shape == 0x325;
}

/* An effect's picture at its origin x,y on the screen: a glow of its own
 * shape twice, larger and added on, flickering, then the picture, smooth
 * (added on too when translucent: the jets of flame); a blast in a fire's
 * colours, gold to red, its light the heat. */
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
    if (isBlast(shape)) {
        for (int i = 0; i < w * h; i++) {
            if ((picture[i] & 0xffU) == 0U) {
                continue;
            }
            const auto red = static_cast<float>((picture[i] >> 24) & 0xffU);
            const auto green = static_cast<float>((picture[i] >> 16) & 0xffU);
            const auto blue = static_cast<float>((picture[i] >> 8) & 0xffU);
            const float light = ((red * 3) + (green * 6) + blue) / 2550.0F;
            /* The darkest stay a deep red, smoke in the fire; the rest
             * orange to gold, white in the hottest. */
            const float heat = light < 0.06F ? 0.12F + (light * 2) : std::min(1.0F, 0.22F + (light * 1.25F));
            float r = 0;
            float g = 0;
            float b = 0;
            fireColour(heat, &r, &g, &b);
            picture[i] = (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
                         (static_cast<std::uint32_t>(b) << 8) | (picture[i] & 0xffU);
        }
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
                const float k = std::clamp(1.0F - root((dx * dx) + (dy * dy)), 0.0F, 1.0F);
                blob[(py * BLOB) + px] = colour | static_cast<std::uint32_t>(255.0F * k * k * strength);
            }
        }
        const float side = radius * (0.7F + (0.3F * strength)) * 2;
        overlayPictureAs(blob, BLOB, BLOB, x - (side / 2), y - (side / 2), side, side, OverlayBlend::ADD, false);
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
enum class Kind : std::uint8_t { FLAME, JET, EMBER, SMOKE };
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
const int MOST_PARTICLES = 360;
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

/* The fire's body this frame: the item's box on the map (its footprint,
 * x0-x1 by y0-y1, and its height from z0), its bounds on the screen, and
 * the cover of the item the game drew (what is in front hides the fire's
 * pixels there), none for an item the game does not draw. The flame is the
 * box's own, no picture's: the art of a fire is not drawn. */
struct Volume {
    float x0;
    float y0;
    float x1;
    float y1;
    float z0;
    float height;
    float left;
    float top;
    float right;
    float bottom;
    bool jet;
    /* How far along its box the jet reaches, 0-1 (its frame's). */
    float reach;
};
Volume volumeNow;

/* The screen's point of the box's u,v across its footprint and w up it
 * (each 0-1). */
void volumePoint(float u, float v, float w, float *x, float *y)
{
    const Volume &box = volumeNow;
    mapToOverlay(static_cast<u16>(box.x1 - (u * (box.x1 - box.x0))), static_cast<u16>(box.y1 - (v * (box.y1 - box.y0))),
                 static_cast<i16>(box.z0 + (w * box.height)), x, y);
}

/* The shape's box at the map's point: its footprint (16 map units a step
 * across, the x and y swapped for a flipped item) and its height (8 a
 * step), at least a flame's. */
void volumeOf(u16 shape, bool flipped, u16 mapX, u16 mapY, i16 mapZ)
{
    const ShapeInfo &info = typeFlags.infos[isJet(shape) ? jetBody(shape) : shape];
    float across = static_cast<float>(std::max<int>(1, info.x)) * 16;
    float deep = static_cast<float>(std::max<int>(1, info.y)) * 16;
    if (flipped) {
        std::swap(across, deep);
    }
    Volume &box = volumeNow;
    box.x1 = mapX;
    box.y1 = mapY;
    box.x0 = static_cast<float>(mapX) - across;
    box.y0 = static_cast<float>(mapY) - deep;
    box.z0 = mapZ;
    box.height = std::max(24.0F, static_cast<float>(info.z) * 8);
    box.jet = isJet(shape);
    box.reach = 1;
    box.left = 1e9F;
    box.top = 1e9F;
    box.right = -1e9F;
    box.bottom = -1e9F;
    for (int corner = 0; corner < 8; corner++) {
        float x = 0;
        float y = 0;
        volumePoint(static_cast<float>(corner & 1), static_cast<float>((corner >> 1) & 1),
                    static_cast<float>(corner >> 2), &x, &y);
        box.left = std::min(box.left, x);
        box.top = std::min(box.top, y);
        box.right = std::max(box.right, x);
        box.bottom = std::max(box.bottom, y);
    }
}

/* The footprint's size in steps of 16 map units square, the flame's
 * thickness: how many tongues it gives off and how wide they are. */
float volumeCells()
{
    return ((volumeNow.x1 - volumeNow.x0) * (volumeNow.y1 - volumeNow.y0)) / 256.0F;
}

/* The screen's point of a jet's line, along 0 at the pipe to 1 at its
 * end, w up its box: down the middle of the box's long side. */
void jetPoint(float along, float w, float *x, float *y)
{
    if (volumeNow.x1 - volumeNow.x0 >= volumeNow.y1 - volumeNow.y0) {
        volumePoint(1 - along, 0.5F, w, x, y);
    } else {
        volumePoint(0.5F, 1 - along, w, x, y);
    }
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
    if (burning && volumeNow.jet) {
        /* A jet: tongues out of the pipe, hot and narrow, shooting along
         * its line, slowing, spreading and rising as they cool at its end;
         * embers thrown off its far half, smoke from its end. */
        float startX = 0;
        float startY = 0;
        float endX = 0;
        float endY = 0;
        /* From just inside the pipe's mouth. */
        jetPoint(-0.05F, JET_HEIGHT, &startX, &startY);
        jetPoint(volumeNow.reach, JET_HEIGHT, &endX, &endY);
        const float dx = endX - startX;
        const float dy = endY - startY;
        const float length = std::max(1.0F, root((dx * dx) + (dy * dy)));
        const int tongues = static_cast<int>(8 + (16 * volumeNow.reach));
        for (int n = 0; n < tongues; n++) {
            const float life = between(14, 20);
            const float speed = between(1.45F, 1.75F) / life;
            const float spread = between(-0.012F, 0.012F);
            const float vx = (dx * speed) - (dy * spread);
            const float vy = (dy * speed) + (dx * spread);
            /* Out at any moment of the tick: no gaps between the ticks'. */
            const float early = random01();
            addParticle(fire, {startX - ox + (vx * early), startY - oy + (vy * early), vx, vy, 0, life,
                               between(3.0F, 4.0F), between(0.9F, 1.0F), Kind::JET});
        }
        float x = 0;
        float y = 0;
        if (random01() < 0.25F) {
            jetPoint(between(0.5F, 1.0F) * volumeNow.reach, JET_HEIGHT, &x, &y);
            addParticle(fire, {x - ox, y - oy, (dx / length) * between(0.3F, 0.8F), -between(0.6F, 1.4F), 0,
                               between(30, 60), between(0.55F, 0.9F), 1, Kind::EMBER});
        }
        if (random01() < 0.35F * volumeNow.reach) {
            jetPoint(between(0.8F, 1.0F) * volumeNow.reach, JET_HEIGHT + 0.2F, &x, &y);
            addParticle(fire, {x - ox, y - oy, between(-0.1F, 0.1F), -between(0.25F, 0.45F), 0, between(70, 110),
                               between(4.0F, 6.0F), 0, Kind::SMOKE});
        }
    } else if (burning) {
        /* Tongues from all over the footprint, near the floor, rising to
         * about the box's height as they cool; embers from its upper part;
         * smoke from its top. */
        const float cells = volumeCells();
        const float size = std::clamp(root(cells) * 1.6F, 2.4F, 6.0F);
        const int flames = std::clamp(static_cast<int>(cells * 2.0F), 5, 24);
        const float tall = volumeNow.height;
        float x = 0;
        float y = 0;
        for (int n = 0; n < flames; n++) {
            volumePoint(random01(), random01(), between(0, 0.12F), &x, &y);
            const float life = between(16, 28);
            const float rise = (tall * between(0.5F, 1.0F)) / life;
            addParticle(fire, {x - ox, y - oy, between(-0.1F, 0.1F), -rise, 0, life, size * between(0.75F, 1.2F),
                               between(0.75F, 1.0F), Kind::FLAME});
        }
        if (random01() < 0.08F + (0.01F * cells)) {
            volumePoint(random01(), random01(), between(0.4F, 0.9F), &x, &y);
            addParticle(fire, {x - ox, y - oy, between(-0.45F, 0.45F), -between(0.8F, 1.6F), 0, between(35, 75),
                               between(0.55F, 0.9F), 1, Kind::EMBER});
        }
        if (random01() < 0.25F + (0.02F * cells)) {
            volumePoint(random01(), random01(), between(0.85F, 1.0F), &x, &y);
            addParticle(fire, {x - ox, y - oy, between(-0.1F, 0.1F), -between(0.25F, 0.45F), 0, between(70, 110),
                               size * between(1.2F, 1.8F), 0, Kind::SMOKE});
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
        case Kind::JET:
            /* Slowed by the air, more and more buoyant as it cools. */
            p.vx *= 0.95F;
            p.vy *= 0.95F;
            p.vy -= 0.3F * (p.age / p.life) * (p.age / p.life);
            p.vx += (noise(p.x * 0.2F, p.age * 0.15F) - 0.5F) * 0.05F;
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

/* A soft spot of radius (the screen's pixels) at the screen's x,y, tall
 * times as high as it is wide: its colour added on, or its smoke (alpha)
 * when smoky. */
void splat(const Canvas &at, float x, float y, float radius, float red, float green, float blue, float alpha,
           bool smoky, float tall = 1.0F)
{
    const float cx = (x - at.left) * CANVAS_SCALE;
    const float cy = (y - at.top) * CANVAS_SCALE;
    const float r = std::max(0.75F, radius * CANVAS_SCALE);
    const float squash = 1.0F / tall;
    const int x0 = std::max(0, static_cast<int>(cx - r));
    const int x1 = std::min(at.width - 1, static_cast<int>(cx + r));
    const int y0 = std::max(0, static_cast<int>(cy - (r * tall)));
    const int y1 = std::min(at.height - 1, static_cast<int>(cy + (r * tall)));
    const float inverse = 1.0F / (r * r);
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            const float dx = (static_cast<float>(px) + 0.5F) - cx;
            const float dy = ((static_cast<float>(py) + 0.5F) - cy) * squash;
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

/* The fire drawn from its origin ox,oy: its body (its frame's pixels that
 * show, their heat flickering), its flames, embers and smoke. */
void drawFire(const Fire &fire, bool burning, float ox, float oy)
{
    float minX = burning ? volumeNow.left : 1e9F;
    float minY = burning ? volumeNow.top : 1e9F;
    float maxX = burning ? volumeNow.right : -1e9F;
    float maxY = burning ? volumeNow.bottom : -1e9F;
    for (int i = 0; i < fire.count; i++) {
        const Particle &p = fire.particles[i];
        const float reach = p.kind == Kind::SMOKE ? p.size * 2.5F : p.size * 2;
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
    if (burning && volumeNow.jet) {
        /* A jet's body: the glow along its line, white hot at the pipe,
         * wider and cooler toward its end, flickering. */
        for (int i = 0; i < 10; i++) {
            const float along = (static_cast<float>(i) + 0.5F) / 10;
            float x = 0;
            float y = 0;
            jetPoint(along * 0.8F * volumeNow.reach, JET_HEIGHT, &x, &y);
            const float flicker = noise(along * 6, t * 7.0F);
            fireColour((1.0F - (0.5F * along)) * (0.7F + (0.4F * flicker)), &red, &green, &blue);
            splat(at, x, y, 3.5F + (5.0F * along), red * 0.6F, green * 0.6F, blue * 0.6F, 0, false, 1.2F);
        }
    } else if (burning) {
        /* Its body: the glow over the footprint and its lower part,
         * flickering, hottest low down. */
        const float cells = volumeCells();
        const int across = std::clamp(static_cast<int>(root(cells) * 2.5F), 3, 9);
        const float radius = std::clamp(root(cells) * 2.2F, 3.0F, 8.0F);
        for (int i = 0; i < across; i++) {
            for (int j = 0; j < across; j++) {
                for (int level = 0; level < 2; level++) {
                    const float u = (static_cast<float>(i) + 0.5F) / static_cast<float>(across);
                    const float v = (static_cast<float>(j) + 0.5F) / static_cast<float>(across);
                    const float w = level == 0 ? 0.04F : 0.22F;
                    float x = 0;
                    float y = 0;
                    volumePoint(u, v, w, &x, &y);
                    const float flicker = noise((u * 5) + (static_cast<float>(level) * 9), (v * 5) + (t * 7.0F));
                    const float heat = (level == 0 ? 0.85F : 0.6F) * (0.55F + (0.6F * flicker));
                    fireColour(heat, &red, &green, &blue);
                    splat(at, x, y, radius * (level == 0 ? 1.0F : 0.8F), red * 0.18F, green * 0.18F, blue * 0.18F,
                          0, false, 1.4F);
                }
            }
        }
    }
    for (int i = 0; i < fire.count; i++) {
        const Particle &p = fire.particles[i];
        const float x = ox + p.x;
        const float y = oy + p.y;
        const float left = 1.0F - (p.age / p.life);
        switch (p.kind) {
        case Kind::FLAME: {
            const float cooled = p.heat * (0.35F + (0.55F * left * left));
            fireColour(cooled, &red, &green, &blue);
            const float fade = std::min(1.0F, p.age / 3.0F) * (0.3F + (0.7F * left));
            splat(at, x, y, p.size * (0.45F + (0.55F * left)), red * 0.32F * fade, green * 0.32F * fade,
                  blue * 0.32F * fade, 0, false, 1.9F);
            break;
        }
        case Kind::JET: {
            /* Narrow out of the pipe, widening as it goes and cools. */
            const float cooled = p.heat * (0.3F + (0.7F * left));
            fireColour(cooled, &red, &green, &blue);
            const float fade = 0.3F + (0.7F * left);
            splat(at, x, y, p.size * (0.8F + (1.5F * (1.0F - left))), red * 0.45F * fade, green * 0.45F * fade,
                  blue * 0.45F * fade, 0, false, 1.2F);
            break;
        }
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
    if (burning && volumeNow.jet) {
        /* A jet is solid where it burns, as its art is: its colours over
         * what is behind it, a little of its light added on over them. */
        for (int i = 0; i < area; i++) {
            const float most = std::max({lightRed[i], lightGreen[i], lightBlue[i]});
            const float solid = std::clamp((most - 40.0F) / 120.0F, 0.0F, 0.92F);
            const float scale = most > 0 ? std::min(1.0F, 255.0F / most) : 0;
            canvas[i] = (channel(lightRed[i] * scale) << 24) | (channel(lightGreen[i] * scale) << 16) |
                        (channel(lightBlue[i] * scale * 0.8F) << 8) | channel(solid * 255);
            lightRed[i] *= 0.4F;
            lightGreen[i] *= 0.4F;
            lightBlue[i] *= 0.4F;
        }
        overlayPictureAs(canvas, at.width, at.height, at.left, at.top, w, h, OverlayBlend::BLEND, false);
    }
    for (int i = 0; i < area; i++) {
        canvas[i] = (channel(lightRed[i]) << 24) | (channel(lightGreen[i]) << 16) | (channel(lightBlue[i]) << 8) | 0xffU;
    }
    overlayPictureAs(canvas, at.width, at.height, at.left, at.top, w, h, OverlayBlend::ADD, false);
}

/* A fire's ticks and pictures: key's, at the map's point, as large as
 * shape's box (volumeOf); with a glow round it, flickering. */
void burn(u16 key, u16 mapX, u16 mapY, i16 mapZ, u16 shape, bool mirrored, u16 frame = 0)
{
    /* A jet's opening, burning and shutting are items one after the other
     * at its point: one fire, what it gave off going on. */
    if (isJet(shape)) {
        for (Fire &other : fires) {
            if (other.used && other.key != key && other.mapX == mapX && other.mapY == mapY && other.mapZ == mapZ) {
                other.key = key;
                break;
            }
        }
    }
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
    volumeOf(shape, mirrored, mapX, mapY, mapZ);
    if (isJet(shape) && shape != jetBody(shape)) {
        const ShapeFrame *image = cachedShapeFrame(shape, frame);
        volumeNow.reach = image != nullptr ? std::clamp(static_cast<float>(image->width) / JET_WIDTH, 0.15F, 1.0F) : 1;
    }
    const u32 time = now();
    for (u32 steps = std::min<u32>(time - fire->tick, 6); steps > 0; steps--) {
        tickFire(*fire, true, x, y);
    }
    fire->tick = time;
    const float strength = std::clamp(volumeCells() / 8.0F, 0.3F, 1.0F) *
                           (0.8F + (0.2F * noise(static_cast<float>(time) * 0.15F, static_cast<float>(key))));
    flash((volumeNow.left + volumeNow.right) / 2, (volumeNow.top + volumeNow.bottom) / 2, strength * 0.45F);
    drawFire(*fire, true, x, y);
}

/* The guns the dead drop (effectsDropGun), for show only: no item, not
 * to be picked up. Each falls from the hand to the floor where its owner
 * fell, settles with a little bounce and lies there while its map is the
 * one played. Map units and ticks. */
struct Gun {
    bool used;
    u16 map;
    u16 shape;
    float x;
    float y;
    float z;
    float vz;
    float floor;
    bool bounced;
    bool landed;
    u32 since;
    u32 tick;
};
Gun guns[32];

/* A tick of the gun's fall: one small bounce, then it lies. */
void fallGun(Gun &gun)
{
    if (gun.landed) {
        return;
    }
    gun.vz -= 0.7F;
    gun.z += gun.vz;
    if (gun.z > gun.floor) {
        return;
    }
    gun.z = gun.floor;
    if (!gun.bounced && gun.vz < -2.5F) {
        gun.bounced = true;
        gun.vz = -gun.vz * 0.3F;
        return;
    }
    gun.landed = true;
}

/* The gun on the screen as it lies on the floor, its shadow under it while
 * it falls. */
void drawGun(const Gun &gun)
{
    float x = 0;
    float y = 0;
    if (!gun.landed) {
        mapToOverlay(static_cast<u16>(gun.x), static_cast<u16>(gun.y), static_cast<i16>(gun.floor), &x, &y);
        overlayDisc(x, y, 3.0F, 0x00000050U);
    }
    mapToOverlay(static_cast<u16>(gun.x), static_cast<u16>(gun.y), static_cast<i16>(gun.z), &x, &y);
    ShapeFrame *image = cachedShapeFrame(gun.shape, 0);
    if (image == nullptr) {
        return;
    }
    const auto *bytes = reinterpret_cast<const u8 *>(image);
    int w = 0;
    int h = 0;
    if (!makePicture(bytes, false, 0, 0, false, &w, &h)) {
        return;
    }
    overlayPictureAs(picture, w, h, x - static_cast<float>(drawframe::dword(bytes + 0xc)),
                     y - static_cast<float>(drawframe::dword(bytes + 0x10)), static_cast<float>(w),
                     static_cast<float>(h), OverlayBlend::BLEND, false);
}

}

bool effectsReplaces(u16 shape)
{
    return isFire(shape) && on();
}

void effectsDropGun(u16 npc)
{
    if (!on()) {
        return;
    }
    Npc dead(npc);
    const u16 weapon = dead.activeWeapon();
    if (weapon == 0U) {
        return;
    }
    Gun *gun = &guns[0];
    for (Gun &each : guns) {
        if (!each.used) {
            gun = &each;
            break;
        }
        if (each.since < gun->since) {
            gun = &each;
        }
    }
    /* Beside its owner, a little way off, from the hand's height. */
    *gun = Gun{true,
               currentMap,
               itemShape[weapon],
               static_cast<float>(dead.centreX()) + between(-10, 10),
               static_cast<float>(dead.centreY()) + between(-10, 10),
               static_cast<float>(itemZ[npc] + 24),
               0,
               static_cast<float>(itemZ[npc]),
               false,
               false,
               now(),
               now()};
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
    lingering[lingeringCount++] = {itemX[item], itemY[item], itemZ[item], now(),
                                   static_cast<u16>(0x8000U | (nextLingeringKey++ & 0x7fffU))};
}

void drawEffects()
{
    if (!on()) {
        lingeringCount = 0;
        flashCount = 0;
        for (Gun &gun : guns) {
            gun.used = false;
        }
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
                /* Not an editor's mark, which the game does not draw (a
                 * jet's place, PLACE ME, burns only as the jet does). */
                if (item < FIRST_ITEM || !isEffect(itemShape[item]) ||
                    (itemFlags[item] & (ITEM_CONTAINED | ITEM_INVISIBLE)) != 0 ||
                    typeFlags.infos[itemShape[item]].editor) {
                    continue;
                }
                if (isFire(itemShape[item])) {
                    burn(item, itemX[item], itemY[item], itemZ[item], itemShape[item],
                         (itemFlags[item] & ITEM_FLIPPED) != 0, Item(item).frame());
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
    /* The flames left, burning on. */
    int kept = 0;
    for (int i = 0; i < lingeringCount; i++) {
        Lingering &fire = lingering[i];
        const u32 since = time - fire.since;
        if (since > FIRE_LINGERS) {
            continue;
        }
        burn(fire.key, fire.x, fire.y, fire.z, LEFT_FIRE, false);
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
    /* The guns the dead dropped, on this map. */
    for (Gun &gun : guns) {
        if (!gun.used) {
            continue;
        }
        if (gun.map != currentMap) {
            gun.used = false;
            continue;
        }
        for (u32 steps = std::min<u32>(time - gun.tick, 6); steps > 0; steps--) {
            fallGun(gun);
        }
        gun.tick = time;
        drawGun(gun);
    }
}
