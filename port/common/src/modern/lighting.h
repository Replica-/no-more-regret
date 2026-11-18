/* Lighting: the world a little darker than its art (about 80 %), but where
 * something glows. A picture with enough of the palette's light colours
 * (the cycled fire colours, the translucent glows and the lone bright
 * entries of lamps, LEDs and screens), an item's but a floor's or an NPC's,
 * or a sprite's, lights the screen round its glowing pixels, fading with
 * the distance, the brightest winning where lights meet: lamps, screens,
 * fire, shots and explosions light the dark. A soft light follows the
 * Silencer so that the dark stays readable. The status display stays
 * as it is. Each light is worked out for each pixel of the frame, a
 * short reach fading smoothly, and drawn over the game (platform/overlay.h):
 * a layer of shade, or with the hardware scene (enhancements.h) the screen
 * multiplied by the light in its colour and the colour's glow added. */
#ifndef PORT_LIGHTING_H
#define PORT_LIGHTING_H
#include "common.h"

/* enhanced.cfg's lighting=: a setting outside the arena, on unless the
 * original controls were asked for (world/enhanced.cpp). */
[[nodiscard]] bool lightingSetting();
void setLightingSetting(bool on);
/* Draws the shade over the world (hud.cpp, first, under the rest). */
void drawLighting();
#endif
