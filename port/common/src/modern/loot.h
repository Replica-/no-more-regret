/* What a dead NPC carried falls out of it (No Remorse's): the game moves
 * it into the body left lying (a D_ class's container, made as the death
 * ends) for a search to give the Silencer; the port throws it out of the
 * body instead, up and out a little round it by the game's own fall
 * (gravity.h's hurl), to be picked up where it lands. A weapon he carries
 * already gives its ammunition when picked up, as from a search. Not the
 * Silencer's; part of the port's controls (pointer.h's setting, which the
 * tests' original controls turn off). */
#ifndef PORT_LOOT_H
#define PORT_LOOT_H
#include "common.h"

/* An NPC died (crusader.cpp's No Remorse's death). */
void noteLoot(u16 npc);
/* A tick of the controller (Controller::run): the bodies made since, their
 * things thrown out. */
void lootTick();
#endif
