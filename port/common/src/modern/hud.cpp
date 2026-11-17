/* What the port's enhancements draw over the screen. See hud.h. */
#include "hud.h"
#include "camera.h"
#include "control.h"
#include "itemdata.h"
#include "itemutil.h"
#include "npc.h"
#include "overlay.h"
#include "pathwalk.h"
#include "pfloat.h"
#include "pointer.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace {

const std::uint32_t WHITE = 0xf0f0f0e0;
const std::uint32_t SHADOW = 0x00000080;
const std::uint32_t IN_REACH = 0x40e060ff;
const std::uint32_t OUT_OF_REACH = 0xe04040ff;
const std::uint32_t PATH = 0xf0d04060;
const std::uint32_t SPREAD = 0xff3030d0;
const std::uint32_t LASER = 0xff2020ff;

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
    if (!pointerMode()) {
        return;
    }
    path();
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

}

void startHud()
{
    overlayAction(drawHud);
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
