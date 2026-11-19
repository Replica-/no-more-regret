/* Smooth turns (with the upscaled textures, enhancements.h): the
 * Silencer's stands as he aims in 64 directions, not 16. port/tools/
 * smooth_turns.py makes three frames between each two of his directions
 * with RIFE, a frame interpolator, into No Remorse's static\turns.dat;
 * with it and the keyboard and mouse controls, standing in one of those
 * stands he is drawn in the frame nearest the pointer's way (DL_ItemNode's
 * place and draw), within half a direction of his own: his direction, the
 * game's, is not changed. The frames are the shape's, numbered past its
 * own and the imported ones (cachedShapeFrame). Without the file, nothing. */
#ifndef PORT_TURNS_H
#define PORT_TURNS_H
#include "common.h"

struct ShapeFrame;

/* The frame the item is drawn in for its frame. */
[[nodiscard]] u16 turnFrame(u16 item, u16 frame);
/* A frame of the file, nullptr for none (cachedShapeFrame). */
ShapeFrame *turnsFrame(u16 shape, u16 frame);
/* The Silencer drawn again when the frame he is drawn in changes
 * (pointerTick). */
void turnsTick();
/* Forgets them, read again when next asked for (closeShapeFiles). */
void releaseTurns();
#endif
