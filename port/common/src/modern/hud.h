/* What the port's enhancements draw over the game's screen (platform/
 * overlay.h): the pointer mode's target cursor once the weapon is out, the
 * dot over what the Silencer can use or take (green in reach, red not), the
 * path of his walk. */
#ifndef PORT_HUD_H
#define PORT_HUD_H
#include "common.h"

#include <cstdint>

/* The overlay's function given to the system layer, once as the game
 * starts. */
void startHud();
/* The angle in degrees (0-359) of the map's dx,dy: 0 along x, 90 along y,
 * by the game's own sines (pfloat.h), as the reticle's (control.cpp's
 * placeReticle). */
[[nodiscard]] i16 angleOf(std::int32_t dx, std::int32_t dy);
/* The point length along angle from x,y, by the same sines. */
void alongAngle(i16 angle, i16 length, i16 *x, i16 *y);
/* A map point's place on the screen. */
void mapToOverlay(u16 x, u16 y, i16 z, float *screenX, float *screenY);
#endif
