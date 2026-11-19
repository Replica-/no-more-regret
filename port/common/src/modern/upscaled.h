/* Upscaled textures (the hardware scene's, enhancements.h's UPSCALED
 * TEXTURES): frames of some shapes upscaled by an AI upscaler, twice as
 * sharp, drawn by the graphics card over the game's own where those still
 * show. port/tools/upscale.py writes them into static\upscaled.dat; with
 * it, each frame of an item on the screen that has one is laid over the
 * game's picture pixel by pixel: where the frame's pixel is still the
 * screen's (nothing in front of it, no blood or shadow painted over it),
 * its two by two upscaled pixels, in the palette's fade. Under the
 * Silencer's colours and the lighting (hud.cpp). Without the file, or the
 * setting off, nothing. */
#ifndef PORT_UPSCALED_H
#define PORT_UPSCALED_H
#include "common.h"

/* Draws them over the world (hud.cpp). */
void drawUpscaled();
/* Whether the item's shape and frame have an upscaled frame. */
[[nodiscard]] bool upscaledFrame(u16 shape, u16 frame);
/* Forgets them, read again when next asked for (closeShapeFiles). */
void releaseUpscaled();
#endif
