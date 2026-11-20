/* The pointer mode's walk (pointer.h): the Silencer walks, or runs, to a
 * point or up to an item along a path round what is in the way. The
 * original's path finder (pathfind.h) only steers greedily round one side,
 * which gets stuck on anything but a straight wall: this one searches a grid
 * of 16 units (A*) with the moves the walk itself makes (collide.h's
 * HitSweep::step: kept to the floor, up a step), planned again at each of his
 * steps; each step goes towards the farthest of the next points that a whole
 * stride reaches. */
#ifndef PORT_PATHWALK_H
#define PORT_PATHWALK_H
#include "common.h"
#include "itemdata.h"

/* A walk to the map point under screen point x,y, at the Silencer's
 * height; to an item, which he uses or takes once in reach; run, running.
 * walkHold walks to the pointer while held (the forward key), wherever it
 * goes. */
void walkToScreen(i16 x, i16 y, bool run);
void walkToItem(u16 item, bool run);
void walkHold(bool held);
void walkStop();
/* A walk goes on. */
[[nodiscard]] bool walking();
/* A tick: the next step when the Silencer is free. */
void walkTick();
/* The point a click walks to, on the floor where it was clicked, and the
 * ticks since the click: while the walk goes on and a moment after; false
 * when none is shown (a walk to an item, or the forward key's). */
bool walkMark(ItemPoint *point, u32 *age);
/* The item a click walks to (to use it when he reaches it), and the ticks
 * since the click; false when he walks to none. */
bool walkTarget(u16 *item, u32 *age);
#endif
