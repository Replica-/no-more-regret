/* The port's keyboard and mouse controls. See pointer.h. */
#include "pointer.h"
#include "gamestate.h"
#include "animprim.h"
#include "camera.h"
#include "collide.h"
#include "control.h"
#include "crusader.h"
#include "crusys.h"
#include "distance.h"
#include "explode.h"
#include "dtable.h"
#include "firetype.h"
#include "shapes.h"
#include "shaphand.h"
#include "sprite_game.h"
#include "hud.h"
#include "itemdata.h"
#include "kernel.h"
#include "keyboard.h"
#include "mousectl.h"
#include "input.h"
#include "screen.h"
#include "status.h"
#include "newgump.h"
#include "npc.h"
#include "spmgr.h"
#include "pathwalk.h"
#include "jump.h"
#include "systimer.h"
#include "type.h"
#include "world.h"

#include <algorithm>
#include <cstdlib>

namespace {

/* enhanced.cfg's pointer=: a setting outside the arena. Off until the
 * game's start turns it on (world/enhanced.cpp), so that a test of parts
 * alone has the original's controls. */
bool &pointerOn = gameSetting<bool>(false);

/* The buttons' state, the arena's (a rewind takes it back with the game).
 * The right button: when it went down, whether it is down, whether its
 * press drew the weapon (or cancelled a draw waiting: its release then
 * leaves the weapon as it is), whether it fires (held long enough), whether
 * its press came while the weapon was being drawn (a tap puts it away at
 * once). Whether the mode took the left button's press (its release then
 * too); the left button's last click, for the double click. A draw or a
 * holster waits for the Silencer's animation. */
struct Buttons {
    u32 rightDownAt;
    u8 rightDown;
    u8 rightDrew;
    u8 rightFiring;
    u8 rightCancel;
    u8 leftTaken;
    u8 pendingDraw;
    u8 pendingHolster;
    u32 lastClickAt;
    i16 lastClickX;
    i16 lastClickY;
};
Buttons &buttons = gameGlobal<Buttons>();

/* A move key pressed while the Silencer was busy, which the original
 * drops (startMoveNoRemorse): given to the controller as soon as he is
 * free, if that is soon (KEY_WAIT ticks); at 0 for none. Whether his
 * animation is a turn to the pointer (turnTowards), which a key cuts
 * short instead. */
struct WaitingKey {
    KeyEvent key;
    Action action;
    u32 at;
    u8 turning;
    u32 turnedAt;
};
WaitingKey &waiting = gameGlobal<WaitingKey>();
const u32 KEY_WAIT = 45;

/* A double click's ticks and pixels apart. */
const u32 DOUBLE_CLICK_TICKS = 16;
const i16 DOUBLE_CLICK_PIXELS = 14;
/* The right button held this long fires (a fifth of a second). */
const u32 HOLD_FIRE_TICKS = 10;
/* The pointer this close to the Silencer turns him no more. */
const i16 FACE_DEAD_ZONE = 10;

/* The pointer's reach (pointerReach): the frame's size, and its columns
 * left of the screen and rows above it, which the driver's place is from;
 * all 0 for the screen's. */
struct Reach {
    i16 left;
    i16 top;
    i16 width;
    i16 height;
};
Reach &reach = gameGlobal<Reach>(Reach{});

/* The most a turn to the pointer takes at a step, of the 16 directions,
 * and the ticks before the next step cuts the stand short (the stand runs
 * 7 or 8): the turn half as fast again as a quarter at a whole stand. */
const int TURN_STEP = 6;
const u32 TURN_TICKS = 5;

/* His pose a combat one (the original's controls' inCombatStance), but
 * not one of peace: No Regret's teleport in leaves him in combat, standing
 * as at peace (ANIM_TELEPORT_IN, inPeacefulStance). */
bool combatPose()
{
    return inCombatStance() && !inPeacefulStance();
}

/* The stand of his stance, which a turn is made of (turnFrom's). */
u16 standAnim(Npc &npc)
{
    if (npc.isKneeling() != 0U) {
        return ANIM_KNEELING_SMALL;
    }
    /* His pose, not the flag: No Regret's teleport in leaves him in combat
     * standing as at peace, and the original's controls go by the pose. */
    if (npc.isInCombat() != 0U && combatPose()) {
        const Item gun(npc.activeWeapon());
        return gun.isValid() && smallWeapon(gun.shape()) == 0U ? ANIM_COMBAT_STAND_LARGE : ANIM_COMBAT_STAND_SMALL;
    }
    return ANIM_STAND;
}

u32 now()
{
    return systemTimer->time();
}

/* A turn towards the pointer, the short way round, as far as TURN_STEP at
 * once: his stand in the new direction, not the game's turn of a step at a
 * time (turnFrom), which lags behind the mouse. */
void turnTowards(Npc &npc, char toward)
{
    const int difference = ((toward - npc.direction()) + 16) % 16;
    const int step = difference <= 8 ? std::min(difference, TURN_STEP) : -std::min(16 - difference, TURN_STEP);
    const auto next = static_cast<u8>((npc.direction() + step) & 0xf);
    if (npc.doAnim(standAnim(npc), next, ONLY_IF_IDLE, 0) != 0U) {
        waiting.turning = 1;
        waiting.turnedAt = now();
    }
}

/* Free to turn: idle, or TURN_TICKS into a turn's stand, cut short. */
bool turnFree(Npc &npc)
{
    if (!npc.isBusy()) {
        return true;
    }
    if (waiting.turning == 0U || npc.lastAnimSet() != standAnim(npc) || now() - waiting.turnedAt < TURN_TICKS) {
        return false;
    }
    killItemProcesses(npc.id, ANIM_TYPE, Priority(PRIORITY_EQUAL | BASE_LEVEL));
    waiting.turning = 0;
    return true;
}

/* The Silencer's weapon out, or being drawn: the flag and his pose. */
bool weaponOut(Npc &npc)
{
    return npc.isWeaponDrawn() != 0U && combatPose();
}

bool drawing(Npc &npc)
{
    return npc.isBusy() && npc.lastAnimSet() == ANIM_READY_SMALL;
}

/* The draw: with the weapon drawn already, standing as at peace (No
 * Regret's teleport in leaves him so, the gun in his hand), the combat
 * stand, not the draw again. */
u16 readyAnim(Npc &npc)
{
    if (npc.isWeaponDrawn() != 0U && npc.isKneeling() == 0U) {
        const Item gun(npc.activeWeapon());
        return gun.isValid() && smallWeapon(gun.shape()) == 0U ? ANIM_COMBAT_STAND_LARGE : ANIM_COMBAT_STAND_SMALL;
    }
    return ANIM_READY_SMALL;
}

/* The weapon drawn, or as soon as the Silencer is free (he is often in the
 * middle of a turn or a step): the draw waits instead of being lost. */
void draw(Npc &npc)
{
    if (!npc.isBusy()) {
        npc.doAnim(readyAnim(npc), DIRECTION_CURRENT, ONLY_IF_IDLE, 0);
    } else {
        buttons.pendingDraw = 1;
    }
}

void holster(Npc &npc, bool atOnce)
{
    buttons.pendingDraw = 0;
    if (atOnce) {
        killItemProcesses(npc.id, ANIM_TYPE, Priority(PRIORITY_EQUAL | BASE_LEVEL));
    }
    if (npc.isKneeling() != 0U) {
        npc.doAnim(ANIM_KNEEL_END, DIRECTION_CURRENT, ONLY_IF_IDLE, 0);
    }
    if (atOnce || !npc.isBusy()) {
        npc.doAnim(ANIM_UNREADY_SMALL, DIRECTION_CURRENT, EVEN_IF_BUSY, 0);
    } else {
        buttons.pendingHolster = 1;
    }
}

/* The fire key's press or release, as the controller takes a key bound to
 * it: held, a press each tick (the controller passes over those that come
 * while it fires). */
void fireKey(bool pressed, u8 repeat)
{
    if (controller == nullptr) {
        return;
    }
    KeyEvent key;
    key.ascii = 0;
    key.scan = 0;
    key.shift = 0;
    key.time = 0;
    key.pressed = pressed ? 1 : 0;
    key.extended = 0;
    key.repeat = repeat;
    controller->actionKey(&key, Action::FIRE);
}

void stopFire()
{
    buttons.rightFiring = 0;
    fireKey(false, 1);
    /* Stopped at once: the controller would end the burst it is in, and
     * fire on while its timer runs. */
    if (controller != nullptr) {
        controller->stopFiring();
    }
}

/* The right button's press: it ends a walk; with the weapon away it draws
 * it (a second press while the draw waits cancels it); held, it fires
 * (pointerTick). */
void rightPress(Npc &npc, bool isDrawing, bool out)
{
    walkStop();
    buttons.rightDown = 1;
    buttons.rightDownAt = now();
    buttons.rightFiring = 0;
    buttons.rightDrew = 0;
    buttons.rightCancel = isDrawing ? 1 : 0;
    if (buttons.pendingDraw != 0U) {
        buttons.pendingDraw = 0;
        buttons.rightDrew = 1;
        return;
    }
    if (!out) {
        buttons.rightDrew = 1;
        draw(npc);
    }
}

/* The right button's release: a hold stops firing; a tap on the weapon
 * out puts it away (half way through drawing it, at once). */
void rightRelease()
{
    buttons.rightDown = 0;
    const bool cancel = buttons.rightCancel != 0U;
    buttons.rightCancel = 0;
    if (buttons.rightFiring != 0U) {
        stopFire();
        return;
    }
    if (buttons.rightDrew != 0U || !pointerMode()) {
        return;
    }
    Npc npc(controlledActor);
    if (cancel) {
        holster(npc, true);
    } else if (weaponOut(npc) && !npc.isBusy()) {
        holster(npc, false);
    }
}

/* The item the Silencer can use or take: one of those the selection (G, S)
 * offers. */
bool usable(const Item &item)
{
    if (!item.isValid() || item.id == controlledActor || (item.flags() & ITEM_CONTAINED) != 0) {
        return false;
    }
    const ShapeInfo info = typeFlags.info(item.shape());
    const i16 family = item.family();
    return item.shape() == SHAPE_CREDITS || family == FAMILY_WEAPON || family == FAMILY_INVENTORY ||
           family == FAMILY_BOMB || family == FAMILY_AMMO || info.selectable;
}

}

bool pointerWeaponOut()
{
    Npc npc(controlledActor);
    return weaponOut(npc);
}

bool pointerSetting()
{
    return pointerOn;
}

void setPointerSetting(bool on)
{
    pointerOn = on;
}

bool pointerMode()
{
    if (!pointerOn || worldActive == 0U || controller == nullptr || controlledActor != 1) {
        return false;
    }
    Npc npc(controlledActor);
    return npc.isValid() && npc.isDead() == 0U && controlledNpc.held == 0U;
}

i16 pointerScreenX()
{
    return mouseCtl != nullptr ? static_cast<i16>(mouseCtl->pointerX - reach.left) : 0;
}

i16 pointerScreenY()
{
    return mouseCtl != nullptr ? static_cast<i16>(mouseCtl->pointerY - reach.top) : 0;
}

bool pointerClicksAtOnce()
{
    return pointerSetting() && worldActive != 0U && worldFrozen == 0U;
}

bool pointerEventPlace(s16 *x, s16 *y)
{
    if (reach.width == 0) {
        return false;
    }
    *x = static_cast<s16>(*x - reach.left);
    *y = static_cast<s16>(*y - reach.top);
    return *x < 0 || *y < 0 || *x >= SCREEN_WIDTH || *y >= SCREEN_HEIGHT;
}

void pointerReach(bool frame)
{
    if (mouseCtl == nullptr) {
        return;
    }
    Reach wanted{};
    if (frame) {
        wanted.width = static_cast<i16>(screenWideWidth());
        wanted.height = static_cast<i16>(screenWideHeight());
        wanted.left = static_cast<i16>((wanted.width - SCREEN_WIDTH) / 2);
        wanted.top = static_cast<i16>((wanted.height - SCREEN_HEIGHT) / 2);
    }
    if (wanted.left == reach.left && wanted.top == reach.top && wanted.width == reach.width &&
        wanted.height == reach.height) {
        /* Its limits again: the game sets the screen's (showStatus, the
         * mouse's reset), which would keep the pointer off the frame's
         * right and bottom. */
        if (frame) {
            inputMouseXRange(0, static_cast<i16>((wanted.width - 1) * 2));
            inputMouseYRange(0, static_cast<i16>(wanted.height - 1));
        }
        return;
    }
    /* The pointer kept where it is on the screen. */
    const i16 x = pointerScreenX();
    const i16 y = pointerScreenY();
    reach = wanted;
    if (frame) {
        inputMouseXRange(0, static_cast<i16>((wanted.width - 1) * 2));
        inputMouseYRange(0, static_cast<i16>(wanted.height - 1));
    } else {
        inputMouseXRange(0, (SCREEN_WIDTH - 1) * 2);
        /* showStatus' and hideStatus' */
        inputMouseYRange(0, statusShown != 0U ? 439 : SCREEN_BOTTOM);
    }
    mouseCtl->moveTo(static_cast<i16>(std::clamp<int>(x + reach.left, 0, (frame ? wanted.width : SCREEN_WIDTH) - 1)),
                     static_cast<i16>(std::clamp<int>(y + reach.top, 0, (frame ? wanted.height : SCREEN_HEIGHT) - 1)));
}

bool pointerItemInReach(u16 item)
{
    const Item it(item);
    if (!usable(it)) {
        return false;
    }
    Npc npc(controlledActor);
    if (itemZ[it.id] >= static_cast<i16>(itemZ[npc.id] + 0x50) ||
        itemZ[it.id] <= static_cast<i16>(itemZ[npc.id] - 0x18)) {
        return false;
    }
    return mapDistance(it.centreX(), it.centreY(), npc.centreX(), npc.centreY()) < 0x80;
}

u16 pointerItemUnder(i16 x, i16 y)
{
    u16 found = 0;
    if (!itemAt(&found, x, y)) {
        return 0;
    }
    return usable(Item(found)) ? found : 0;
}

void pointerUse(u16 item)
{
    Item it(item);
    if (typeFlags.info(it.shape()).selectable) {
        it.use();
    } else {
        pickUp(item, 1);
    }
}

bool pointerMotion(NewGumpEvent * /*event*/)
{
    /* The pointer goes where the mouse takes it, whoever has the Silencer
     * (a scripted moment too): nothing steers. */
    return pointerSetting() && worldActive != 0U;
}

bool pointerButton(u8 right, NewGumpEvent *event)
{
    /* The mode takes the right button's presses and releases: the
     * original's (its walk, a double click readying the weapon) never see
     * them. */
    if (event->isMouseRelease()) {
        if (right != 0U) {
            if (buttons.rightDown != 0U) {
                rightRelease();
            }
            return pointerSetting() && worldActive != 0U;
        }
        const bool taken = buttons.leftTaken != 0U;
        buttons.leftTaken = 0;
        return taken;
    }
    if (!pointerMode()) {
        /* The Silencer not the player's (a scripted moment): the mouse's
         * press does nothing, as the pointer is the player's still. */
        return pointerSetting() && worldActive != 0U;
    }
    Npc npc(controlledActor);
    const bool isDrawing = drawing(npc);
    const bool out = weaponOut(npc) || isDrawing;
    if (right != 0U) {
        rightPress(npc, isDrawing, out);
        return true;
    }
    buttons.leftTaken = 1;
    if (out) {
        holster(npc, isDrawing);
    }
    const i16 x = pointerScreenX();
    const i16 y = pointerScreenY();
    const u32 time = now();
    const bool twice = buttons.lastClickAt != 0U && time - buttons.lastClickAt <= DOUBLE_CLICK_TICKS &&
                       std::abs(x - buttons.lastClickX) <= DOUBLE_CLICK_PIXELS &&
                       std::abs(y - buttons.lastClickY) <= DOUBLE_CLICK_PIXELS;
    buttons.lastClickAt = twice ? 0 : time;
    buttons.lastClickX = x;
    buttons.lastClickY = y;
    const u16 item = pointerItemUnder(x, y);
    if (item != 0U && pointerItemInReach(item)) {
        walkStop();
        pointerUse(item);
    } else if (item != 0U) {
        walkToItem(item, twice);
    } else {
        walkToScreen(x, y, twice);
    }
    return true;
}

namespace {

/* A roll the mode gives the controller (screenRoll): not taken again. */
bool screenRolling = false;

/* The roll keys roll towards the screen's left or right, not his: he
 * faces the pointer, so his own sides go round with it. A side roll goes
 * a quarter turn off his facing (sideStep's), right to the facing's +4,
 * left to its -4; of the two ways to go, the one turning him least. */
void screenRoll(Npc &npc, KeyEvent &key, bool right)
{
    const auto screenward =
        static_cast<u8>(fineDirection(0, 0, static_cast<i16>(right ? 100 : -100), static_cast<i16>(right ? -100 : 100)) &
                        0xf);
    const auto facing = static_cast<u8>(npc.direction() & 0xf);
    const auto turn = [facing](u8 to) {
        const int d = std::abs(static_cast<int>(to) - static_cast<int>(facing));
        return std::min(d, 16 - d);
    };
    const auto asRight = static_cast<u8>((screenward - 4) & 0xf);
    const auto asLeft = static_cast<u8>((screenward + 4) & 0xf);
    const bool rollRight = turn(asRight) <= turn(asLeft);
    npc.setDirection(rollRight ? asRight : asLeft);
    if (controller != nullptr) {
        screenRolling = true;
        KeyEvent roll = key;
        controller->actionKey(&roll, rollRight ? Action::ROLL_RIGHT : Action::ROLL_LEFT);
        screenRolling = false;
    }
}

}

bool pointerKey(KeyEvent &key, Action action)
{
    if (!pointerMode()) {
        return false;
    }
    /* The forward key with the roll key held rolls forward (the
     * controller's roll); alone it walks to the pointer. */
    if (action == Action::MOVE_FORWARD && keyMode(&key, Action::ROLL)) {
        walkStop();
        return false;
    }
    /* Kneeling, it rolls forward, towards the reticle (once a press). */
    if (action == Action::MOVE_FORWARD && Npc(controlledActor).isKneeling() != 0U) {
        walkStop();
        if (key.pressed != 0U && key.repeat == 1 && controller != nullptr) {
            Npc npc(controlledActor);
            ItemPoint aim;
            pointerAimPoint(&aim);
            if (std::abs(static_cast<int>(aim.x) - static_cast<int>(npc.centreX())) +
                    std::abs(static_cast<int>(aim.y) - static_cast<int>(npc.centreY())) >=
                FACE_DEAD_ZONE * 4) {
                npc.setDirection(static_cast<u8>(fineDirection(static_cast<i16>(npc.centreX()),
                                                               static_cast<i16>(npc.centreY()),
                                                               static_cast<i16>(aim.x), static_cast<i16>(aim.y)) &
                                                 0xf));
            }
            KeyEvent roll = key;
            controller->actionKey(&roll, Action::ROLL_FORWARD);
        }
        return true;
    }
    if (action == Action::MOVE_FORWARD) {
        walkHold(key.pressed != 0U);
        return true;
    }
    /* The jump key held aims a jump (jump.h); a tap goes on as the
     * original's, made as the key is let go. */
    if (action == Action::JUMP) {
        if (jumpKey(key.pressed != 0U, key.repeat)) {
            return true;
        }
        walkStop();
        key.pressed = 1;
        key.repeat = 1;
        return false;
    }
    if (key.pressed != 0U &&
        (action == Action::MOVE_BACK || action == Action::SLIDE_LEFT || action == Action::SLIDE_RIGHT ||
         action == Action::ROLL_LEFT || action == Action::ROLL_RIGHT || action == Action::KNEEL ||
         action == Action::ADVANCE || action == Action::RETREAT || action == Action::ROLL_FORWARD)) {
        walkStop();
        /* Not lost while he is busy: a turn to the pointer gives way at
         * once, anything else is waited for (pointerTick). */
        Npc npc(controlledActor);
        if (npc.isBusy() && key.repeat == 1) {
            if (waiting.turning != 0U && npc.lastAnimSet() == standAnim(npc)) {
                killItemProcesses(npc.id, ANIM_TYPE, Priority(PRIORITY_EQUAL | BASE_LEVEL));
                waiting.turning = 0;
            } else {
                waiting.key = key;
                waiting.action = action;
                waiting.at = std::max<u32>(1, now());
                return true;
            }
        }
        if ((action == Action::ROLL_LEFT || action == Action::ROLL_RIGHT) && !screenRolling) {
            screenRoll(npc, key, action == Action::ROLL_RIGHT);
            return true;
        }
    }
    return false;
}

bool pointerAimPoint(ItemPoint *point)
{
    if (!pointerMode()) {
        return false;
    }
    Npc npc(controlledActor);
    u16 muzzleX;
    u16 muzzleZ;
    fireOffset(static_cast<u8>(npc.direction()), &muzzleX, &muzzleX, &muzzleZ);
    pointerMapPoint(pointerScreenX(), pointerScreenY(), itemZ[npc.id] + muzzleZ, false, point);
    return true;
}

void pointerMapPoint(i16 screenX, i16 screenY, int height, bool ground, ItemPoint *point)
{
    u16 mapX;
    u16 mapY;
    screenToMap(screenX, screenY, &mapX, &mapY);
    /* The points under the screen point are a line through the map, the
     * screen's point lifted by twice the height (placeReticle's): x + 2h,
     * y + 2h, h. The point is where it first meets the box of the item
     * under it (the NPC, the crate, the floor), from the viewer's side, the
     * highest; on the ground, an item's not the floor's at its foot; without
     * one, at height. */
    u16 found = 0;
    if (itemAt(&found, screenX, screenY) && found != controlledActor) {
        const Item it(found);
        i16 width;
        i16 depth;
        i16 tall;
        typeFlags.info(it.shape()).box(width, depth, tall);
        if ((it.flags() & ITEM_FLIPPED) != 0) {
            std::swap(width, depth);
        }
        const auto floorHalf = [](int value) { return value >= 0 ? value / 2 : -((1 - value) / 2); };
        const auto ceilHalf = [&](int value) { return -floorHalf(-value); };
        const int x = itemX[found];
        const int y = itemY[found];
        const int z = itemZ[found];
        const int highest = std::min({floorHalf(x - mapX), floorHalf(y - mapY), z + tall});
        const int lowest = std::max({ceilHalf(x - width + 1 - mapX), ceilHalf(y - depth + 1 - mapY), z});
        height = highest >= lowest ? highest : z + tall;
        if (ground && typeFlags.info(it.shape()).land == 0U) {
            height = z;
        }
    }
    height = std::clamp(height, 0, 0xfa);
    const i16 lift = static_cast<i16>(height * 2);
    *point = ItemPoint(static_cast<u16>(mapX + lift), static_cast<u16>(mapY + lift), static_cast<u8>(height));
}

void pointerTick()
{
    pointerReach(pointerSetting() && worldActive != 0U && worldFrozen == 0U);
    if (!pointerMode()) {
        waiting.at = 0;
        if (buttons.rightFiring != 0U) {
            stopFire();
        }
        buttons.rightDown = 0;
        buttons.pendingDraw = 0;
        buttons.pendingHolster = 0;
        walkStop();
        return;
    }
    Npc npc(controlledActor);
    /* Hidden in a blank frame (mission 1's start, till he teleports in): a
     * turn or a draw would stand him there to be seen. */
    FrameRect bounds{};
    shapeCache->frameBounds(npc.shape(), npc.frame(), &bounds, 0, 0);
    if (bounds.left == bounds.right) {
        walkStop();
        return;
    }
    /* The target the original chooses is not the player's: the aim is the
     * pointer's. */
    targeting = 0;
    if (!npc.isBusy()) {
        waiting.turning = 0;
    }
    if (waiting.at != 0U && now() - waiting.at > KEY_WAIT) {
        waiting.at = 0;
    }
    if (waiting.at != 0U && !npc.isBusy() && controller != nullptr) {
        waiting.at = 0;
        KeyEvent key = waiting.key;
        controller->actionKey(&key, waiting.action);
    }
    if (buttons.pendingHolster != 0U && !npc.isBusy()) {
        buttons.pendingHolster = 0;
        if (weaponOut(npc)) {
            holster(npc, false);
        }
    }
    /* The weapon drawn already, standing as at peace (a script's: No
     * Regret's teleport in leaves him so, the gun in his hand): the combat
     * stand at once, the weapon out. */
    if (npc.isWeaponDrawn() != 0U && npc.isKneeling() == 0U && !npc.isBusy() &&
        (npc.lastAnimSet() == ANIM_TELEPORT_IN || npc.lastAnimSet() == ANIM_STAND)) {
        npc.doAnim(readyAnim(npc), DIRECTION_CURRENT, ONLY_IF_IDLE, 0);
    }
    if (buttons.pendingDraw != 0U && !npc.isBusy()) {
        buttons.pendingDraw = 0;
        if (!weaponOut(npc)) {
            npc.doAnim(readyAnim(npc), DIRECTION_CURRENT, ONLY_IF_IDLE, 0);
        }
    }
    /* The right button held a fifth of a second fires until let go, from
     * as soon as the weapon is out. */
    if (buttons.rightDown != 0U && buttons.rightFiring == 0U && now() - buttons.rightDownAt >= HOLD_FIRE_TICKS &&
        weaponOut(npc) && !drawing(npc)) {
        buttons.rightFiring = 1;
        fireKey(true, 1);
    } else if (buttons.rightFiring != 0U) {
        fireKey(true, 2);
    }
    walkTick();
    jumpTick();
    ItemPoint aim;
    pointerAimPoint(&aim);
    const i16 aimX = static_cast<i16>(aim.x);
    const i16 aimY = static_cast<i16>(aim.y);
    const u8 height = aim.z;
    const i16 dx = static_cast<i16>(aimX - static_cast<i16>(npc.centreX()));
    const i16 dy = static_cast<i16>(aimY - static_cast<i16>(npc.centreY()));
    if (std::abs(dx) + std::abs(dy) < FACE_DEAD_ZONE * 4) {
        return;
    }
    const char toward = fineDirection(static_cast<i16>(npc.centreX()), static_cast<i16>(npc.centreY()), aimX, aimY);
    if (weaponOut(npc) && !drawing(npc)) {
        /* The reticle at the pointer's angle: the shots fly to it. */
        reticleAngle = angleOf(dx, dy);
        if (reticleShown != 0U) {
            placeReticle();
            showSprite(reticleSprite, aimX, aimY, height);
            /* Not drawn: the cursor (hud.cpp) is where it is. */
            sprites[reticleSprite].flags |= SPRITE_UNSEEN;
        }
        if (!walking() && toward != npc.direction() && turnFree(npc)) {
            turnTowards(npc, toward);
        }
    } else if (!walking() && buttons.pendingDraw == 0U && toward != npc.direction() && turnFree(npc)) {
        turnTowards(npc, toward);
    }
}

u16 pointerShotTarget()
{
    if (!pointerMode()) {
        return 0;
    }
    u16 found = 0;
    if (!itemAt(&found, pointerScreenX(), pointerScreenY()) || found == controlledActor) {
        return 0;
    }
    Item it(found);
    if (it.isNpc()) {
        Npc npc(found);
        return npc.isDead() == 0U ? found : 0;
    }
    return it.family() == FAMILY_BREAKABLE ? found : 0;
}

bool pointerAim(PointerAim *aim)
{
    if (!pointerMode()) {
        return false;
    }
    Npc npc(controlledActor);
    if (!weaponOut(npc) || npc.activeWeapon() == 0U || reticleShown == 0U) {
        return false;
    }
    u16 offsetX;
    u16 offsetY;
    u16 offsetZ;
    fireOffset(static_cast<u8>(npc.direction()), &offsetX, &offsetY, &offsetZ);
    aim->from = ItemPoint(static_cast<u16>(itemX[npc.id] + offsetX), static_cast<u16>(itemY[npc.id] + offsetY),
                          static_cast<u8>(itemZ[npc.id] + offsetZ));
    /* The shots' own point (launchAtReticle): under the pointer, at its
     * height, so the line ends at the reticle. What it meets there, the
     * item under the pointer, is not in the way. */
    const u16 target = pointerShotTarget();
    pointerAimPoint(&aim->to);
    u16 under = 0;
    if (!itemAt(&under, pointerScreenX(), pointerScreenY()) || under == controlledActor) {
        under = 0;
    }
    Sweep sweep;
    ItemPoint start = aim->from;
    const bool clear = sweep.test(SHAPE_SHOT, start, aim->to, MOVE_NO_FLOOR, npc.id, target);
    const int shortBy = std::max({std::abs(static_cast<i16>(sweep.end.x - aim->to.x)),
                                 std::abs(static_cast<i16>(sweep.end.y - aim->to.y)), std::abs(sweep.end.z - aim->to.z)});
    aim->blocked = !clear && sweep.hitItem != 0U && sweep.hitItem != target && sweep.hitItem != under && shortBy > 16;
    aim->stop = aim->blocked ? sweep.end : aim->to;
    /* SuperSprite::launch's spread: a tenth of the distance to where the
     * shot is launched at (past the reticle in No Regret), less for some
     * weapons, at most 80 across and 24 up; scaled back to the point
     * aimed at. */
    const i16 type = weaponOf(Item(npc.activeWeapon()).shape());
    aim->spread = 0;
    aim->rise = 0;
    if (type < 0 || fireType(static_cast<u16>(type))->accurate != 0U) {
        return true;
    }
    const int dx = std::abs(static_cast<i16>(aim->to.x - aim->from.x));
    const int dy = std::abs(static_cast<i16>(aim->to.y - aim->from.y));
    const int dz = std::abs(aim->to.z - aim->from.z);
    const int near = std::max({dx, dy, dz});
    const int launched = (target == 0U && AIM_PAST_RETICLE) ? near * 6 : near;
    int divisor = 10;
    if (type == FIRE_SG_A1 || type == FIRE_AC_88) {
        divisor = 8;
    } else if (type == FIRE_PA) {
        divisor = 12;
    } else if (type == FIRE_AR_7) {
        divisor = 5;
    }
    const int spread = std::min(launched / divisor, 80);
    const int rise = std::min(spread / 3, 24);
    aim->spread = static_cast<i16>(launched > 0 ? spread * near / launched : 0);
    aim->rise = static_cast<i16>(launched > 0 ? rise * near / launched : 0);
    return true;
}
