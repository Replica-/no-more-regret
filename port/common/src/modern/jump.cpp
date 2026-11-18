/* The pointer mode's targeted jump. See jump.h. */
#include "jump.h"
#include "gamestate.h"
#include "animcach.h"
#include "animfrm.h"
#include "animprim.h"
#include "astream.h"
#include "camera.h"
#include "collide.h"
#include "control.h"
#include "gravity.h"
#include "hud.h"
#include "itemdata.h"
#include "kernel.h"
#include "npc.h"
#include "overlay.h"
#include "pathwalk.h"
#include "pfloat.h"
#include "pointer.h"
#include "systimer.h"

#include <algorithm>
#include <cstdlib>

namespace {

/* The key held, the arena's: down, and since when; the flight under way,
 * since when, its steps and its direction. */
struct Held {
    u8 down;
    u32 downAt;
    u8 flying;
    u32 flyingAt;
    i16 steps;
    u8 direction;
};
Held &held = gameGlobal<Held>();

/* Ticks the key is held before the jump is a targeted one; the farthest
 * landing; how far above and below the Silencer a landing is looked for; the
 * fall's pull (hurl's, as a fall off a ledge). */
const u32 HOLD = 12;
const i16 RANGE = 256;
const i16 ABOVE = 24;
const i16 BELOW = 160;
const i16 GRAVITY = 4;
/* Ticks a step of the fall takes (GravityProcess::run's timeout). */
const u32 STEP_TICKS = 4;

const std::uint32_t ARC = 0x96f0ffeb;
const std::uint32_t ARC_SHADOW = 0x0000005a;
const std::uint32_t RING = 0x78e6ffc8;

/* A flight: thrown at speed x,y,z, it lands at to after steps steps of the
 * fall. */
struct Flight {
    ItemPoint from;
    ItemPoint to;
    i16 speedX;
    i16 speedY;
    i16 speedZ;
    i16 steps;
};

u32 now()
{
    return systemTimer->time();
}

i16 floorDivide(std::int32_t a, std::int32_t b)
{
    std::int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q--;
    }
    return static_cast<i16>(q);
}

/* The point of the flight after step steps of the fall (GravityProcess::run:
 * the speed added, then the pull taken from it). */
void flightPoint(const Flight &flight, std::int32_t step, std::int32_t *x, std::int32_t *y, std::int32_t *z)
{
    *x = flight.from.x + (flight.speedX * step);
    *y = flight.from.y + (flight.speedY * step);
    *z = flight.from.z + (flight.speedZ * step) - (GRAVITY * step * (step - 1) / 2);
}

/* The Silencer's shape stands at x,y,z: room for it, and a floor under it. */
bool stands(u16 shape, u16 npc, std::int32_t x, std::int32_t y, std::int32_t z, u8 close)
{
    if (z < 0 || z > 250) {
        return false;
    }
    u16 blocker = 0;
    if (!shapeFits(shape, static_cast<u16>(x), static_cast<u16>(y), static_cast<u16>(z), 0, npc, 0, &blocker)) {
        return false;
    }
    Support support;
    testSupport(&support, static_cast<i16>(shape), ItemPoint(static_cast<u16>(x), static_cast<u16>(y),
                                                             static_cast<u8>(z)),
                close, npc);
    return support.standing() != 0U;
}

/* Nothing is in the way of the flight; its last step may come down on the
 * floor short of the end. */
bool clear(u16 shape, u16 npc, const Flight &flight)
{
    ItemPoint previous = flight.from;
    for (i16 step = 1; step <= flight.steps; step++) {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        flightPoint(flight, step, &x, &y, &z);
        if (z < 0 || z > 250) {
            return false;
        }
        ItemPoint next(static_cast<u16>(x), static_cast<u16>(y), static_cast<u8>(z));
        Sweep sweep;
        if (!sweep.test(shape, previous, next, MOVE_NO_FLOOR, npc, 0)) {
            if (step < flight.steps || (sweep.blockedAxes & 3) != 0) {
                return false;
            }
        }
        previous = next;
    }
    return true;
}

/* The flight to the place under the pointer: from there back towards the
 * Silencer, the first place he stands on that the flight reaches. */
bool planFlight(Flight *flight)
{
    Npc npc(controlledActor);
    const u16 shape = npc.shape();
    u16 mapX;
    u16 mapY;
    screenToMap(pointerScreenX(), pointerScreenY(), &mapX, &mapY);
    /* At his height, his place for his centre (pathwalk.cpp's goal). */
    const i16 lift = static_cast<i16>(itemZ[npc.id] * 2);
    const i16 dx = static_cast<i16>(mapX + lift + (itemX[npc.id] - npc.centreX()) - itemX[npc.id]);
    const i16 dy = static_cast<i16>(mapY + lift + (itemY[npc.id] - npc.centreY()) - itemY[npc.id]);
    const i16 distance = std::max(std::abs(dx), std::abs(dy));
    if (distance < 8) {
        return false;
    }
    const i16 reach = std::min(distance, RANGE);
    flight->from = ItemPoint(itemX[npc.id], itemY[npc.id], itemZ[npc.id]);
    for (i16 back = 0; back <= reach - 16; back += 8) {
        const i16 along = static_cast<i16>(reach - back);
        flight->steps = static_cast<i16>(std::clamp(5 + (along / 48), 5, 10));
        flight->speedX = static_cast<i16>((static_cast<std::int32_t>(dx) * along / distance) / flight->steps);
        flight->speedY = static_cast<i16>((static_cast<std::int32_t>(dy) * along / distance) / flight->steps);
        const std::int32_t x = flight->from.x + (flight->speedX * flight->steps);
        const std::int32_t y = flight->from.y + (flight->speedY * flight->steps);
        /* The floor there, from a little above him down two storeys: the
         * first place with room and something under it, then down onto
         * it. */
        for (std::int32_t z = flight->from.z + ABOVE; z >= std::max(0, flight->from.z - BELOW); z -= 4) {
            if (!stands(shape, npc.id, x, y, z, 1)) {
                continue;
            }
            std::int32_t ground = z;
            for (i16 down = 0; down <= 9 && z - down >= 0; down++) {
                if (stands(shape, npc.id, x, y, z - down, 0)) {
                    ground = z - down;
                    break;
                }
            }
            flight->to = ItemPoint(static_cast<u16>(x), static_cast<u16>(y), static_cast<u8>(ground));
            const std::int32_t pull = GRAVITY * flight->steps * (flight->steps - 1) / 2;
            flight->speedZ = floorDivide(ground - flight->from.z + pull, flight->steps);
            if (clear(shape, npc.id, *flight)) {
                return true;
            }
        }
    }
    return false;
}

/* The Silencer may jump now. */
bool mayJump()
{
    Npc npc(controlledActor);
    return pointerMode() && npc.isValid() && npc.isDead() == 0U && npc.isSilencer() &&
           firstProcess(npc.id, GRAVITY_TYPE) == nullptr;
}

bool targeted()
{
    return held.down != 0U && now() - held.downAt >= HOLD;
}

void leap(const Flight &flight)
{
    Npc npc(controlledActor);
    walkStop();
    const char toward = npc.directionTo(flight.to.x, flight.to.y);
    killItemProcesses(npc.id, ANIM_TYPE, Priority(PRIORITY_EQUAL | BASE_LEVEL));
    npc.setDirection(static_cast<u8>(toward));
    hurl(&npc, flight.speedX, flight.speedY, flight.speedZ, GRAVITY);
    held.flying = 1;
    held.flyingAt = now();
    held.steps = flight.steps;
    held.direction = static_cast<u8>(toward);
}

/* A ring on the floor at x,y, as the view shows a circle there. */
void ring(float x, float y)
{
    const int segments = 28;
    for (int i = 0; i < segments; i++) {
        float x0 = x;
        float y0 = y;
        float x1 = x;
        float y1 = y;
        PFloat a0(static_cast<i16>(360 * i / segments), 0);
        PFloat a1(static_cast<i16>(360 * (i + 1) / segments), 0);
        x0 += 15 * static_cast<float>(cosine(a0).value) / 65536;
        y0 += 7.5F * static_cast<float>(sine(a0).value) / 65536;
        x1 += 15 * static_cast<float>(cosine(a1).value) / 65536;
        y1 += 7.5F * static_cast<float>(sine(a1).value) / 65536;
        overlayLine(x0, y0, x1, y1, 1.6F, RING);
    }
}

}

void jumpTick()
{
    if (held.flying == 0U) {
        return;
    }
    Npc npc(controlledActor);
    if (!npc.isValid() || npc.isDead() != 0U) {
        held.flying = 0;
        return;
    }
    if (firstProcess(npc.id, GRAVITY_TYPE) == nullptr) {
        held.flying = 0;
        npc.doAnim(ANIM_JUMP_LANDING, held.direction, EVEN_IF_BUSY, 1);
        return;
    }
    /* The quick jump's frames over the flight, its first (standing) and
     * last (landed) left out. */
    const AnimInfo info = animInfo(npc.shape(), ANIM_QUICK_JUMP);
    const i16 frames = static_cast<i16>(info.frames);
    if (frames < 3) {
        return;
    }
    const u32 length = static_cast<u32>(held.steps) * STEP_TICKS;
    const u32 into = std::min(now() - held.flyingAt, length);
    const i16 index = static_cast<i16>(1 + ((frames - 3) * into / std::max(length, 1U)));
    AnimFrame frame;
    getAnimFrame(npc.shape(), ANIM_QUICK_JUMP, held.direction, static_cast<u16>(index), &frame);
    npc.setFrame(frame.frame);
    if (frame.flipped != 0U) {
        npc.orFlags(ITEM_FLIPPED);
    } else {
        npc.andFlags(static_cast<u16>(~ITEM_FLIPPED));
    }
}

bool jumpKey(bool pressed, u16 repeat)
{
    if (pressed) {
        if (repeat <= 1 && held.down == 0U) {
            held.down = 1;
            held.downAt = now();
        }
        return true;
    }
    if (held.down == 0U) {
        return true;
    }
    const bool aimed = targeted();
    held.down = 0;
    if (!aimed) {
        return false;
    }
    Flight flight;
    if (mayJump() && planFlight(&flight)) {
        leap(flight);
    }
    return true;
}

void drawJump()
{
    if (!targeted() || !mayJump()) {
        return;
    }
    Flight flight;
    if (!planFlight(&flight)) {
        return;
    }
    /* The arc in four pieces a step, dashed: 7 pixels drawn, 5 left out. */
    const int pieces = flight.steps * 4;
    float lastX = 0;
    float lastY = 0;
    float into = 0;
    bool drawn = true;
    for (int i = 0; i <= pieces; i++) {
        /* The fall's points between its steps, on the same curve. */
        const float t = static_cast<float>(i) / 4;
        const float x = flight.from.x + (flight.speedX * t);
        const float y = flight.from.y + (flight.speedY * t);
        const float z = flight.from.z + (flight.speedZ * t) - (GRAVITY * t * (t - 1) / 2);
        float sx;
        float sy;
        mapToOverlay(static_cast<u16>(x), static_cast<u16>(y), static_cast<i16>(std::max(z, 0.0F)), &sx, &sy);
        if (i > 0) {
            const float length = std::max(std::abs(sx - lastX), std::abs(sy - lastY));
            if (drawn) {
                overlayLine(lastX, lastY + 1, sx, sy + 1, 3, ARC_SHADOW);
                overlayLine(lastX, lastY, sx, sy, 2.2F, ARC);
            }
            into += length;
            if (into >= (drawn ? 7.0F : 5.0F)) {
                into = 0;
                drawn = !drawn;
            }
        }
        lastX = sx;
        lastY = sy;
    }
    float x;
    float y;
    Npc npc(controlledActor);
    float ringX;
    float ringY;
    mapToOverlay(static_cast<u16>(flight.to.x + (npc.centreX() - itemX[npc.id])),
                 static_cast<u16>(flight.to.y + (npc.centreY() - itemY[npc.id])), flight.to.z, &ringX, &ringY);
    ring(ringX, ringY);
    /* The Silencer standing there, see-through, breathing. */
    mapToOverlay(flight.to.x, flight.to.y, flight.to.z, &x, &y);
    PFloat phase(static_cast<i16>((now() * 6) % 360), 0);
    const float beat = static_cast<float>(sine(phase).value) / 65536;
    wordsBegin();
    wordsShape(npc.shape(), npc.frame(), npc.isFlipped() != 0U, static_cast<i16>(x), static_cast<i16>(y));
    wordsEnd(static_cast<u8>(120 + (25 * beat)));
}
