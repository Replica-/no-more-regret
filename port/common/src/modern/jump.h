/* The pointer mode's targeted jump (pointer.h): the jump key tapped is the
 * original's jump; held, the Silencer's flight to the place under the
 * pointer is shown (a dashed arc, a ring where he lands and a see-through
 * copy of him standing there) and he jumps there when it is let go. The
 * landing is the farthest place towards the pointer, within reach, where he
 * stands and the flight is clear, up a little or down a drop of two storeys
 * off a ledge. The flight is the game's own fall (gravity.h's hurl), thrown
 * so that it comes down there, showing the quick jump's frames, and the
 * landing's animation ends it. */
#ifndef PORT_JUMP_H
#define PORT_JUMP_H
#include "common.h"

/* The jump key down or up (pointerKey): true when the jump took it; false
 * for a tap let go, which the original's jump then makes. */
bool jumpKey(bool pressed, u16 repeat);
/* A tick of the controller (pointerTick): the jump's frames in flight, its
 * landing. */
void jumpTick();
/* Draws the flight while the key is held (hud.cpp). */
void drawJump();
#endif
