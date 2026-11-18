/* The port's keyboard and mouse controls (enhanced.cfg's pointer=, on by
 * default): a free pointer in place of the original's mouse that stays in
 * the middle and steers. The Silencer faces the pointer; with the weapon
 * drawn he aims exactly at it (the reticle follows it, no target is chosen
 * for him). The left button walks to the point clicked (a double click runs)
 * or walks up to the item clicked and uses or takes it; the right button
 * draws the weapon, a tap puts it away and held it fires. The forward key
 * walks towards the pointer for as long as it is held, round what is in the
 * way. The other keys are the controller's, relative to the way he faces. */
#ifndef PORT_POINTER_H
#define PORT_POINTER_H
#include "common.h"
#include "bindings.h"
#include "itemdata.h"

class KeyEvent;
class NewGumpEvent;

/* The setting (enhanced.cfg), and whether the mode is on now: the setting,
 * the Silencer controlled in the world. */
[[nodiscard]] bool pointerSetting();
void setPointerSetting(bool on);
[[nodiscard]] bool pointerMode();
/* The pointer's place on the screen (of the wide screen's frame, left of
 * 0 and above it in its sides). */
[[nodiscard]] i16 pointerScreenX();
[[nodiscard]] i16 pointerScreenY();
/* The pointer's reach: all the wide screen's frame while the mode has the
 * world (pointerTick), else the screen's (freezeWorld: a screen over the
 * world has the game's pointer), the pointer kept where it is. */
void pointerReach(bool frame);

/* The item under screen point x,y that the Silencer can use or take (the
 * selection's: G, S), 0 for none; whether it is in his reach, as the
 * selection's are; its use, or its taking. */
[[nodiscard]] u16 pointerItemUnder(i16 x, i16 y);
[[nodiscard]] bool pointerItemInReach(u16 item);
void pointerUse(u16 item);

/* The item the Silencer's shots go to in the mode (fireWeapon): the NPC or
 * the breakable thing under the pointer, 0 for none (the shots fly to the
 * reticle, at the pointer). */
[[nodiscard]] u16 pointerShotTarget();
/* The point the Silencer aims at in the mode: under the pointer, at the
 * height of his shots, wherever it is (SuperSprite::launchAtReticle's in
 * place of the reticle's, which the game keeps on a circle round him);
 * false when the mode is off. */
bool pointerAimPoint(ItemPoint *point);
/* Where the Silencer's next shot would go, for the overlay: from the
 * muzzle to the point aimed at, stopped at what is in the way first
 * (blocked); its spread there across and up, as SuperSprite::launch
 * scatters it. False when the weapon is not out. */
struct PointerAim {
    ItemPoint from;
    ItemPoint to;
    ItemPoint stop;
    bool blocked;
    i16 spread;
    i16 rise;
};
bool pointerAim(PointerAim *aim);

/* The world's gump's mouse in the mode: true when the mode took the event
 * (WorldGump::handleMouseMotion, mouseButton). */
bool pointerMotion(NewGumpEvent *event);
bool pointerButton(u8 right, NewGumpEvent *event);
/* The controller's key in the mode: true when the mode took it
 * (Controller::keyboardRest): the forward key walks to the pointer, the
 * jump key aims a jump (a tap let go comes back pressed, for the original's
 * jump), the other moves end a walk. */
bool pointerKey(KeyEvent &key, Action action);
/* A tick of the controller (Controller::run): the facing, the aim, the
 * fire held, the walk. */
void pointerTick();
#endif
