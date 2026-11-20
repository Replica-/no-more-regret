/* Seeing through: while the player holds the see-through action's keys
 * (bindings.h's SEE_THROUGH: B in the modern layout), the world's things
 * that stand in the way (walls, machines, furniture, crates) are drawn half
 * through, each pixel of theirs the palette's nearest to it mixed half and
 * half with what is under it, so that the dead behind them show and can be
 * searched. What is the player's to find or fight stays as it is: the NPCs,
 * living and dead (the bodies, D_ classes' containers), the weapons,
 * ammunition, bombs, credits and other things to pick up, and the floors
 * and what lies flat on them (no height). Their upscaled frames (upscaled.h) are left off meanwhile: the
 * low-resolution mix shows. The original layout has no key for it. */
#ifndef PORT_SEETHROUGH_H
#define PORT_SEETHROUGH_H
#include "common.h"

class Viewport;
struct ShapeFrame;

/* Takes the keys' state for the frames to come, once a frame after the
 * world is drawn (hud.cpp's drawHud): the screen drawn again when it
 * changes, all of a frame drawn alike. */
void noteSeeThrough();
/* Whether item is drawn half through now. */
[[nodiscard]] bool seeThrough(u16 item);
/* While the player sees through, a click goes to a body under the pointer
 * before what stands in front of it (item.h's itemAt): whether one is, in
 * item. A body: an NPC dead or dying, or a dead one's container (the D_
 * classes'). */
bool seeThroughBodyAt(u16 *item, i16 x, i16 y);
/* A corpse under the pointer whatever is drawn in front of it, seeing
 * through or not (pointer.h's pointerItemUnder: a click searches it before
 * what hides it): whether one is, in item. A corpse: an NPC dead or dying,
 * or a dead one's container (a D_ usecode class's). */
bool corpseAt(u16 *item, i16 x, i16 y);
/* Draws frame half through at x,y of view, mirrored or not
 * (DL_ItemNode::draw). */
void drawSeeThrough(Viewport &view, i16 x, i16 y, ShapeFrame *frame, bool mirrored);
#endif
