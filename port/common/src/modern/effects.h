/* The hardware scene's fires, explosions and muzzle flashes (No Remorse's,
 * with the hardware scene: enhancements.h). The explosions, flashes and
 * puffs of smoke (sprites of shapes 31Ch-327h), the jets of flame (items
 * 438h, 439h, 43Bh, 43Ch, 50Ah) and the flames a blast leaves (44Ah) are
 * drawn again over the game's picture, smooth, over a glow of their own
 * shape added round them, flickering a little, the lighting's shade not on
 * them; the flames a blast leaves burn on six seconds after the game takes
 * them away; each shot leaves a warm flash at the muzzle, gone in a tenth of
 * a second. Pictures over the game only: nothing of the game's changes. */
#ifndef PORT_EFFECTS_H
#define PORT_EFFECTS_H
#include "common.h"

/* Draws them over the world (hud.cpp, after the lighting). */
void drawEffects();
/* A shot left the gun at map point x,y,z (fireWeapon). */
void effectsShot(u16 x, u16 y, u8 z);
/* The item is going away (Item::destroy): the flames a blast leaves burn
 * on. */
void effectsGone(u16 item);
#endif
