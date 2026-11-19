/* Shadows: each character (an NPC, standing or moving, not dead or
 * invisible) casts its own outline on the floor, the light from one
 * direction for all of them: the frame's pixels laid down from its feet
 * along the floor to the lower right (half as far down as across, as the
 * floor's lines go), darkening what is under them in the palette's nearest
 * colour to it darkened, softly, fading toward its far end. Painted into
 * view just before the character (DL_ItemNode::draw), its place on the
 * screen widened to the shadow's (the floor under it drawn first, and
 * redrawn as it moves): what stands in front
 * draws over it, the character over its own. Part of the port's controls
 * (pointer.h's setting), which the tests' original controls turn off. */
#ifndef PORT_SHADOWS_H
#define PORT_SHADOWS_H
#include "common.h"

class Viewport;
struct ShapeFrame;

/* The shadow of item's frame, drawn with its origin at x,y of view,
 * mirrored or not, under it. */
void paintShadow(u16 item, const ShapeFrame *frame, i16 x, i16 y, bool mirrored, Viewport &view);
/* The item's place on the screen (DL_ItemNode::place's, not scrolled)
 * widened to its shadow's, which goes no further left or up: redrawn with
 * it, and the floor under the shadow drawn before it. */
void shadowBounds(u16 item, i16 top, i16 &right, i16 &bottom);
#endif
