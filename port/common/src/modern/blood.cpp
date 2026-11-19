/* Blood. See blood.h. */
#include "blood.h"
#include "gamestate.h"
#include "camera.h"
#include "crusys.h"
#include "dlitem.h"
#include "gamepal.h"
#include "hud.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "npc.h"
#include "colours.h"
#include "overlay.h"
#include "pointer.h"
#include "pfloat.h"
#include "systimer.h"
#include "type.h"
#include "viewport.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace {

/* A drop in flight (or the mist), in map units, per tick of 60 a second. */
struct Drop {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float ground;
    float size;
    u16 age;
    u16 life;
    u8 mist;
};
/* A splat (where a drop came down) or a pool (under one shot dead): its
 * middle on the floor, how far it spreads (map units) and how long it takes
 * to (ticks), when it landed, its edge's shape (radii round it, a share of
 * its spread each), how dry it was drawn last, and the floors it lies on
 * (each painted with the part over its footprint). */
const int EDGE_POINTS = 16;
const int MOST_FLOORS = 6;
struct Splat {
    u16 x;
    u16 y;
    u8 z;
    u8 pool;
    u16 spread;
    u16 grows;
    u32 landed;
    u8 edge[EDGE_POINTS];
    u8 dryness;
    u8 floorCount;
    u16 floors[MOST_FLOORS];
};

const int MOST_DROPS = 600;
const int MOST_SPLATS = 500;
const float GRAVITY = 0.18F;
/* Blood dries over these ticks after it has lain this long, darkening to
 * brown in DRY_LEVELS steps. */
const u32 DRY_AFTER = 30 * 60;
const u32 DRY_OVER = 90 * 60;
const int DRY_LEVELS = 4;
/* The ways a pixel of it is shaded: the thin rim, the body, the deep
 * middle of a pool, the shine on a fresh one. */
enum Tone : u8 { RIM, BODY, DEEP, SHINE, TONES };

Drop drops[MOST_DROPS];
int dropCount = 0;
/* The splats as a ring, the oldest at first when it is full. */
Splat splats[MOST_SPLATS];
int splatCount = 0;
int splatFirst = 0;
/* The splats on each item, for the display list's quick test. */
u8 onItem[ITEMS];
/* Each colour reddened in each tone and dryness (the palette's nearest), 0
 * until worked out. */
u8 reddened[DRY_LEVELS][TONES][256];
u8 reddenedKnown[DRY_LEVELS][TONES][256];
u16 bloodMap = 0xffff;
u32 lastTime = 0;
std::uint32_t seed = 12345;

/* The drawing's own random numbers (the game's are kept for its
 * decisions). */
float random(float low, float high)
{
    seed = (seed * 1664525U) + 1013904223U;
    return low + ((high - low) * static_cast<float>((seed >> 8) & 0xffffU) / 65535.0F);
}

float cosineOf(i16 degrees)
{
    PFloat angle(static_cast<i16>(((degrees % 360) + 360) % 360), 0);
    return static_cast<float>(cosine(angle).value) / 65536;
}

float sineOf(i16 degrees)
{
    PFloat angle(static_cast<i16>(((degrees % 360) + 360) % 360), 0);
    return static_cast<float>(sine(angle).value) / 65536;
}

void forget()
{
    dropCount = 0;
    splatCount = 0;
    splatFirst = 0;
    std::fill(std::begin(onItem), std::end(onItem), 0);
    for (auto &level : reddenedKnown) {
        for (auto &tone : level) {
            std::fill(std::begin(tone), std::end(tone), 0);
        }
    }
}

/* The floor at x,y whose top is z: an item of the land with the point in
 * its footprint; 0 for none. */
u16 floorAt(u16 x, u16 y, u8 z)
{
    const int column = x >> CHUNK_SHIFT;
    const int row = y >> CHUNK_SHIFT;
    u16 found = 0;
    /* An item's place is its box's far corner: one in the next chunk may
     * reach back over the point. */
    for (int c = column; c <= std::min(column + 1, CHUNKS - 1); c++) {
        for (int r = row; r <= std::min(row + 1, CHUNKS - 1); r++) {
            for (u16 item = mapChunks[c][r]; item != 0U; item = itemNext[item]) {
                if (item < FIRST_ITEM || (itemFlags[item] & (ITEM_CONTAINED | ITEM_ETHEREAL | ITEM_INVISIBLE)) != 0) {
                    continue;
                }
                const ShapeInfo &info = typeFlags.infos[itemShape[item]];
                if (info.land == 0U || std::abs(itemZ[item] + (info.z * 8) - z) > 2) {
                    continue;
                }
                i16 width;
                i16 depth;
                i16 height;
                info.box(width, depth, height);
                if ((itemFlags[item] & ITEM_FLIPPED) != 0) {
                    std::swap(width, depth);
                }
                if (x <= itemX[item] && x > itemX[item] - width && y <= itemY[item] && y > itemY[item] - depth) {
                    found = item;
                }
            }
        }
    }
    return found;
}

/* Its place on the screen drawn again, what lies there in its order (the
 * camera's invalidate). */
void redrawFloors(const Splat &splat)
{
    const i16 hx = static_cast<i16>(splat.x >> 1);
    const i16 hy = static_cast<i16>(splat.y >> 1);
    const int sx = hx - hy - scrollX;
    const int sy = (hx >> 1) + (hy >> 1) - splat.z - scrollY;
    const int across = ((splat.spread * 3) / 4) + 2;
    const int down = ((splat.spread * 2) / 5) + 2;
    Rect area(static_cast<i16>(sx - across), static_cast<i16>(sy - down), static_cast<i16>(sx + across),
              static_cast<i16>(sy + down));
    if (gameCamera != nullptr && area.touches(screenRect)) {
        clipRect(&area, &screenRect);
        gameCamera->invalidate(area, 0);
    }
}

/* A splat or pool at x,y on the floor whose top is z, spreading to spread
 * over grows ticks; none off a floor. */
void addSplat(u16 x, u16 y, u8 z, u16 spread, u16 grows, bool pool)
{
    Splat splat{};
    splat.x = x;
    splat.y = y;
    splat.z = z;
    splat.pool = pool ? 1 : 0;
    splat.spread = spread;
    splat.grows = grows;
    splat.landed = systemTimer->time();
    /* An uneven edge: a few lobes, rough. */
    const auto phase = static_cast<i16>(random(0, 359));
    const auto phase2 = static_cast<i16>(random(0, 359));
    const int lobes = static_cast<int>(random(2, 4.99F));
    for (int i = 0; i < EDGE_POINTS; i++) {
        const int around = (i * 360) / EDGE_POINTS;
        const float swell = (0.65F * sineOf(static_cast<i16>((around * lobes) + phase))) +
                            (0.35F * sineOf(static_cast<i16>((around * (lobes + 3)) + phase2)));
        const float share = 0.8F + (0.15F * swell) + random(-0.08F, 0.08F);
        splat.edge[i] = static_cast<u8>(std::clamp(share, 0.5F, 1.0F) * 255);
    }
    /* The floors under it: its middle's and those its edge reaches. */
    const int reach = (spread * 11) / 10;
    const int offsets[9][2] = {{0, 0},      {reach, 0},      {-reach, 0},    {0, reach},     {0, -reach},
                               {reach, reach}, {-reach, -reach}, {reach, -reach}, {-reach, reach}};
    for (const auto &o : offsets) {
        const u16 floor = floorAt(static_cast<u16>(x + o[0]), static_cast<u16>(y + o[1]), z);
        if (floor == 0U || splat.floorCount == MOST_FLOORS ||
            std::find(splat.floors, splat.floors + splat.floorCount, floor) != splat.floors + splat.floorCount) {
            continue;
        }
        splat.floors[splat.floorCount++] = floor;
    }
    if (splat.floorCount == 0 || splat.floors[0] != floorAt(x, y, z)) {
        return;
    }
    if (splatCount == MOST_SPLATS) {
        /* The oldest goes. */
        const Splat &old = splats[splatFirst];
        for (int f = 0; f < old.floorCount; f++) {
            if (onItem[old.floors[f]] != 0U) {
                onItem[old.floors[f]]--;
            }
        }
        redrawFloors(old);
        splatFirst = (splatFirst + 1) % MOST_SPLATS;
        splatCount--;
    }
    splats[(splatFirst + splatCount) % MOST_SPLATS] = splat;
    splatCount++;
    for (int f = 0; f < splat.floorCount; f++) {
        if (onItem[splat.floors[f]] < 255) {
            onItem[splat.floors[f]]++;
        }
    }
    redrawFloors(splat);
}

void addSplat(const Drop &drop)
{
    addSplat(static_cast<u16>(drop.x), static_cast<u16>(drop.y), static_cast<u8>(drop.ground),
             static_cast<u16>(random(5, 12)), static_cast<u16>(random(8, 20)), false);
}

/* The splats spreading, or drying a step further: their floors drawn
 * again. */
void redrawChanging()
{
    const u32 now = systemTimer->time();
    for (int n = 0; n < splatCount; n++) {
        Splat &splat = splats[(splatFirst + n) % MOST_SPLATS];
        const u32 age = now - splat.landed;
        const auto dryness = static_cast<u8>(
            age < DRY_AFTER ? 0 : std::min<u32>(DRY_LEVELS - 1, (((age - DRY_AFTER) * DRY_LEVELS) / DRY_OVER)));
        if (age <= splat.grows + 1U || dryness != splat.dryness) {
            splat.dryness = dryness;
            redrawFloors(splat);
        }
    }
}

/* A tick of the drops: they fly, fall and land; the mist slows and fades. */
void step()
{
    int kept = 0;
    for (int i = 0; i < dropCount; i++) {
        Drop drop = drops[i];
        drop.age++;
        drop.x += drop.vx;
        drop.y += drop.vy;
        drop.z += drop.vz;
        if (drop.mist != 0U) {
            drop.vx *= 0.9F;
            drop.vy *= 0.9F;
            drop.vz *= 0.9F;
        } else {
            drop.vz -= GRAVITY;
            if (drop.z <= drop.ground) {
                drop.z = drop.ground;
                addSplat(drop);
                continue;
            }
        }
        if (drop.age >= drop.life) {
            continue;
        }
        drops[kept++] = drop;
    }
    dropCount = kept;
}

/* The colour under blood of the tone and dryness, 6 bits a component: so
 * much of the blood's (tenths) over the rest of the colour's; the blood
 * fresh, red, dark in a pool's depth, lighter where it shines, and dried
 * to a dark brown. */
void reddenedParts(u8 under, int dryness, int tone, int &red, int &green, int &blue)
{
    const RGB *colours = gamePalette->data.get();
    static const int FRESH[TONES][4] = {{46, 3, 5, 6}, {50, 3, 5, 8}, {36, 1, 3, 9}, {60, 22, 20, 8}};
    const int *blood = FRESH[tone];
    /* Toward the dried brown, a step a level. */
    const int dried[3] = {20, 7, 3};
    const int mix = dryness * 3;
    const int bloodRed = ((blood[0] * (9 - mix)) + (dried[0] * mix)) / 9;
    const int bloodGreen = ((blood[1] * (9 - mix)) + (dried[1] * mix)) / 9;
    const int bloodBlue = ((blood[2] * (9 - mix)) + (dried[2] * mix)) / 9;
    const int share = blood[3];
    red = ((colours[under].red * (10 - share)) + (bloodRed * share)) / 10;
    green = ((colours[under].green * (10 - share)) + (bloodGreen * share)) / 10;
    blue = ((colours[under].blue * (10 - share)) + (bloodBlue * share)) / 10;
}

/* The colour under blood as it is, 0xRRGGBB. */
std::uint32_t reddenedExactly(u8 under, int dryness, int tone)
{
    int red = 0;
    int green = 0;
    int blue = 0;
    reddenedParts(under, dryness, tone, red, green, blue);
    const auto part = [](int component) { return static_cast<std::uint32_t>((component * 255) / 63); };
    return part(red) << 16 | part(green) << 8 | part(blue);
}

u8 reddenedOf(u8 under, int dryness, int tone)
{
    if (reddenedKnown[dryness][tone][under] == 0U) {
        const RGB *colours = gamePalette->data.get();
        int red = 0;
        int green = 0;
        int blue = 0;
        reddenedParts(under, dryness, tone, red, green, blue);
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
        reddened[dryness][tone][under] = static_cast<u8>(best);
        reddenedKnown[dryness][tone][under] = 1;
    }
    return reddened[dryness][tone][under];
}

/* Where round its middle a point of the map dx,dy is, 0 up to EDGE_POINTS
 * (a fraction between): not its true angle, but even enough for an
 * uneven edge, and with no search. */
float edgePlace(int dx, int dy)
{
    const auto ax = static_cast<float>(std::abs(dx));
    const auto ay = static_cast<float>(std::abs(dy));
    if (ax + ay == 0) {
        return 0;
    }
    /* 0-4 round the circle, a quarter each. */
    const float part = ay / (ax + ay);
    float round = 0;
    if (dx >= 0 && dy >= 0) {
        round = part;
    } else if (dx < 0 && dy >= 0) {
        round = 2 - part;
    } else if (dx < 0) {
        round = 2 + part;
    } else {
        round = 4 - part;
    }
    return round * (EDGE_POINTS / 4.0F);
}

}

void noteBlood(u16 victim, u16 hitter, u8 direction, i16 damage)
{
    /* Part of the port's controls (pointer.h's setting), which the tests'
     * original controls turn off. */
    if (!pointerSetting()) {
        return;
    }
    if (currentMap != bloodMap) {
        bloodMap = currentMap;
        forget();
    }
    const Item body(victim);
    const Item from(hitter);
    /* The way the shot went (the game's 16 directions, 0 up the map and on
     * clockwise), else away from the shooter through the victim, else any
     * way. */
    i16 angle = static_cast<i16>(random(0, 359));
    if (direction < 16) {
        angle = static_cast<i16>(270 + ((direction * 45) / 2));
    } else if (hitter != 0U && hitter != victim && from.isValid()) {
        const std::int32_t dx = body.centreX() - from.centreX();
        const std::int32_t dy = body.centreY() - from.centreY();
        if (dx != 0 || dy != 0) {
            angle = angleOf(dx, dy);
        }
    }
    /* Shot dead: a pool spreads from under the body, a little back along
     * the shot's way, where it falls. */
    if (Npc(victim).hp() == 0) {
        i16 poolX = static_cast<i16>(body.centreX());
        i16 poolY = static_cast<i16>(body.centreY());
        alongAngle(angle, 20, &poolX, &poolY);
        addSplat(static_cast<u16>(poolX), static_cast<u16>(poolY), static_cast<u8>(body.z()),
                 static_cast<u16>(random(34, 48)), static_cast<u16>(random(300, 480)), true);
    }
    const int amount = std::clamp(8 + (damage / 3), 8, 40);
    for (int i = 0; i < amount && dropCount < MOST_DROPS; i++) {
        Drop &drop = drops[dropCount++];
        /* A cone about the shot's way. */
        const auto way = static_cast<i16>(angle + random(-28, 28));
        const float speed = random(1.5F, 5.0F);
        drop.x = body.centreX() + (cosineOf(angle) * 6);
        drop.y = body.centreY() + (sineOf(angle) * 6);
        drop.z = body.z() + random(22, 40);
        drop.vx = cosineOf(way) * speed;
        drop.vy = sineOf(way) * speed;
        drop.vz = random(0.3F, 3.2F);
        drop.ground = body.z();
        drop.size = random(1.2F, 2.4F);
        drop.age = 0;
        drop.life = 240;
        drop.mist = 0;
        /* A third of it a fine mist that fades quickly. */
        if (i % 3 == 0) {
            drop.mist = 1;
            drop.life = static_cast<u16>(random(15, 30));
            drop.size = random(3, 6);
        }
    }
}

void drawBlood()
{
    if (currentMap != bloodMap) {
        bloodMap = currentMap;
        forget();
    }
    const u32 now = systemTimer->time();
    if (worldActive == 0U) {
        lastTime = now;
        return;
    }
    const u32 ticks = std::min<u32>(now - lastTime, 60);
    lastTime = now;
    for (u32 i = 0; i < ticks; i++) {
        step();
    }
    if (ticks != 0U) {
        redrawChanging();
    }
    for (int i = 0; i < dropCount; i++) {
        const Drop &drop = drops[i];
        float x;
        float y;
        mapToOverlay(static_cast<u16>(drop.x), static_cast<u16>(drop.y), static_cast<i16>(drop.z), &x, &y);
        if (drop.mist != 0U) {
            const float fade = 1 - (static_cast<float>(drop.age) / drop.life);
            overlayDisc(x, y, drop.size, 0x9a101000U | static_cast<std::uint32_t>(140 * fade));
            continue;
        }
        /* A short streak behind it. */
        float x0;
        float y0;
        mapToOverlay(static_cast<u16>(drop.x - (drop.vx * 2)), static_cast<u16>(drop.y - (drop.vy * 2)),
                     static_cast<i16>(drop.z - (drop.vz * 2)), &x0, &y0);
        overlayLine(x0, y0, x, y, drop.size, 0x8a0a0af2U);
    }
}

bool bloodOn(u16 item)
{
    return item < ITEMS && onItem[item] != 0U;
}

void paintBlood(u16 item, Viewport &view)
{
    const Rect clip = view.bounds();
    const u32 now = systemTimer->time();
    /* The floor's footprint: each splat painted only over it, the part
     * over the next floor painted with that one. */
    i16 width;
    i16 depth;
    i16 height;
    typeFlags.infos[itemShape[item]].box(width, depth, height);
    if ((itemFlags[item] & ITEM_FLIPPED) != 0) {
        std::swap(width, depth);
    }
    const int floorRight = itemX[item];
    const int floorLeft = itemX[item] - width;
    const int floorBottom = itemY[item];
    const int floorTop = itemY[item] - depth;
    for (int n = 0; n < splatCount; n++) {
        const Splat &splat = splats[(splatFirst + n) % MOST_SPLATS];
        if (std::find(splat.floors, splat.floors + splat.floorCount, item) == splat.floors + splat.floorCount) {
            continue;
        }
        /* How far it has spread: quickly at first, slowing. */
        const u32 age = now - splat.landed;
        float grown = 1;
        if (age < splat.grows) {
            const float t = static_cast<float>(age) / static_cast<float>(splat.grows);
            grown = 1 - ((1 - t) * (1 - t));
        }
        const float spread = std::max(1.5F, static_cast<float>(splat.spread) * grown);
        /* DL_ItemNode::draw's place of a point, scrolled. */
        const i16 hx = static_cast<i16>(splat.x >> 1);
        const i16 hy = static_cast<i16>(splat.y >> 1);
        const int sx = hx - hy - scrollX;
        const int sy = (hx >> 1) + (hy >> 1) - splat.z - scrollY;
        /* A map circle is an ellipse on the screen: as far as 0.71 of it
         * across and 0.36 of it up and down. */
        const int across = static_cast<int>(spread * 0.75F) + 1;
        const int down = static_cast<int>(spread * 0.38F) + 1;
        const int dryness = splat.dryness;
        const bool shines = splat.pool != 0U && dryness == 0;
        for (int dy = -down; dy <= down; dy++) {
            for (int dx = -across; dx <= across; dx++) {
                const int x = sx + dx;
                const int y = sy + dy;
                if (x < clip.left || x > clip.right || y < clip.top || y > clip.bottom) {
                    continue;
                }
                /* The pixel's point on the map, from the splat's middle
                 * (the screen's dx is half of mx - my, its dy a quarter of
                 * mx + my). */
                const int mx = dx + (2 * dy);
                const int my = (2 * dy) - dx;
                const int mapX = splat.x + mx;
                const int mapY = splat.y + my;
                if (mapX > floorRight || mapX <= floorLeft || mapY > floorBottom || mapY <= floorTop) {
                    continue;
                }
                const auto distance = static_cast<float>((mx * mx) + (my * my));
                const float place = edgePlace(mx, my);
                const int at = static_cast<int>(place);
                const float between = place - static_cast<float>(at);
                const float edge = ((splat.edge[at % EDGE_POINTS] * (1 - between)) +
                                    (splat.edge[(at + 1) % EDGE_POINTS] * between)) /
                                   255.0F * spread;
                if (distance > edge * edge) {
                    continue;
                }
                /* How deep in it the pixel is, 0 at the edge to 1 at the
                 * middle (its square's, near enough). */
                const float deep = 1 - (distance / (edge * edge));
                Tone tone = BODY;
                if (deep < 0.2F) {
                    tone = RIM;
                } else if (splat.pool != 0U && deep > 0.55F) {
                    tone = DEEP;
                }
                /* A fresh pool's shine: up and left of its middle, the
                 * light's side. */
                if (shines) {
                    const float shineX = static_cast<float>(mx) + (spread * 0.3F);
                    const float shineY = static_cast<float>(my) + (spread * 0.3F);
                    if ((shineX * shineX) + (shineY * shineY) < spread * spread * 0.02F) {
                        tone = SHINE;
                    }
                }
                u8 *pixel = view.address(static_cast<s16>(x), static_cast<s16>(y));
                const u8 under = *pixel;
                *pixel = reddenedOf(under, dryness, tone);
                /* With the hardware scene, the reddening exactly (colours.h). */
                keepExact(view, pixel, *pixel, reddenedExactly(under, dryness, tone));
            }
        }
    }
}
