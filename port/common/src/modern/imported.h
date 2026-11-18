/* An animation of No Regret's and its frames, in No Remorse: No Remorse's
 * anim.dat has no forward roll (animation 5) for the Silencer, and its shape
 * has not the frames No Regret's is drawn with. port/tools/forward_roll.py
 * writes them from No Regret's data into No Remorse's static\fwdroll.dat;
 * with it, the frames are the shape's past its own (the animation's frames
 * numbered so) and the animation is the shape's. Without it, nothing. */
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
/* Whether the shape's animation is the imported one. */
bool importedAnim(u16 shape, u16 anim);
/* Forgets them, read again when next asked for (closeShapeFiles: the
 * game's release, before another game's data). */
void releaseImported();
#endif
