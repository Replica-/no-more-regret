/* An animation of No Regret's and its frames, in No Remorse: No Remorse's
 * anim.dat has no forward roll (animation 5) for the Silencer, and its shape
 * has not the frames No Regret's is drawn with. port/tools/forward_roll.py
 * writes them from No Regret's data into No Remorse's static\fwdroll.dat;
 * with it, the frames are the shape's past its own (the animation's frames
 * numbered so) and the animation is the shape's, its weapon overlays
 * No Regret's. Without it, nothing. */
#ifndef PORT_IMPORTED_H
#define PORT_IMPORTED_H
#include "common.h"

class ActorAnim;
class ShapeFrame;

/* The imported frame of shape, nullptr for none (cachedShapeFrame). */
ShapeFrame *importedFrame(u16 shape, u16 frame);
/* The imported animation put in the shape's table when it has none of
 * that number (the animation cache's loadShape). */
void importAnims(u16 shape, ActorAnim *table);
/* No Regret's draw and holster of the small weapon in No Remorse with the
 * modern controls: a frame, the pose they end in, not eleven (the
 * animation cache's loadShape); the frames No Remorse's had, for its
 * weapon overlay's, 0 for an animation not made so. */
void quickenAnims(u16 shape, ActorAnim *table);
u8 quickenedFrames(u16 shape, u16 anim);
/* The imported animation's weapon overlays of the overlay type (frames x
 * directions), nullptr for none. */
const struct OverlayFrame *importedOverlays(u16 anim, u16 type);
/* Whether the shape's animation is the imported one. */
bool importedAnim(u16 shape, u16 anim);
/* Forgets them, read again when next asked for (closeShapeFiles: the
 * game's release, before another game's data). */
void releaseImported();
#endif
