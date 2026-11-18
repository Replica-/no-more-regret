/* The pickup display's lines (display.h) shown over the place each item came
 * from, the crate, the body or the floor, instead of at the top left of the
 * screen: drawn over the game (platform/overlay.h), they follow the place as
 * the view moves, those from one place stacked upwards. A line whose item
 * came from nowhere on the map stays at the top left. */
#ifndef PORT_POPUPS_H
#define PORT_POPUPS_H
#include "common.h"

/* An item the controlled NPC is about to take (pickUp): where it is, or its
 * outermost container. */
void notePickupSource(u16 item);
/* The display's line got an item (addPickup): it is over the place noted. */
void notePickupLine(i16 line);
/* Draws the lines (hud.cpp). */
void drawPopups();
#endif
