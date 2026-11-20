/* Upscaled textures (the hardware scene's, enhancements.h's UPSCALED
 * TEXTURES): frames of some shapes upscaled by an AI upscaler, twice as
 * sharp, drawn by the graphics card over the game's own where those still
 * show. port/tools/upscale.py writes them into static\upscaled.dat; with
 * it, each frame of an item on the screen that has one is laid over the
 * game's picture pixel by pixel: where the frame's pixel is still the
 * screen's (nothing in front of it, no blood or shadow painted over it),
 * its two by two upscaled pixels, in the palette's fade. Under the
 * lighting (hud.cpp); the Silencer's frames too (his own, the imported and
 * the turns'), his colours' ramps in the colours chosen, as bright as the
 * upscaled pixel. The file is held outside the arena (files.h's
 * fileHeld). Without it, or the setting off, nothing. */
#ifndef PORT_UPSCALED_H
#define PORT_UPSCALED_H
#include "common.h"

/* Draws them over the world (hud.cpp): all but the Silencer under his
 * colours' layer, him over it, in his colours (colours.h). */
void drawUpscaled(bool silencer);
/* An item drawn (DL_ItemNode::draw): its frame and its origin on the
 * screen not scrolled, mirrored or not; laid over as the screen shows it,
 * not as it is since (walking, he moves before he is drawn again). */
void upscaledDrawn(u16 item, u16 shape, u16 frame, i16 x, i16 y, bool mirrored);
/* Whether the item's shape and frame have an upscaled frame. */
[[nodiscard]] bool upscaledFrame(u16 shape, u16 frame);
/* Forgets them, read again when next asked for (closeShapeFiles). */
void releaseUpscaled();
#endif
