/* What the port's enhancements draw over the screen. See hud.h. */
#include "hud.h"
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
#include "shaphand.h"
#include "screen.h"
#include "systimer.h"
#include "wheel.h"
#include "missionmap.h"
#include "type.h"
#include "viewport.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iterator>

namespace {

const std::uint32_t WHITE = 0xf0f0f0e0;
const std::uint32_t SHADOW = 0x00000080;
const std::uint32_t IN_REACH = 0x40e060ff;
const std::uint32_t OUT_OF_REACH = 0xe04040ff;
const std::uint32_t PATH = 0xf0d04060;
const std::uint32_t SPREAD = 0xff3030d0;
const std::uint32_t LASER = 0xff2020ff;

/* Ticks a hit NPC's health bar is shown, the last of them fading. */
const u32 BAR_SHOWN = 135;
const u32 BAR_FADE = 30;

/* The target cursor: a thin ring, a dot and four short lines, each over a
 * shadow. */
void targetCursor(float x, float y)
{
    for (int pass = 0; pass < 2; pass++) {
        const std::uint32_t colour = pass == 0 ? SHADOW : WHITE;
        const float width = pass == 0 ? 2.5F : 1.2F;
        overlayRing(x, y, 8, width, colour);
        overlayLine(x - 14, y, x - 10, y, width, colour);
        overlayLine(x + 10, y, x + 14, y, width, colour);
        overlayLine(x, y - 14, x, y - 10, width, colour);
        overlayLine(x, y + 10, x, y + 14, width, colour);
        overlayDisc(x, y, pass == 0 ? 1.8F : 1.1F, colour);
    }
}

/* Where the next shot goes (pointer.h's PointerAim): four red lines round
 * the point aimed at, as far apart as the shots scatter there (a map spread
 * s is s across the screen and s/2 down, the rise above it), and a laser
 * dot on what the shot meets first, with a short beam fading back towards
 * the Silencer. */
void aimMarks()
{
    PointerAim aim;
    if (!pointerAim(&aim)) {
        return;
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
    if (length > 1) {
        /* Forty pixels of beam at most, in steps that fade. */
        const float reach = std::min(length, 40.0F) / length;
        const int steps = 8;
        for (int i = 0; i < steps; i++) {
            const float a = reach * static_cast<float>(i) / steps;
            const float b = reach * static_cast<float>(i + 1) / steps;
            const auto alpha = static_cast<std::uint32_t>(0xa0 * (steps - i) / steps);
            overlayLine(stopX + (dx * a), stopY + (dy * a), stopX + (dx * b), stopY + (dy * b), 1.2F,
                        (LASER & 0xffffff00U) | alpha);
        }
    }
    overlayDisc(stopX, stopY, aim.blocked ? 2.6F : 2.0F, SHADOW);
    overlayDisc(stopX, stopY, aim.blocked ? 1.9F : 1.4F, LASER);
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

void path()
{
    const ItemPoint *points = nullptr;
    const u8 count = walkPath(&points);
    for (u8 i = 0; i < count; i++) {
        float x;
        float y;
        mapToOverlay(points[i].x, points[i].y, points[i].z, &x, &y);
        overlayDisc(x, y, 1.5F, PATH);
    }
}

void drawHud()
{
    if (worldActive != 0U) {
        noteSeen();
    }
    if (drawWheel() || drawMissionMap()) {
        return;
    }
    if (worldActive != 0U) {
        healthBars();
        drawPopups();
    }
    if (!pointerMode()) {
        return;
    }
    path();
    drawJump();
    const float x = static_cast<float>(pointerScreenX());
    const float y = static_cast<float>(pointerScreenY());
    Npc npc(controlledActor);
    if (npc.isWeaponDrawn() != 0U) {
        aimMarks();
        targetCursor(x, y);
        return;
    }
    const u16 item = pointerItemUnder(pointerScreenX(), pointerScreenY());
    if (item != 0U) {
        overlayDisc(x + 9, y + 9, 3.5F, SHADOW);
        overlayDisc(x + 9, y + 9, 2.5F, pointerItemInReach(item) ? IN_REACH : OUT_OF_REACH);
    }
}

/* wordsBegin's pixels, and the picture wordsEnd makes of them. */
constexpr std::size_t PIXELS = static_cast<std::size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT;
u8 canvas[PIXELS];
std::uint32_t picture[PIXELS];

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
    if (id < 2 || id >= NPCS || after >= before) {
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
