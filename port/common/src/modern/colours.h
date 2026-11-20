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

#include <cstdint>
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
/* With the hardware scene (enhancements.h), drawInColours draws him in the
 * game's colours and keeps his pixels; this draws those still showing over
 * the screen in his colours exactly, as the screen's palette has them now
 * (its fades), by the graphics card (hud.cpp, under the lighting). */
void drawColoursLayer();
/* A pixel at address of view (the screen or the wide screen's sides) that
 * was written in the palette's written but is rgb (0xRRGGBB) exactly: with
 * the hardware scene, shown so by the same layer (the blood's, blood.h). */
void keepExact(Viewport &view, u8 *address, u8 written, std::uint32_t rgb);
/* Each palette entry of his colours' ramps as the layer shows it,
 * 0xRRGGBBFF in the screen's fade; 0 for the other entries (the upscaled
 * Silencer's, upscaled.h). */
void silencerColours(std::uint32_t *out);
/* The entry the screen shows for an entry of his frame: drawInColours's,
 * his colours' table's (the entry itself in the game's own colours). */
[[nodiscard]] u8 silencerShownEntry(u8 entry);
/* Opens the choice over the world; the choice as a gump of its own (the
 * settings' button opens it); draws it while it is open, true then
 * (hud.cpp). */
void openColours();
[[nodiscard]] NewGump *coloursGump();
bool drawColours();
#endif
