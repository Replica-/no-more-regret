/* What the port's enhancements draw over the game's screen (platform/
 * overlay.h): the pointer mode's target cursor once the weapon is out, the
 * dot over what the Silencer can use or take (green in reach, red not), the
 * path of his walk; a health bar over each NPC hit lately. */
#ifndef PORT_HUD_H
#define PORT_HUD_H
#include "common.h"

#include <cstdint>

/* The overlay's function given to the system layer, once as the game
 * starts. */
void startHud();
/* An NPC's hit that took its hit points from before to after (npcTakesHit),
 * for its health bar; none for the Silencer. */
void noteHit(u16 id, u16 before, u16 after);
/* The angle in degrees (0-359) of the map's dx,dy: 0 along x, 90 along y,
 * by the game's own sines (pfloat.h), as the reticle's (control.cpp's
 * placeReticle). */
[[nodiscard]] i16 angleOf(std::int32_t dx, std::int32_t dy);
/* The point length along angle from x,y, by the same sines. */
void alongAngle(i16 angle, i16 length, i16 *x, i16 *y);
/* A map point's place on the screen. */
void mapToOverlay(u16 x, u16 y, i16 z, float *screenX, float *screenY);
/* Words and the game's pictures over the overlay, drawn by the game into
 * pixels of its own (its fonts, gump.h's CAFont, and its gump shapes)
 * between wordsBegin and wordsEnd, which gives them to the overlay in the
 * palette's colours. A line of text ends at x (align -1), is centred on it
 * (0) or starts at it (1); a picture is centred on x,y; a frame of the
 * map's shapes has its origin at x,y, mirrored as a flipped item's. wordsEnd
 * gives them see-through by alpha. */
/* The game's pointer as the overlay's arrow, for a screen of the port's
 * drawn over the game's picture, which hides the game's own (colours.h). */
void drawMenuPointer();
void wordsBegin();
void wordsText(i16 font, i16 x, i16 y, const char *text, i16 align);
[[nodiscard]] i16 wordsWidth(i16 font, const char *text);
void wordsPicture(u16 shape, u16 frame, i16 x, i16 y);
/* A gump shape's frame with its origin at x,y. */
void wordsGump(u16 shape, u16 frame, i16 x, i16 y);
void wordsShape(u16 shape, u16 frame, bool mirrored, i16 x, i16 y);
void wordsEnd(u8 alpha = 0xff);
#endif
