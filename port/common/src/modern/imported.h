/* Animations of No Regret's and their frames, in No Remorse: No Remorse's
 * anim.dat has no forward roll (animation 5) nor kneeling steps to the right
 * and left (1bh, 35h) for the Silencer, and its shape has not the frames No
 * Regret's are drawn with. port/tools/import_anims.py writes them from No
 * Regret's data into No Remorse's static\imported.dat; with it, the frames
 * are the shape's past its own (the animations' frames numbered so) and the
 * animations are the shape's, their weapon overlays No Regret's. Without it,
 * nothing. */
#ifndef PORT_IMPORTED_H
#define PORT_IMPORTED_H
#include "common.h"

class ActorAnim;
class AnimFrame;
class ShapeFrame;

/* The imported frame of shape, nullptr for none (cachedShapeFrame). */
ShapeFrame *importedFrame(u16 shape, u16 frame);
/* The shape's frame an animation frame shows: its 12 bits, past 4095 for
 * an imported one's (unknown0d, which No Remorse's own never set, their
 * 13th). */
u16 animShapeFrame(const AnimFrame &frame);
/* The imported animations put in the shape's table where it has none of
 * their number (the animation cache's loadShape). */
void importAnims(u16 shape, ActorAnim *table);
/* No Regret's draw and holster of the small weapon in No Remorse with the
 * modern controls: a frame, the pose they end in, not eleven (the
 * animation cache's loadShape); the frames No Remorse's had, for its
 * weapon overlay's, 0 for an animation not made so. */
void quickenAnims(u16 shape, ActorAnim *table);
u8 quickenedFrames(u16 shape, u16 anim);
/* An imported animation's weapon overlays of the overlay type (frames x
 * directions), nullptr for none. */
const struct OverlayFrame *importedOverlays(u16 anim, u16 type);
/* Whether the shape's animation is an imported one. */
bool importedAnim(u16 shape, u16 anim);
/* Forgets them, read again when next asked for (closeShapeFiles: the
 * game's release, before another game's data). */
void releaseImported();
#endif
