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

class Viewport;

/* The fires (the jets of flame and the flames a blast leaves) are not the
 * game's frames, nor in their shape (an editor's mark of PLACE ME is one),
 * but fire of the scene's own as large as the item's box, its footprint and
 * height: a glowing body and the tongues of flame, embers and smoke it
 * gives off. Where the game draws the frame, what stands in front of the
 * fire hides it; what was under the frame is kept as it is drawn and put
 * back over it. */
[[nodiscard]] bool effectsReplaces(u16 shape);
/* Before (before) and after the game draws item's frame on view at x,y
 * (DL_ItemNode::draw, the wide screen's sides' drawItem, sides); toScreenX
 * and toScreenY take view's pixels to the screen's. */
void effectsItemDrawing(Viewport &view, u16 item, u16 frame, bool before, bool sides, i16 x, i16 y, i16 toScreenX,
                        i16 toScreenY);
/* Puts back what was under the fires the game drew (hud.cpp, before the
 * lighting, which shades it as the rest). */
void drawEffectCovers();
/* Draws them over the world (hud.cpp, after the lighting). */
void drawEffects();
/* The NPC died (loot.h): its gun is flung out of its hand, turning over,
 * bounces where it fell and lies a while, for show (no item). */
void effectsDropGun(u16 npc);
/* A shot left the gun at map point x,y,z (fireWeapon). */
void effectsShot(u16 x, u16 y, u8 z);
/* The item is going away (Item::destroy): the flames a blast leaves burn
 * on. */
void effectsGone(u16 item);
#endif
