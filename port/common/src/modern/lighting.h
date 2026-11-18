/* Lighting: the world a little darker than its art (about 80 %), but where
 * something glows. A picture with enough of the palette's light colours
 * (the cycled fire colours, the translucent glows and the lone bright
 * entries of lamps, LEDs and screens), an item's but a floor's or an NPC's,
 * or a sprite's, lights the screen round its glowing pixels, fading with
 * the distance, the brightest winning where lights meet: lamps, screens,
 * fire, shots and explosions light the dark. A soft light follows the
 * Silencer so that the dark stays readable. The status display stays
 * as it is. Drawn over the game (platform/overlay.h) as a layer of shade,
 * from a small map of the light that the window scales up smoothly. */
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
