/* The hardware scene's fires, explosions and muzzle flashes (No Remorse's,
 * with the hardware scene: enhancements.h). The explosions, flashes and
 * puffs of smoke (sprites of shapes 31Ch-327h), the jets of flame (items
 * 438h-43Ch, 4CBh, 50Ah, 518h: opening, burning, shutting) and the flames a
 * blast leaves (44Ah) are
 * drawn again over the game's picture, smooth, over a glow of their own
 * shape added round them, flickering a little, the lighting's shade not on
 * them; the flames a blast leaves burn on six seconds after the game takes
 * them away; each shot leaves a warm flash at the muzzle, gone in a tenth of
 * a second. Pictures over the game only: nothing of the game's changes. */
#ifndef PORT_EFFECTS_H
#define PORT_EFFECTS_H
#include "common.h"

class Viewport;

/* The fires (the jets of flame and the flames a blast leaves) are not the
 * game's frames: the game draws none of them (effectsReplaces), and the
 * editor's marks of their places (PLACE ME) none at all. The fire is the
 * scene's own, as large as the item's box, its footprint and height: a
 * glowing body and the tongues of flame, embers and smoke it gives off,
 * over what stands in front of it. A jet's is solid, shot out of its pipe
 * along its box, as long as its frame's (opening and shutting). */
[[nodiscard]] bool effectsReplaces(u16 shape);
/* Draws them over the world (hud.cpp, after the lighting). */
void drawEffects();
/* The NPC died (loot.h): its gun drops from its hand to the floor beside
 * it and lies there while the map is played, for show (no item). */
void effectsDropGun(u16 npc);
/* A shot left the gun at map point x,y,z (fireWeapon). */
void effectsShot(u16 x, u16 y, u8 z);
/* The item is going away (Item::destroy): the flames a blast leaves burn
 * on. */
void effectsGone(u16 item);
#endif
