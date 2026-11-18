/* The codes the Silencer has read on the terminals, listed beside the keypad
 * (keypad.h) with what they open; a click on one types it in, Enter still
 * to be pressed. A terminal's text (compgump.h's showComputerText) gives a
 * code where a number of 3 to 6 digits stands alone (not "1701-D" nor
 * "512-555-2788") with a word of codes ("passcode", "code", "combination",
 * "keypad", "pin", "password") in the 160 characters before it or the 40
 * after. Its label is what the text says it is for ("the passcode to the
 * Sector Y15 blast door": SECTOR Y15 BLAST DOOR), else the text's first
 * line. The newest first, each code once, twelve at most. */
#ifndef PORT_CODES_H
#define PORT_CODES_H
#include "common.h"

class NewGumpEvent;

/* Notes the codes in a terminal's text. */
void noteCodes(const char *text);
/* The width the list takes beside the keypad, 0 with no code seen. */
[[nodiscard]] i16 codesWidth();
/* Draws the list with its top left corner at x,y. */
void paintCodes(i16 x, i16 y);
/* The code of the list's row at x,y from its top left corner: its digits
 * (digits[0] the first), how many; 0 for none. */
i16 codeAt(i16 x, i16 y, i16 *digits, i16 most);
#endif
