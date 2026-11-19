/* What a dead NPC carried (No Remorse's): the game moves it into the body
 * left lying (a D_ class's container, made as the death ends), and a click
 * on the body searches it, the Silencer taking it all (pointer.h's
 * pointerUse: the game's own use of the body). The NPC's gun drops to the
 * floor beside it for show (effects.h's effectsDropGun), not an item. Not the
 * Silencer's; part of the port's controls (pointer.h's setting, which the
 * tests' original controls turn off). */
#ifndef PORT_LOOT_H
#define PORT_LOOT_H
#include "common.h"

/* An NPC died (crusader.cpp's No Remorse's death). */
void noteLoot(u16 npc);
#endif
