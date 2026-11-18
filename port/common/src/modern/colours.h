/* The Silencer's colours: his pictures are drawn with three ramps of the
 * palette, the reds of the armour, the gold of the chest's logo and the
 * browns of the pack, and each is given a colour of the player's, keeping
 * the ramp's light and shade, each shade drawn as the palette's nearest
 * colour. The choice (bindings.h's CHARACTER_COLOURS opens it: P in the
 * modern layout; the settings' ENHANCEMENTS too) picks each colour on a
 * square of shades under a bar of hues, from quick colours, or all three
 * from a preset, with the Silencer turning round in them; kept in
 * enhanced.cfg's colours= (three RRGGBB). The first preset is the game's
 * own, which the Silencer is drawn in as he is. */
#ifndef PORT_COLOURS_H
#define PORT_COLOURS_H
#include "common.h"

#include <string>

class NewGump;
class Viewport;
struct ShapeFrame;

/* The colours as enhanced.cfg has them: RRGGBB,RRGGBB,RRGGBB (a preset's
 * number is read too); a setting outside the arena. */
[[nodiscard]] std::string coloursText();
void setColoursText(const std::string &text);
/* Draws the Silencer's frame in the colours chosen, with its origin at
 * x,y, mirrored or not (DL_ItemNode::draw); false when they are the game's
 * own, which the caller draws. */
bool drawInColours(Viewport &view, i16 x, i16 y, ShapeFrame *frame, bool mirrored);
/* Opens the choice over the world; the choice as a gump of its own (the
 * settings' button opens it); draws it while it is open, true then
 * (hud.cpp). */
void openColours();
[[nodiscard]] NewGump *coloursGump();
bool drawColours();
#endif
