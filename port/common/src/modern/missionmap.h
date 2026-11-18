/* The port's mission map (bindings.h's MISSION_MAP: M in the modern
 * layout): the world stops and the map is drawn over it from above
 * (platform/overlay.h), in the game's isometric orientation without the
 * height, only where the Silencer has been: the floors, the walls lighter
 * the taller, the doors amber and the force fields violet, and dots for him
 * (green), the NPCs alive (red), the keys and the items to use (cyan), the
 * weapons and ammunition (orange) and the things to use (blue). It opens
 * on him; a drag or the arrows move it, the mouse's wheel (the previous and
 * next weapon's inputs) or + and - zoom, the middle of the keypad or C
 * comes back to him; M, Escape or the right button close it. */
#ifndef PORT_MISSIONMAP_H
#define PORT_MISSIONMAP_H
#include "common.h"

void openMissionMap();
/* Draws the open map, if it is open; true when it is (hud.cpp). */
bool drawMissionMap();
/* Marks what the screen shows of the map as seen, at each frame
 * (hud.cpp). */
void noteSeen();
#endif
