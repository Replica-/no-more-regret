/* What the port's enhancements draw over the screen. See hud.h. */
#include "hud.h"
#include "effects.h"
#include "enhancements.h"
#include "gamestate.h"
#include "camera.h"
#include "cafont.h"
#include "control.h"
#include "crusys.h"
#include "gamepal.h"
#include "gumpshap.h"
#include "itemdata.h"
#include "itemutil.h"
#include "npc.h"
#include "overlay.h"
#include "pathwalk.h"
#include "pfloat.h"
#include "pointer.h"
#include "jump.h"
#include "popups.h"
#include "blood.h"
#include "lighting.h"
#include "colours.h"
#include "death.h"
#include "shaphand.h"
#include "screen.h"
#include "systimer.h"
#include "wheel.h"
#include "mousectl.h"
#include "world.h"
#include "missionmap.h"
#include "type.h"
#include "viewport.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iterator>

namespace {

const std::uint32_t SHADOW = 0x00000080;
const std::uint32_t IN_REACH = 0x40e060ff;
const std::uint32_t OUT_OF_REACH = 0xe04040ff;
const std::uint32_t SPREAD = 0xff3030d0;
const std::uint32_t LASER = 0xff2020ff;
/* The laser is not drawn where it meets something this near the Silencer
 * on the screen (pixels from his middle across, half as much up and down,
 * he being tall), his frames' angles not those of a beam so short; it fades
 * in over the next LASER_FADE. */
const float LASER_NEAREST = 36;
const float LASER_FADE = 16;

/* Ticks a hit NPC's health bar is shown, the last of them fading. */
const u32 BAR_SHOWN = 135;
const u32 BAR_FADE = 30;

const std::uint32_t TARGET_RED = 0xff2828ff;
/* The reticle when the shot would hit (PointerAim::hits). */
const std::uint32_t TARGET_GREEN = 0x30f050ff;
const std::uint32_t RIM = 0x000000aa;
const std::uint32_t OUTLINE = 0x000000d7;
const std::uint32_t ARROW_FILL = 0xfafafaff;
/* The game's reticle, which the target cursor is as big as (2.2 times its
 * picture). */
const u16 RETICLE_SHAPE = 0x4cc;

/* A dot over a dark rim of 1.4 more: whether the item under the pointer is
 * in reach (green) or not (red). */
void reachBadge(float x, float y, float radius, bool inReach)
{
    overlayDisc(x, y, radius + 1.4F, OUTLINE);
    overlayDisc(x, y, radius, inReach ? IN_REACH : OUT_OF_REACH);
}

/* The target cursor with the weapon out: a thin red ring, a dot and four
 * short lines out from the ring, over a dark rim; the reach's dot at its
 * lower right over an item (badge 0 none, 1 in reach, 2 not). */
void targetCursor(float x, float y, int badge, bool hits)
{
    const std::uint32_t colour = hits ? TARGET_GREEN : TARGET_RED;
    float size = 30;
    if (ShapeFrame *frame = cachedShapeFrame(RETICLE_SHAPE, 0)) {
        size = static_cast<float>(std::max(frame->width, frame->height)) * 2.2F;
    }
    const float outer = size * 0.5F;
    const float ring = outer * 0.56F;
    const float thick = std::max(0.8F, outer * 0.04F);
    const float tick = std::max(1.0F, outer * 0.055F);
    overlayRing(x, y, ring, (2 * thick) + 1.4F, RIM);
    overlayRing(x, y, ring, 2 * thick, colour);
    const float ends[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto &end : ends) {
        const float x0 = x + (end[0] * (ring + thick));
        const float y0 = y + (end[1] * (ring + thick));
        overlayLine(x0, y0, x + (end[0] * outer), y + (end[1] * outer), tick + 1.4F, RIM);
        overlayLine(x0, y0, x + (end[0] * outer), y + (end[1] * outer), tick, colour);
    }
    const float dot = std::max(1.5F, outer * 0.09F);
    overlayDisc(x, y, dot + 0.7F, RIM);
    overlayDisc(x, y, dot, colour);
    if (badge != 0) {
        reachBadge(x + (outer * 0.95F), y + (outer * 0.95F), std::max(3.8F, outer * 0.2F), badge == 1);
    }
}

/* The pointer with the weapon put away (the game hides its own over the
 * world): a white arrow, its point at x,y, outlined in black; the reach's
 * dot at its lower right over an item. */
void arrowCursor(float x, float y, int badge)
{
    const float SCALE = 1.15F;
    /* The point, the left foot, the inner notch, the tail's two corners,
     * the notch, the right wing; and its triangles. */
    const float shape[7][2] = {{0, 0}, {0, 16}, {4.4F, 12.2F}, {7.4F, 18.6F}, {10, 17.4F}, {7, 11}, {12, 11}};
    const int triangles[5][3] = {{0, 1, 2}, {0, 2, 5}, {0, 5, 6}, {2, 3, 4}, {2, 4, 5}};
    const auto polygon = [&](float ox, float oy, std::uint32_t colour) {
        for (const auto &t : triangles) {
            overlayTriangle(x + ox + (shape[t[0]][0] * SCALE), y + oy + (shape[t[0]][1] * SCALE),
                            x + ox + (shape[t[1]][0] * SCALE), y + oy + (shape[t[1]][1] * SCALE),
                            x + ox + (shape[t[2]][0] * SCALE), y + oy + (shape[t[2]][1] * SCALE), colour);
        }
    };
    const float outline[6][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}, {-1, -1}};
    for (const auto &o : outline) {
        polygon(o[0], o[1], OUTLINE);
    }
    polygon(0, 0, ARROW_FILL);
    if (badge != 0) {
        reachBadge(x + 17, y + 18, 3.8F, badge == 1);
    }
}

/* Where the next shot goes (pointer.h's PointerAim): four red lines round
 * the point aimed at, as far apart as the shots scatter there (a map spread
 * s is s across the screen and s/2 down, the rise above it), and a laser
 * dot on what the shot meets first, with a short beam fading back towards
 * the Silencer; neither when it meets something close to him
 * (LASER_NEAREST). */
bool aimMarks()
{
    PointerAim aim;
    if (!pointerAim(&aim)) {
        return false;
    }
    float toX;
    float toY;
    mapToOverlay(aim.to.x, aim.to.y, aim.to.z, &toX, &toY);
    const float across = std::max(4.0F, static_cast<float>(aim.spread));
    const float down = std::max(4.0F, (static_cast<float>(aim.spread) / 2) + static_cast<float>(aim.rise));
    overlayLine(toX - across - 5, toY, toX - across, toY, 1.5F, SPREAD);
    overlayLine(toX + across, toY, toX + across + 5, toY, 1.5F, SPREAD);
    overlayLine(toX, toY - down - 5, toX, toY - down, 1.5F, SPREAD);
    overlayLine(toX, toY + down, toX, toY + down + 5, 1.5F, SPREAD);
    float fromX;
    float fromY;
    float stopX;
    float stopY;
    mapToOverlay(aim.from.x, aim.from.y, aim.from.z, &fromX, &fromY);
    mapToOverlay(aim.stop.x, aim.stop.y, aim.stop.z, &stopX, &stopY);
    const float dx = fromX - stopX;
    const float dy = fromY - stopY;
    const float length = std::max(std::abs(dx), std::abs(dy));
    Npc silencer(controlledActor);
    float middleX;
    float middleY;
    mapToOverlay(silencer.centreX(), silencer.centreY(),
                 static_cast<i16>(silencer.z() + (typeFlags.info(silencer.shape()).z * 4)), &middleX, &middleY);
    const float near = std::max(std::abs(stopX - middleX), std::abs(stopY - middleY) / 2);
    const float shown = std::clamp((near - LASER_NEAREST) / LASER_FADE, 0.0F, 1.0F);
    if (shown <= 0) {
        return aim.hits;
    }
    /* A colour's alpha (its low byte) by how much the laser shows. */
    const auto faded = [shown](std::uint32_t colour) {
        return (colour & 0xffffff00U) | static_cast<std::uint32_t>(static_cast<float>(colour & 0xffU) * shown);
    };
    if (length > 1) {
        /* Sixty-four pixels of beam at most, in steps that fade: a soft
         * glow round a bright core. */
        const float reach = std::min(length, 64.0F) / length;
        const int steps = 10;
        for (int i = 0; i < steps; i++) {
            const float a = reach * static_cast<float>(i) / steps;
            const float b = reach * static_cast<float>(i + 1) / steps;
            const auto fade = static_cast<std::uint32_t>(steps - i);
            overlayLine(stopX + (dx * a), stopY + (dy * a), stopX + (dx * b), stopY + (dy * b), 4.5F,
                        faded((LASER & 0xffffff00U) | (0x48U * fade / steps)));
            overlayLine(stopX + (dx * a), stopY + (dy * a), stopX + (dx * b), stopY + (dy * b), 2.2F,
                        faded((LASER & 0xffffff00U) | (0xe8U * fade / steps)));
        }
    }
    overlayDisc(stopX, stopY, aim.blocked ? 3.2F : 2.6F, faded(SHADOW));
    overlayDisc(stopX, stopY, aim.blocked ? 2.4F : 1.9F, faded(LASER));
    return aim.hits;
}

/* The NPCs hit lately (noteHit): the most hit points each had in its
 * series of hits, and the tick of its last. */
struct HitBar {
    u16 most;
    u32 at;
};
HitBar (&hitBars)[NPCS] = gameGlobal<HitBar[NPCS]>();

/* A slim bar over each NPC hit lately: a faint dark track and its hit
 * points' share of the most, green to red. */
void healthBars()
{
    const u32 now = systemTimer->ticks;
    for (u16 id = 2; id < NPCS; id++) {
        const HitBar &bar = hitBars[id];
        Npc npc(id);
        if (bar.most == 0U || now - bar.at >= BAR_SHOWN || npc.container() != 0U || !npc.isOnScreen()) {
            continue;
        }
        const float share = std::clamp(static_cast<float>(npc.hp()) / static_cast<float>(bar.most), 0.0F, 1.0F);
        const u32 left = BAR_SHOWN - (now - bar.at);
        const float fade = left < BAR_FADE ? static_cast<float>(left) / BAR_FADE : 1.0F;
        const ShapeInfo info = typeFlags.info(npc.shape());
        float x;
        float y;
        mapToOverlay(npc.centreX(), npc.centreY(), static_cast<i16>(npc.z() + (info.z * 8) + 10), &x, &y);
        const float width = 32;
        const float height = 3;
        x -= width / 2;
        overlayBox(x - 1, y - 1, width + 2, height + 2, 0x30000000U | static_cast<std::uint32_t>(170 * fade));
        const auto red = static_cast<std::uint32_t>(std::clamp(2 * (1 - share), 0.0F, 1.0F) * 225);
        const auto green = static_cast<std::uint32_t>((std::clamp(2 * share, 0.0F, 1.0F) * 200) + 25);
        overlayBox(x, y, width * share, height,
                   (red << 24) | (green << 16) | (60U << 8) | static_cast<std::uint32_t>(215 * fade));
    }
}

/* A circle's 16 points, cosine and sine, for the walk's mark. */
const float CIRCLE[16][2] = {{1, 0},        {0.9239F, 0.3827F},   {0.7071F, 0.7071F},   {0.3827F, 0.9239F},
                             {0, 1},        {-0.3827F, 0.9239F},  {-0.7071F, 0.7071F},  {-0.9239F, 0.3827F},
                             {-1, 0},       {-0.9239F, -0.3827F}, {-0.7071F, -0.7071F}, {-0.3827F, -0.9239F},
                             {0, -1},       {0.3827F, -0.9239F},  {0.7071F, -0.7071F},  {0.9239F, -0.3827F}};
const std::uint32_t MARK = 0x50f070U;

/* The point a click walks to (pathwalk.h's walkMark): a ring flat on the
 * floor, closing in as it appears, and a dot, over a shadow, fading at its
 * end. */
void walkMarker()
{
    ItemPoint point;
    u32 age = 0;
    if (!walkMark(&point, &age)) {
        return;
    }
    float x;
    float y;
    mapToOverlay(point.x, point.y, point.z, &x, &y);
    const float closing = age < 10 ? static_cast<float>(10 - age) / 10 : 0;
    const float radius = 9 + (8 * closing);
    const u32 fade = age > 30 && age < 40 ? (40 - age) * 255 / 10 : 255;
    const auto alpha = static_cast<std::uint32_t>(fade);
    for (int pass = 0; pass < 2; pass++) {
        const std::uint32_t colour = pass == 0 ? (alpha * 3 / 4) : ((MARK << 8) | alpha);
        const float width = pass == 0 ? 3.0F : 1.6F;
        for (int i = 0; i < 16; i++) {
            const auto &a = CIRCLE[i];
            const auto &b = CIRCLE[(i + 1) % 16];
            /* Flat on the floor: half as high as wide. */
            overlayLine(x + (a[0] * radius), y + (a[1] * radius / 2), x + (b[0] * radius), y + (b[1] * radius / 2),
                        width, colour);
        }
        overlayDisc(x, y, pass == 0 ? 2.6F : 1.6F, colour);
    }
}


void drawHud()
{
    if (worldActive != 0U) {
        noteSeen();
    }
    if (drawColours()) {
        return;
    }
    /* The port's screens over the game's picture hide the game's pointer:
     * the arrow over them. */
    if (drawWheel() || drawMissionMap() || drawDeath()) {
        drawMenuPointer();
        return;
    }
    drawColoursLayer();
    drawLighting();
    drawBlood();
    drawEffects();
    if (worldActive != 0U) {
        healthBars();
        drawPopups();
    }
    if (!pointerMode()) {
        /* The pointer always shown over the world played (the Silencer
         * held, hit down, dying), the arrow. */
        if (pointerSetting() && worldActive != 0U && worldFrozen == 0U) {
            arrowCursor(static_cast<float>(pointerScreenX()), static_cast<float>(pointerScreenY()), 0);
        }
        return;
    }
    walkMarker();
    drawJump();
    /* A screen over the world (the options, a message) has the game's own
     * pointer. */
    if (worldFrozen != 0U) {
        return;
    }
    const float x = static_cast<float>(pointerScreenX());
    const float y = static_cast<float>(pointerScreenY());
    const u16 item = pointerItemUnder(pointerScreenX(), pointerScreenY());
    const int badge = item == 0U ? 0 : pointerItemInReach(item) ? 1 : 2;
    if (pointerWeaponOut()) {
        const bool hits = aimMarks();
        targetCursor(x, y, badge, hits);
        return;
    }
    arrowCursor(x, y, badge);
}

/* wordsBegin's pixels, and the picture wordsEnd makes of them. */
constexpr std::size_t PIXELS = static_cast<std::size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT;
u8 canvas[PIXELS];
std::uint32_t picture[PIXELS];

}

void drawMenuPointer()
{
    if (mouseCtl != nullptr) {
        /* On the screen, as the gumps' events have it: over the world the
         * pointer's place is the wide frame's (pointer.h's reach). */
        arrowCursor(static_cast<float>(pointerScreenX()), static_cast<float>(pointerScreenY()), 0);
    }
}

void wordsBegin()
{
    std::fill(std::begin(canvas), std::end(canvas), TRANSPARENT);
}

i16 wordsWidth(i16 font, const char *text)
{
    CAFont face(font);
    return face.textWidth(text);
}

void wordsText(i16 font, i16 x, i16 y, const char *text, i16 align)
{
    Viewport view(VIEWPORT_LARGE, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, canvas);
    const DrawTarget target(&view);
    CAFont face(font);
    const i16 width = face.textWidth(text);
    const i16 left = align < 0 ? static_cast<i16>(x - width) : align == 0 ? static_cast<i16>(x - (width / 2)) : x;
    face.draw(left, y, "%s", text);
}

void wordsPicture(u16 shape, u16 frame, i16 x, i16 y)
{
    Viewport view(VIEWPORT_LARGE, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, canvas);
    const DrawTarget target(&view);
    GumpShape icon(shape);
    icon.setFrame(frame);
    const Rect area = icon.dimensions();
    icon.draw(static_cast<u16>(x - ((area.left + area.right) / 2)), static_cast<u16>(y - ((area.top + area.bottom) / 2)));
}

void wordsGump(u16 shape, u16 frame, i16 x, i16 y)
{
    Viewport view(VIEWPORT_LARGE, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, canvas);
    const DrawTarget target(&view);
    GumpShape icon(shape);
    icon.setFrame(frame);
    icon.draw(static_cast<u16>(x), static_cast<u16>(y));
}

void wordsShape(u16 shape, u16 frame, bool mirrored, i16 x, i16 y)
{
    ShapeFrame *image = cachedShapeFrame(shape, frame);
    if (image == nullptr) {
        return;
    }
    Viewport view(VIEWPORT_LARGE, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, canvas);
    if (mirrored) {
        view.drawShapeMirrored(x, y, image);
    } else {
        view.drawShape(x, y, image);
    }
}

void wordsEnd(u8 alpha)
{
    const RGB *colours = gamePalette->data.get();
    for (std::size_t i = 0; i < PIXELS; i++) {
        if (canvas[i] == TRANSPARENT) {
            picture[i] = 0;
            continue;
        }
        /* The palette's 6 bits a colour. */
        const RGB &c = colours[canvas[i]];
        picture[i] = (static_cast<std::uint32_t>(c.red) << 26) | (static_cast<std::uint32_t>(c.green) << 18) |
                     (static_cast<std::uint32_t>(c.blue) << 10) | alpha;
    }
    overlayPicture(picture, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

void startHud()
{
    overlayAction(drawHud);
}

void noteHit(u16 id, u16 before, u16 after)
{
    if (id < 2 || id >= NPCS || after >= before || !pointerSetting()) {
        return;
    }
    HitBar &bar = hitBars[id];
    const u32 now = systemTimer->ticks;
    if (bar.most == 0U || now - bar.at >= BAR_SHOWN) {
        bar.most = before;
    } else {
        bar.most = std::max(bar.most, before);
    }
    bar.at = now;
}

void mapToOverlay(u16 x, u16 y, i16 z, float *screenX, float *screenY)
{
    /* itemutil.h's, the height lifting it. */
    i16 x0;
    i16 y0;
    mapToScreen(x, y, &x0, &y0);
    *screenX = static_cast<float>(x0);
    *screenY = static_cast<float>(y0 - z);
}

i16 angleOf(std::int32_t dx, std::int32_t dy)
{
    i16 best = 0;
    std::int64_t bestScore = INT64_MIN;
    for (i16 degree = 0; degree < 360; degree++) {
        PFloat angle(degree, 0);
        const std::int64_t score = (static_cast<std::int64_t>(dx) * cosine(angle).value) +
                                   (static_cast<std::int64_t>(dy) * sine(angle).value);
        if (score > bestScore) {
            bestScore = score;
            best = degree;
        }
    }
    return best;
}

void alongAngle(i16 angle, i16 length, i16 *x, i16 *y)
{
    PFloat degrees(static_cast<i16>(((angle % 360) + 360) % 360), 0);
    const PFloat across = cosine(degrees) * length;
    const PFloat down = sine(degrees) * length;
    *x = static_cast<i16>(*x + static_cast<i16>(across.value >> 16));
    *y = static_cast<i16>(*y + static_cast<i16>(down.value >> 16));
}
