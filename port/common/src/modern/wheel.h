/* The port's weapon wheel and item wheel (bindings.h's WEAPON_WHEEL and
 * ITEM_WHEEL: Tab and I in the modern layout): the world stops and a ring
 * of the weapons in the Silencer's weapon slots, or of the items in his
 * pack, is drawn over it (platform/overlay.h), each with its picture and
 * name, the one in hand marked. The pointer or the arrows light one; a left
 * click, Enter or Space takes it in hand (as the next weapon and next item
 * keys would); the wheel's key again, Escape or the right button close it,
 * the other wheel's key turns to the other wheel. */
#ifndef PORT_WHEEL_H
#define PORT_WHEEL_H
#include "common.h"

/* Opens a wheel: 0 the weapons, 1 the items. */
void openWheel(u8 kind);
/* Draws the open wheel, if one is open; true when one is (hud.cpp). */
bool drawWheel();
#endif
