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
#include "host.h"

#include <cstdarg>
#include <cstdio>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace {

/* The walk, the arena's: on, to the pointer while held, running, the item
 * it goes to (0 a point); the goal (the Silencer's place, not his centre);
 * the nearest he has come to it, where the last step started and the steps
 * in a row that went nowhere, the directions that failed from there; a
 * wait before trying again; the stand after a walk the forward key let go;
 * the plan he follows: its points, the next of them, whether it reaches
 * the goal and the goal it was made for (planned once, again only when he
 * leaves it, is stuck or the goal moves). */
const int PATH_POINTS = 240;
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
    u8 next;
    u8 reaches;
    i16 planGoalX;
    i16 planGoalY;
    ItemPoint points[PATH_POINTS];
    /* The cells he has stood in on this walk (the map's, CELL wide), the
     * last VISITED of them, round: a plan made again keeps off them. */
    u8 visitedCount;
    u8 visitedNext;
    u16 visitedX[64];
    u16 visitedY[64];
    /* The point clicked (walkMark) and when; markAt 0 for none. */
    ItemPoint mark;
    u32 markAt;
};
Walk &walk = gameGlobal<Walk>();

/* The port's walk trace for the walk's tests (REGRET_WALK_TRACE): each
 * step's place and each plan on the standard error, the Silencer immortal
 * so that no guard ends a test. */
bool traced()
{
    static int on = -1;
    if (on < 0) {
        on = hostEnvironment("REGRET_WALK_TRACE") != nullptr ? 1 : 0;
    }
    return on == 1;
}

void trace(const char *format, ...)
{
    if (!traced()) {
        return;
    }
    char line[160];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    hostNotice(line);
}


/* The grid's cell (small: a way he just fits through between two cells of
 * a coarser grid is lost), its cells each way from the start and the most
 * nodes a plan looks at. */
const int CELL = 8;
const int RADIUS = 180;
const int SIDE = (2 * RADIUS) + 1;
const int MOST_EXPANDED = 60000;
const int HEAP = MOST_EXPANDED * 8;
/* Arrived within this of the goal: a walk's stride is 64 whatever is
 * left, so a step from nearer goes past it more than it comes nearer. */
const i16 ARRIVED = 28;
/* Nearer than this and no stride fits, a creep of CREEP units at most. */
const i16 CREEP_NEAR = 80;
const int CREEP = 16;
/* A run goes on to a walk this near the goal (a run stops a stride on). */
const i16 RUN_ENDS = 128;
/* A point of the plan this near is passed; farther than LEFT_PATH from all
 * the next ones he has left it. */
const i16 PASSED = 12;
const i16 LEFT_PATH = 48;
/* The goal moved this far plans again (a held walk's, the pointer's). */
const i16 GOAL_MOVED = 24;
/* The next points a step heads for the farthest of. */
const int LOOK_AHEAD = 8;
/* The cells walked kept, and what one costs a plan again (tenths of a
 * cell's step): not forbidden, as a way back out of a dead end may need
 * it, but any other way first. */
const int VISITED = 64;
const int VISITED_COST = 40;
/* The blocks the cells walked are kept in, units square. */
const int VISIT_CELL = 32;
/* A blocked stride: his sidesteps tried, units across (round a corner his
 * box catches on). */
const int SIDESTEPS[] = {4, 8, 12, 16};

/* The search's nodes: scratch outside the arena, written anew by each plan
 * (a rewind does not need them). */
struct Node {
    std::int32_t g;
    std::int32_t parent;
    u8 z;
    u8 closed;
    u16 plan;
    u8 visited;
};
Node nodes[SIDE * SIDE];
u16 planNumber = 0;
struct HeapEntry {
    std::int32_t f;
    std::int32_t node;
};
HeapEntry heap[HEAP];
int heapSize = 0;

void heapPush(std::int32_t f, std::int32_t node)
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
    const auto index = [](int cx, int cy) { return ((cy + RADIUS) * SIDE) + cx + RADIUS; };
    /* The grid is the map's (its cells at whole multiples of CELL), the
     * same for every plan: one moved with him finds a narrow way in one
     * place and not a step on. The start is his own place, the cell he
     * stands in. */
    const int originX = from.x & ~(CELL - 1);
    const int originY = from.y & ~(CELL - 1);
    const auto cellPoint = [&](int cx, int cy, u8 z) {
        return ItemPoint(static_cast<u16>(originX + (cx * CELL)), static_cast<u16>(originY + (cy * CELL)), z);
    };
    const int gx = (walk.goalX - originX) / CELL;
    const int gy = (walk.goalY - originY) / CELL;
    const auto heuristic = [&](int cx, int cy) { return octile(gx - cx, gy - cy) * 12 / 10; };
    const auto node = [&](int at) -> Node & {
        Node &n = nodes[at];
        if (n.plan != planNumber) {
            n = {1 << 30, -1, 0, 0, planNumber, 0};
        }
        return n;
    };
    heapSize = 0;
    const int start = index(0, 0);
    node(start) = {0, -1, from.z, 0, planNumber, 0};
    for (int v = 0; v < walk.visitedCount; v++) {
        for (int ux = 0; ux < VISIT_CELL; ux += CELL) {
            for (int uy = 0; uy < VISIT_CELL; uy += CELL) {
                const int cx = (walk.visitedX[v] + ux - originX) / CELL;
                const int cy = (walk.visitedY[v] + uy - originY) / CELL;
                if ((cx != 0 || cy != 0) && std::abs(cx) <= RADIUS && std::abs(cy) <= RADIUS) {
                    node(index(cx, cy)).visited = 1;
                }
            }
        }
    }
    heapPush(heuristic(0, 0), start);
    int best = start;
    std::int32_t bestH = heuristic(0, 0);
    int found = -1;
    int expanded = 0;
    while (heapSize > 0 && expanded < MOST_EXPANDED) {
        const int current = heapPop().node;
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
        const ItemPoint at = current == start ? from : cellPoint(cx, cy, here.z);
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
                const int neighbour = index(nx, ny);
                Node &next = node(neighbour);
                const std::int32_t g =
                    here.g + ((dx != 0 && dy != 0) ? 14 : 10) + (next.visited != 0U ? VISITED_COST : 0);
                if (next.closed != 0U || g >= next.g) {
                    continue;
                }
                ItemPoint to = cellPoint(nx, ny, here.z);
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
    /* The nodes back from the end, then the first of them from the start
     * (a longer plan is made again at its end), and the goal itself last. */
    static int chain[MOST_EXPANDED + 1];
    int at = found >= 0 ? found : best;
    int count = 0;
    while (at != start && at >= 0 && count < static_cast<int>(std::size(chain))) {
        chain[count++] = at;
        at = nodes[at].parent;
    }
    walk.count = 0;
    walk.next = 0;
    for (int i = count - 1; i >= 0 && walk.count < PATH_POINTS - 1; i--) {
        const int cx = (chain[i] % SIDE) - RADIUS;
        const int cy = (chain[i] / SIDE) - RADIUS;
        walk.points[walk.count++] = cellPoint(cx, cy, nodes[chain[i]].z);
    }
    walk.reaches = (found >= 0 && count < PATH_POINTS) ? 1 : 0;
    if (walk.reaches != 0U) {
        const u8 z = walk.count > 0 ? walk.points[walk.count - 1].z : from.z;
        walk.points[walk.count++] = ItemPoint(static_cast<u16>(walk.goalX), static_cast<u16>(walk.goalY), z);
    }
    walk.planGoalX = walk.goalX;
    walk.planGoalY = walk.goalY;
    trace("walk plan %u found %d expanded %d points %d", systemTimer->time(), found >= 0 ? 1 : 0, expanded, walk.count);
    return found >= 0;
}

/* The point under screen point x,y at the Silencer's height, as his place
 * (not his centre). */
void goalAtScreen(i16 x, i16 y)
{
    Npc npc(controlledActor);
    /* The floor under the point (or an item's foot), not the point at his
     * height: from the pad above a floor, that is far off. */
    ItemPoint ground;
    pointerMapPoint(x, y, itemZ[npc.id], true, &ground);
    walk.goalX = static_cast<i16>(ground.x + (itemX[npc.id] - npc.centreX()));
    walk.goalY = static_cast<i16>(ground.y + (itemY[npc.id] - npc.centreY()));
    walk.goalZ = ground.z;
    /* The walk's tests' goal, a map point (REGRET_WALK_GOAL=x,y, traced
     * only). */
    if (const char *goal = traced() ? hostEnvironment("REGRET_WALK_GOAL") : nullptr) {
        int gx = 0;
        int gy = 0;
        if (std::sscanf(goal, "%d,%d", &gx, &gy) == 2) {
            walk.goalX = static_cast<i16>(gx);
            walk.goalY = static_cast<i16>(gy);
        }
    }
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
    walk.next = 0;
    walk.visitedCount = 0;
    walk.visitedNext = 0;
    walk.pendingStand = 0;
}

/* His cell walked, once. */
void visit(const ItemPoint &here)
{
    const auto x = static_cast<u16>(here.x & ~(VISIT_CELL - 1));
    const auto y = static_cast<u16>(here.y & ~(VISIT_CELL - 1));
    const int last = (walk.visitedNext + VISITED - 1) % VISITED;
    if (walk.visitedCount > 0 && walk.visitedX[last] == x && walk.visitedY[last] == y) {
        return;
    }
    walk.visitedX[walk.visitedNext] = x;
    walk.visitedY[walk.visitedNext] = y;
    walk.visitedNext = static_cast<u8>((walk.visitedNext + 1) % VISITED);
    walk.visitedCount = static_cast<u8>(std::min(VISITED, walk.visitedCount + 1));
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
    trace("walk blocked %u", systemTimer->time());
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
    if (traced()) {
        npcs[npc.id].immortal = 1;
        trace("walk at %u %d %d goal %d %d", systemTimer->time(), itemX[npc.id], itemY[npc.id], walk.goalX, walk.goalY);
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
        trace("walk arrived %u", systemTimer->time());
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
    visit(here);
    const u16 shape = npc.shape();
    /* Where he is on the plan: the nearest of its next points, those he is
     * on passed. */
    const auto away = [&](const ItemPoint &point) {
        return std::max(std::abs(static_cast<i16>(point.x) - static_cast<i16>(here.x)),
                        std::abs(static_cast<i16>(point.y) - static_cast<i16>(here.y)));
    };
    int nearest = -1;
    int nearestAway = 0x7fff;
    for (int i = walk.next; i < std::min<int>(walk.count, walk.next + LOOK_AHEAD + 4); i++) {
        if (away(walk.points[i]) < nearestAway) {
            nearest = i;
            nearestAway = away(walk.points[i]);
        }
    }
    if (nearest >= 0) {
        walk.next = static_cast<u8>(nearestAway <= PASSED ? nearest + 1 : nearest);
    }
    const bool goalMoved =
        std::max(std::abs(walk.goalX - walk.planGoalX), std::abs(walk.goalY - walk.planGoalY)) > GOAL_MOVED;
    if (walk.count == 0 || walk.next >= walk.count || nearestAway > LEFT_PATH || goalMoved || walk.stuck != 0U) {
        const bool reaches = plan(shape, npc.id, here);
        if (walk.count == 0) {
            blocked(8);
            return;
        }
        const ItemPoint &last = walk.points[walk.count - 1];
        if (!reaches && distance(static_cast<i16>(last.x), static_cast<i16>(last.y)) >= walk.best - 8) {
            /* As near as it gets: a plan that leads no nearer than he has
             * been would only take him to and fro. */
            walk.count = 0;
            blocked(12);
            return;
        }
    }
    const u16 lastAnim = npc.lastAnimSet();
    const bool running = isRunning(lastAnim) || isStartingRun(lastAnim);
    const bool wantRun = (walk.run != 0U || actionHeld(Action::RUN) != 0U) && left > RUN_ENDS;
    const bool armed = pointerWeaponOut();
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
    steps[stepCount++] = {static_cast<u16>(armed ? ANIM_ADVANCE_SMALL : ANIM_WALK), 64};
    if (armed) {
        steps[stepCount++] = {ANIM_WALK, 64};
    }
    /* The steps that fit, each scored by where its stride ends: how far
     * along the plan (its points are CELL apart) less twice how far off
     * it; the best of the first kind of step any fits, if it comes further
     * along than he is. A step off the way, past where the plan turns,
     * loses to a sidestep lining him up with it. */
    const int farthest = std::min(static_cast<int>(walk.count) - 1, walk.next + LOOK_AHEAD);
    const int lastSeen = std::min(static_cast<int>(walk.count) - 1, walk.next + (LOOK_AHEAD * 3));
    const auto along = [&](int x, int y) {
        int best = -0x7fffffff;
        for (int j = walk.next; j <= lastSeen; j++) {
            const int off =
                std::max(std::abs(x - static_cast<int>(walk.points[j].x)), std::abs(y - static_cast<int>(walk.points[j].y)));
            best = std::max(best, ((j - walk.next + 1) * CELL) - (2 * off));
        }
        return best;
    };
    const auto towards = [&](const ItemPoint &from, int i) {
        return static_cast<u8>(fineDirection(static_cast<i16>(from.x), static_cast<i16>(from.y),
                                             static_cast<i16>(walk.points[i].x), static_cast<i16>(walk.points[i].y)) &
                               0xf);
    };
    const int now = along(static_cast<int>(here.x), static_cast<int>(here.y));
    for (int s = 0; s < stepCount; s++) {
        const int length = std::clamp(static_cast<int>(left), 16, steps[s].length);
        int bestScore = now;
        u8 bestDirection = 0;
        bool bestAside = false;
        ItemPoint bestFrom = here;
        const auto consider = [&](const ItemPoint &from, u8 direction, bool aside) {
            if ((walk.badDirections & (1U << direction)) != 0U) {
                return;
            }
            const Unit &unit = directionUnit(direction);
            const int score = along(static_cast<int>(from.x) + ((unit.x * steps[s].length) / 256),
                                    static_cast<int>(from.y) + ((unit.y * steps[s].length) / 256));
            if (score <= bestScore || !strideFits(shape, npc.id, from, direction, length)) {
                return;
            }
            bestScore = score;
            bestDirection = direction;
            bestAside = aside;
            bestFrom = from;
        };
        const int most = walk.badDirections != 0U ? 3 : 1;
        for (int i = farthest; i >= walk.next; i--) {
            for (int offset = -most; offset <= most; offset++) {
                consider(here, static_cast<u8>((towards(here, i) + offset) & 0xf), false);
            }
        }
        /* A sidestep of a few units across, then straight at the next
         * point (his box caught on a corner, a way he just fits). */
        if (walk.next < walk.count) {
            const u8 straight = towards(here, walk.next);
            for (const int across : SIDESTEPS) {
                for (const int side : {4, 12}) {
                    const Unit &unit = directionUnit(static_cast<u8>((straight + side) & 0xf));
                    ItemPoint aside(static_cast<u16>(here.x + ((unit.x * across) / 256)),
                                    static_cast<u16>(here.y + ((unit.y * across) / 256)), here.z);
                    if (walkMove(shape, npc.id, here, aside)) {
                        consider(aside, towards(aside, walk.next), true);
                    }
                }
            }
        }
        if (bestScore > now) {
            if (bestAside) {
                npc.move(bestFrom.x, bestFrom.y, bestFrom.z);
            }
            walk.stepDirection = static_cast<std::int8_t>(bestDirection);
            npc.doAnim(steps[s].anim, bestDirection, ONLY_IF_IDLE, 0);
            return;
        }
    }
    /* Near the goal and no stride fits (a stride is longer than what is
     * left, into a narrow place): a creep of a few units straight at it. */
    if (left < CREEP_NEAR) {
        /* At the plan's next point (the way into the narrow place), else
         * at the goal. */
        for (int target = 0; target < 2; target++) {
            const bool onPlan = target == 0 && walk.next < walk.count;
            if (target == 0 && !onPlan) {
                continue;
            }
            const i16 toX = onPlan ? static_cast<i16>(walk.points[walk.next].x) : walk.goalX;
            const i16 toY = onPlan ? static_cast<i16>(walk.points[walk.next].y) : walk.goalY;
            const int gap = std::max(std::abs(toX - static_cast<i16>(here.x)), std::abs(toY - static_cast<i16>(here.y)));
            if (gap == 0) {
                continue;
            }
            const u8 straight =
                static_cast<u8>(fineDirection(static_cast<i16>(here.x), static_cast<i16>(here.y), toX, toY) & 0xf);
            const int step = std::min(gap, CREEP);
            /* Straight, or two directions off at most, if it comes nearer. */
            for (const int offset : {0, 1, -1, 2, -2}) {
                const Unit &unit = directionUnit(static_cast<u8>((straight + offset) & 0xf));
                ItemPoint to(static_cast<u16>(here.x + ((unit.x * step) / 256)),
                             static_cast<u16>(here.y + ((unit.y * step) / 256)), here.z);
                const int after =
                    std::max(std::abs(toX - static_cast<i16>(to.x)), std::abs(toY - static_cast<i16>(to.y)));
                if (after < gap && walkMove(shape, npc.id, here, to)) {
                    npc.move(to.x, to.y, to.z);
                    return;
                }
            }
        }
    }
    /* Nothing fits: tried again a few times, the directions that failed
     * left out, before the walk ends (a held one waits). */
    walk.stuck++;
    if (walk.stuck < 3) {
        if (walk.stepDirection >= 0) {
            walk.badDirections |= static_cast<u16>(1U << walk.stepDirection);
        }
        return;
    }
    blocked(6);
}
