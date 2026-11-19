/* Blood: a human that is shot (organic, with violence on) sprays it away
 * from the shot, drops that fly and fall and a fine mist that fades, drawn
 * over the game (platform/overlay.h); a drop that comes down on a floor
 * leaves a splat painted into the floor itself, under what stands on it,
 * in the palette's nearest colour to the floor reddened; one shot dead
 * leaves a pool, spreading from under the body for some seconds. Each
 * spreads as it lands, its edge uneven, a thin rim lighter, a pool's depth
 * darker and a fresh one's shine lighter, and dries to a dark brown over a
 * minute and a half after half a minute; a large one lies over the floors
 * it reaches, each painted with its part. The splats stay until there are
 * too many, the oldest going first, or the map changes.
 * The drawing's own state, outside the game's: a rewind or a load keeps
 * what was shed. */
#ifndef PORT_BLOOD_H
#define PORT_BLOOD_H
#include "common.h"

class Viewport;

/* An NPC took damage from hitter, the shot going direction (npcTakesHit). */
void noteBlood(u16 victim, u16 hitter, u8 direction, i16 damage);
/* The drops in flight, moved on by the time passed and drawn (hud.cpp). */
void drawBlood();
/* Whether a splat lies on item, and its splats painted into view after it
 * (DL_ItemNode::draw). */
[[nodiscard]] bool bloodOn(u16 item);
void paintBlood(u16 item, Viewport &view);
#endif
