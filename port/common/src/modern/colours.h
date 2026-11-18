/* The Silencer's colours: a look chosen among ready-made ones (bindings.h's
 * CHARACTER_COLOURS opens the choice: P in the modern layout), kept in
 * enhanced.cfg's colours=. His pictures are drawn with three ramps of the
 * palette: the reds of the armour, the gold of the chest's logo and the
 * browns of the pack. A look gives each its colour, keeping the ramp's
 * light and shade, each shade drawn as the palette's nearest colour. The
 * first look is the game's own. */
#ifndef PORT_COLOURS_H
#define PORT_COLOURS_H
#include "common.h"

class Viewport;
struct ShapeFrame;

/* The look chosen (0 the game's own), a setting outside the arena. */
[[nodiscard]] u8 coloursSetting();
void setColoursSetting(u8 look);
/* Draws the Silencer's frame in the look chosen, with its origin at x,y,
 * mirrored or not (DL_ItemNode::draw); false when the look is the game's
 * own, which the caller draws. */
bool drawInColours(Viewport &view, i16 x, i16 y, ShapeFrame *frame, bool mirrored);
/* Opens the choice of look; draws it while it is open, true then
 * (hud.cpp). */
void openColours();
bool drawColours();
#endif
