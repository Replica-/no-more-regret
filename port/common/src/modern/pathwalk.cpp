/* The pointer mode's walk. See pathwalk.h. */
#include "pathwalk.h"
#include "gamestate.h"
#include "animprim.h"
#include "camera.h"
#include "collide.h"
#include "control.h"
#include "distance.h"
#include "hud.h"
#include "itemdata.h"
#include "keyboard.h"
#include "npc.h"
#include "pointer.h"
#include "systimer.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace {

/* The walk, the arena's: on, to the pointer while held, running, the item
 * it goes to (0 a point); the goal (the Silencer's place, not his centre);
 * the nearest he has come to it, where the last step started and the steps
 * in a row that went nowhere, the directions that failed from there; a
 * wait before trying again; the stand after a walk the forward key let go;
 * the last plan's points, for the overlay. */
const int PATH_POINTS = 24;
struct Walk {
    u8 active;
    u8 hold;
    u8 run;
    u8 pendingStand;
    u16 item;
    i16 goalX;
    i16 goalY;
    u8 goalZ;
    i16 best;
    i16 lastX;
    i16 lastY;
    u8 stuck;
    u16 badDirections;
    std::int8_t stepDirection;
    u32 waitUntil;
    u8 count;
    ItemPoint points[PATH_POINTS];
    /* The point clicked (walkMark) and when; markAt 0 for none. */
    ItemPoint mark;
    u32 markAt;
};
Walk &walk = gameGlobal<Walk>();

/* The grid's cell, its cells each way from the start and the most nodes a
 * plan looks at. */
const int CELL = 16;
const int RADIUS = 48;
const int SIDE = (2 * RADIUS) + 1;
const int MOST_EXPANDED = 1500;
const int HEAP = MOST_EXPANDED * 8;
/* Arrived within this of the goal. */
const i16 ARRIVED = 20;

/* The search's nodes: scratch outside the arena, written anew by each plan
 * (a rewind does not need them). */
struct Node {
    std::int32_t g;
    i16 parent;
    u8 z;
    u8 closed;
    u16 plan;
};
Node nodes[SIDE * SIDE];
u16 planNumber = 0;
struct HeapEntry {
    std::int32_t f;
    i16 node;
};
HeapEntry heap[HEAP];
int heapSize = 0;

void heapPush(std::int32_t f, i16 node)
{
    if (heapSize == HEAP) {
        return;
    }
    int i = heapSize++;
    while (i > 0) {
        const int parent = (i - 1) / 2;
        if (heap[parent].f <= f) {
            break;
        }
        heap[i] = heap[parent];
        i = parent;
    }
    heap[i] = {f, node};
}

HeapEntry heapPop()
{
    const HeapEntry top = heap[0];
    const HeapEntry last = heap[--heapSize];
    int i = 0;
    for (;;) {
        int child = (2 * i) + 1;
        if (child >= heapSize) {
            break;
        }
        if (child + 1 < heapSize && heap[child + 1].f < heap[child].f) {
            child++;
        }
        if (heap[child].f >= last.f) {
            break;
        }
        heap[i] = heap[child];
        i = child;
    }
    heap[i] = last;
    return top;
}

/* Octile distance in tenths of a cell. */
std::int32_t octile(int dx, int dy)
{
    dx = std::abs(dx);
    dy = std::abs(dy);
    return (10 * std::max(dx, dy)) + (4 * std::min(dx, dy));
}

/* The walk's own move of shape from from to to (x and y; z found on the
 * way): kept to the floor, up a step of 8 or 9, down one of 9. */
bool walkMove(u16 shape, u16 npc, const ItemPoint &from, ItemPoint &to)
{
    HitSweep sweep;
    ItemPoint start = from;
    ItemPoint end = to;
    end.z = from.z;
    if (!sweep.step(shape, start, end, MOVE_STANDS | MOVE_CLIMB, npc, 0)) {
        return false;
    }
    if (sweep.end.x != end.x || sweep.end.y != end.y) {
        return false;
    }
    to.z = sweep.end.z;
    return true;
}

/* A unit of each of the 16 directions (fineDirection's), in 256ths: the
 * middle of the angles that take each, by the game's sines (hud.h). */
struct Unit {
    i16 x;
    i16 y;
};
const Unit &directionUnit(u8 direction)
{
    static Unit units[16];
    static bool made = false;
    if (!made) {
        /* Each direction's first and count of angles, from one where the
         * direction changes so that none wraps round. */
        int first[16] = {};
        int count[16] = {};
        u8 directions[360];
        for (int degree = 0; degree < 360; degree++) {
            i16 x = 0;
            i16 y = 0;
            alongAngle(static_cast<i16>(degree), 1000, &x, &y);
            directions[degree] = static_cast<u8>(fineDirection(0, 0, x, y) & 0xf);
        }
        int origin = 0;
        while (origin < 359 && directions[origin] == directions[(origin + 359) % 360]) {
            origin++;
        }
        for (int i = 0; i < 360; i++) {
            const int degree = (origin + i) % 360;
            const u8 d = directions[degree];
            if (count[d] == 0) {
                first[d] = origin + i;
            }
            count[d]++;
        }
        for (int d = 0; d < 16; d++) {
            i16 x = 0;
            i16 y = 0;
            alongAngle(static_cast<i16>((first[d] + (count[d] / 2)) % 360), 256, &x, &y);
            units[d] = {x, y};
        }
        made = true;
    }
    return units[direction & 0xf];
}

/* A whole stride of length in direction is free from from, 8 units at a
 * time as the animation's frames go. */
bool strideFits(u16 shape, u16 npc, const ItemPoint &from, u8 direction, int length)
{
    const Unit &unit = directionUnit(direction);
    ItemPoint here = from;
    for (int done = 8; done <= length; done += 8) {
        ItemPoint next(static_cast<u16>(from.x + ((unit.x * done) / 256)),
                       static_cast<u16>(from.y + ((unit.y * done) / 256)), here.z);
        if (!walkMove(shape, npc, here, next)) {
            return false;
        }
        here = next;
    }
    return true;
}

/* The plan from from to the goal: the points into walk.points (the start
 * left out); whether it reaches the goal (else the nearest it came). */
bool plan(u16 shape, u16 npc, const ItemPoint &from)
{
    planNumber++;
    if (planNumber == 0) {
        for (Node &node : nodes) {
            node.plan = 0;
        }
        planNumber = 1;
    }
    const auto index = [](int cx, int cy) { return static_cast<i16>(((cy + RADIUS) * SIDE) + cx + RADIUS); };
    const int gx = (walk.goalX - static_cast<i16>(from.x)) / CELL;
    const int gy = (walk.goalY - static_cast<i16>(from.y)) / CELL;
    const auto heuristic = [&](int cx, int cy) { return octile(gx - cx, gy - cy) * 12 / 10; };
    const auto node = [&](i16 at) -> Node & {
        Node &n = nodes[at];
        if (n.plan != planNumber) {
            n = {1 << 30, -1, 0, 0, planNumber};
        }
        return n;
    };
    heapSize = 0;
    const i16 start = index(0, 0);
    node(start) = {0, -1, from.z, 0, planNumber};
    heapPush(heuristic(0, 0), start);
    i16 best = start;
    std::int32_t bestH = heuristic(0, 0);
    i16 found = -1;
    int expanded = 0;
    while (heapSize > 0 && expanded < MOST_EXPANDED) {
        const i16 current = heapPop().node;
        Node &here = node(current);
        if (here.closed != 0U) {
            continue;
        }
        here.closed = 1;
        expanded++;
        const int cx = (current % SIDE) - RADIUS;
        const int cy = (current / SIDE) - RADIUS;
        const std::int32_t h = heuristic(cx, cy);
        if (h < bestH) {
            best = current;
            bestH = h;
        }
        if (std::max(std::abs(gx - cx), std::abs(gy - cy)) <= 1) {
            found = current;
            break;
        }
        const ItemPoint at(static_cast<u16>(from.x + (cx * CELL)), static_cast<u16>(from.y + (cy * CELL)), here.z);
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int nx = cx + dx;
                const int ny = cy + dy;
                if (std::abs(nx) > RADIUS || std::abs(ny) > RADIUS) {
                    continue;
                }
                const i16 neighbour = index(nx, ny);
                Node &next = node(neighbour);
                const std::int32_t g = here.g + ((dx != 0 && dy != 0) ? 14 : 10);
                if (next.closed != 0U || g >= next.g) {
                    continue;
                }
                ItemPoint to(static_cast<u16>(from.x + (nx * CELL)), static_cast<u16>(from.y + (ny * CELL)), here.z);
                if (!walkMove(shape, npc, at, to)) {
                    continue;
                }
                next.g = g;
                next.parent = current;
                next.z = to.z;
                heapPush(g + heuristic(nx, ny), neighbour);
            }
        }
    }
    /* The points back from the end, then turned round. */
    i16 at = found >= 0 ? found : best;
    ItemPoint reversed[PATH_POINTS * 4];
    int count = 0;
    while (at != start && at >= 0 && count < static_cast<int>(std::size(reversed))) {
        const int cx = (at % SIDE) - RADIUS;
        const int cy = (at / SIDE) - RADIUS;
        reversed[count++] = ItemPoint(static_cast<u16>(from.x + (cx * CELL)), static_cast<u16>(from.y + (cy * CELL)),
                                      nodes[at].z);
        at = nodes[at].parent;
    }
    walk.count = 0;
    for (int i = count - 1; i >= 0 && walk.count < PATH_POINTS; i--) {
        walk.points[walk.count++] = reversed[i];
    }
    return found >= 0;
}

/* The point under screen point x,y at the Silencer's height, as his place
 * (not his centre). */
void goalAtScreen(i16 x, i16 y)
{
    Npc npc(controlledActor);
    u16 mapX;
    u16 mapY;
    screenToMap(x, y, &mapX, &mapY);
    const i16 lift = static_cast<i16>(itemZ[npc.id] * 2);
    walk.goalX = static_cast<i16>(mapX + lift + (itemX[npc.id] - npc.centreX()));
    walk.goalY = static_cast<i16>(mapY + lift + (itemY[npc.id] - npc.centreY()));
    walk.goalZ = itemZ[npc.id];
}

void start()
{
    Npc npc(controlledActor);
    walk.active = 1;
    walk.stuck = 0;
    walk.badDirections = 0;
    walk.stepDirection = -1;
    walk.waitUntil = 0;
    walk.best = 0x7fff;
    walk.lastX = static_cast<i16>(itemX[npc.id]);
    walk.lastY = static_cast<i16>(itemY[npc.id]);
    walk.count = 0;
    walk.pendingStand = 0;
}

bool isRunning(u16 anim)
{
    return anim == ANIM_RUN || anim == ANIM_COMBAT_RUN_SMALL || anim == ANIM_COMBAT_RUN_LARGE ||
           anim == ANIM_JUMP_FORWARD;
}

bool isStartingRun(u16 anim)
{
    return anim == ANIM_START_RUN || anim == ANIM_START_RUN_SMALL || anim == ANIM_START_RUN_LARGE;
}

/* The run's end, when he runs. */
void stopRunning(Npc &npc)
{
    const u16 last = npc.lastAnimSet();
    if (isRunning(last) || isStartingRun(last)) {
        npc.doAnim(ANIM_STOP_RUN_SMALL, DIRECTION_CURRENT, ONLY_IF_IDLE, 0);
    }
}

/* A held walk waits a little before trying again; any other ends. */
void blocked(u32 ticks)
{
    if (walk.hold != 0U) {
        walk.waitUntil = systemTimer->time() + ticks;
    } else {
        walk.active = 0;
        walk.count = 0;
    }
}

}

void walkToScreen(i16 x, i16 y, bool run)
{
    start();
    walk.item = 0;
    walk.run = run ? 1 : 0;
    goalAtScreen(x, y);
    /* The point clicked, at his height: the goal less his place's offset
     * from his centre. */
    Npc npc(controlledActor);
    walk.mark = ItemPoint(static_cast<u16>(walk.goalX - (itemX[npc.id] - npc.centreX())),
                          static_cast<u16>(walk.goalY - (itemY[npc.id] - npc.centreY())), walk.goalZ);
    walk.markAt = std::max<u32>(1, systemTimer->time());
}

void walkToItem(u16 item, bool run)
{
    const Item it(item);
    Npc npc(controlledActor);
    start();
    walk.item = item;
    walk.run = run ? 1 : 0;
    walk.goalX = static_cast<i16>(it.centreX() + (itemX[npc.id] - npc.centreX()));
    walk.goalY = static_cast<i16>(it.centreY() + (itemY[npc.id] - npc.centreY()));
    walk.goalZ = itemZ[npc.id];
}

void walkHold(bool held)
{
    if (held) {
        if (walk.hold == 0U) {
            start();
            walk.item = 0;
            walk.run = 0;
            walk.hold = 1;
            goalAtScreen(pointerScreenX(), pointerScreenY());
        }
        return;
    }
    if (walk.hold != 0U) {
        walk.hold = 0;
        walk.active = 0;
        walk.count = 0;
        walk.pendingStand = 1;
    }
}

void walkStop()
{
    if (walk.active != 0U && walk.hold == 0U) {
        walk.pendingStand = 1;
    }
    walk.active = 0;
    walk.hold = 0;
    walk.item = 0;
    walk.count = 0;
}

bool walking()
{
    return walk.active != 0U;
}

bool walkMark(ItemPoint *point, u32 *age)
{
    /* Shown this long after the click, longer while he walks there. */
    const u32 SHOWN = 40;
    if (walk.markAt == 0U) {
        return false;
    }
    const u32 since = systemTimer->time() - walk.markAt;
    const bool going = walk.active != 0U && walk.item == 0U && walk.hold == 0U;
    if (!going && since > SHOWN) {
        walk.markAt = 0;
        return false;
    }
    *point = walk.mark;
    *age = since;
    return true;
}

u8 walkPath(const ItemPoint **points)
{
    *points = walk.points;
    return walk.active != 0U ? walk.count : 0;
}

void walkTick()
{
    Npc npc(controlledActor);
    if (walk.pendingStand != 0U && !npc.isBusy()) {
        walk.pendingStand = 0;
        stopRunning(npc);
    }
    if (walk.active == 0U) {
        return;
    }
    if (walk.item != 0U && pointerItemInReach(walk.item)) {
        const u16 item = walk.item;
        walkStop();
        pointerUse(item);
        return;
    }
    if (npc.isBusy()) {
        return;
    }
    if (walk.hold != 0U) {
        goalAtScreen(pointerScreenX(), pointerScreenY());
        if (systemTimer->time() < walk.waitUntil) {
            return;
        }
    }
    const ItemPoint here(itemX[npc.id], itemY[npc.id], itemZ[npc.id]);
    const auto distance = [](i16 x, i16 y) {
        return std::max(std::abs(x - walk.goalX), std::abs(y - walk.goalY));
    };
    const i16 left = static_cast<i16>(distance(static_cast<i16>(here.x), static_cast<i16>(here.y)));
    if (left <= ARRIVED) {
        stopRunning(npc);
        if (walk.hold == 0U) {
            walk.active = 0;
            walk.count = 0;
        }
        return;
    }
    /* Steps that went nowhere: their directions are not tried again from
     * here; three in a row end the walk (a held one waits). */
    if (std::max(std::abs(static_cast<i16>(here.x) - walk.lastX), std::abs(static_cast<i16>(here.y) - walk.lastY)) <
        2) {
        walk.stuck++;
        if (walk.stepDirection >= 0) {
            walk.badDirections |= static_cast<u16>(1U << walk.stepDirection);
        }
    } else {
        walk.stuck = 0;
        walk.badDirections = 0;
    }
    walk.lastX = static_cast<i16>(here.x);
    walk.lastY = static_cast<i16>(here.y);
    if (walk.stuck >= 3) {
        walk.stuck = 0;
        blocked(8);
        return;
    }
    walk.best = std::min(walk.best, left);
    const u16 shape = npc.shape();
    const bool reaches = plan(shape, npc.id, here);
    if (walk.count == 0) {
        blocked(8);
        return;
    }
    const ItemPoint &last = walk.points[walk.count - 1];
    if (!reaches && distance(static_cast<i16>(last.x), static_cast<i16>(last.y)) >= walk.best - 8) {
        /* As near as it gets: a plan that leads no nearer than he has been
         * would only take him to and fro. */
        blocked(12);
        return;
    }
    const u16 lastAnim = npc.lastAnimSet();
    const bool running = isRunning(lastAnim) || isStartingRun(lastAnim);
    const bool wantRun = walk.run != 0U || actionHeld(Action::RUN) != 0U;
    const bool armed = npc.isWeaponDrawn() != 0U;
    /* The steps to try, best first, and the length of their strides. */
    struct Step {
        u16 anim;
        int length;
    };
    Step steps[3];
    int stepCount = 0;
    if (wantRun) {
        steps[stepCount++] = {running ? static_cast<u16>(armed ? ANIM_COMBAT_RUN_SMALL : ANIM_RUN)
                                      : static_cast<u16>(armed ? ANIM_START_RUN_SMALL : ANIM_START_RUN),
                              64};
    }
    steps[stepCount++] = {static_cast<u16>(armed ? ANIM_ADVANCE_SMALL : ANIM_WALK), 32};
    if (armed) {
        steps[stepCount++] = {ANIM_WALK, 32};
    }
    for (int s = 0; s < stepCount; s++) {
        for (int i = std::min(static_cast<int>(walk.count) - 1, 5); i >= 0; i--) {
            const ItemPoint &towards = walk.points[i];
            const u8 straight = static_cast<u8>(
                fineDirection(static_cast<i16>(here.x), static_cast<i16>(here.y), static_cast<i16>(towards.x),
                              static_cast<i16>(towards.y)) &
                0xf);
            for (const int offset : {0, 1, -1, 2, -2, 3, -3}) {
                if ((offset > 1 || offset < -1) && walk.badDirections == 0U) {
                    break;
                }
                const u8 direction = static_cast<u8>((straight + offset) & 0xf);
                if ((walk.badDirections & (1U << direction)) != 0U) {
                    continue;
                }
                /* Not past the goal: the stride no longer than what is
                 * left, a step at least. */
                const int length = std::clamp(static_cast<int>(left), 16, steps[s].length);
                if (strideFits(shape, npc.id, here, direction, length)) {
                    walk.stepDirection = static_cast<std::int8_t>(direction);
                    npc.doAnim(steps[s].anim, direction, ONLY_IF_IDLE, 0);
                    return;
                }
            }
        }
    }
    walk.stuck++;
    blocked(6);
}
