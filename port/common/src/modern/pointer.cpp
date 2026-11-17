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
#include "sprite_game.h"
#include "hud.h"
#include "itemdata.h"
#include "kernel.h"
#include "keyboard.h"
#include "mousectl.h"
#include "newgump.h"
#include "npc.h"
#include "spmgr.h"
#include "pathwalk.h"
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
 * The right button: when it went down, whether its press drew the weapon
 * (its release then leaves it out), whether it fires (held long enough),
 * whether its press stopped a draw half way; the releases the mode took the
 * presses of. The left button's last click, for the double click. A draw or
 * a holster waits for the Silencer's animation. */
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

/* Ticks the right button is held before it fires; a double click's ticks
 * and pixels apart. */
const u32 FIRE_HOLD = 10;
const u32 DOUBLE_CLICK_TICKS = 16;
const i16 DOUBLE_CLICK_PIXELS = 14;
/* The pointer this close to the Silencer turns him no more. */
const i16 FACE_DEAD_ZONE = 10;

u32 now()
{
    return systemTimer->time();
}

/* The Silencer's weapon out, or being drawn. */
bool weaponOut(Npc &npc)
{
    return npc.isWeaponDrawn() != 0U;
}

bool drawing(Npc &npc)
{
    return npc.isBusy() && npc.lastAnimSet() == ANIM_READY_SMALL;
}

void draw(Npc &npc)
{
    if (!npc.isBusy()) {
        npc.doAnim(ANIM_READY_SMALL, DIRECTION_CURRENT, ONLY_IF_IDLE, 0);
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
    return mouseCtl != nullptr ? mouseCtl->pointerX : 0;
}

i16 pointerScreenY()
{
    return mouseCtl != nullptr ? mouseCtl->pointerY : 0;
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
    /* The pointer goes where the mouse takes it: nothing steers. */
    return pointerMode();
}

bool pointerButton(u8 right, NewGumpEvent *event)
{
    const bool release = event->isMouseRelease();
    if (release) {
        if (right == 0U) {
            const bool taken = buttons.leftTaken != 0U;
            buttons.leftTaken = 0;
            return taken;
        }
        if (buttons.rightDown == 0U) {
            return false;
        }
        buttons.rightDown = 0;
        Npc npc(controlledActor);
        if (buttons.rightFiring != 0U) {
            buttons.rightFiring = 0;
            fireKey(false, 1);
        } else if (pointerMode() && buttons.rightDrew == 0U && (weaponOut(npc) || buttons.rightCancel != 0U)) {
            /* A tap with the weapon out puts it away; half way through
             * the draw, at once. */
            holster(npc, buttons.rightCancel != 0U);
        }
        buttons.rightCancel = 0;
        return true;
    }
    if (!pointerMode() || event->isMouseRepeat()) {
        /* The double click's second press comes as a repeat: a click of its
         * own here. */
        if (!pointerMode()) {
            return false;
        }
    }
    Npc npc(controlledActor);
    const bool isDrawing = drawing(npc);
    const bool out = weaponOut(npc) || isDrawing;
    if (right == 0U) {
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
    walkStop();
    buttons.rightDown = 1;
    buttons.rightDrew = 0;
    buttons.rightFiring = 0;
    buttons.rightCancel = isDrawing ? 1 : 0;
    buttons.rightDownAt = now();
    if (buttons.pendingDraw != 0U) {
        /* A draw still waiting: the second press calls it off. */
        buttons.pendingDraw = 0;
        buttons.rightDrew = 1;
    } else if (!out) {
        buttons.rightDrew = 1;
        buttons.pendingHolster = 0;
        draw(npc);
    }
    return true;
}

bool pointerKey(const KeyEvent &key, Action action)
{
    if (!pointerMode()) {
        return false;
    }
    if (action == Action::MOVE_FORWARD) {
        walkHold(key.pressed != 0U);
        return true;
    }
    if (key.pressed != 0U &&
        (action == Action::MOVE_BACK || action == Action::SLIDE_LEFT || action == Action::SLIDE_RIGHT ||
         action == Action::ROLL_LEFT || action == Action::ROLL_RIGHT || action == Action::KNEEL ||
         action == Action::JUMP || action == Action::ADVANCE || action == Action::RETREAT)) {
        walkStop();
    }
    return false;
}

void pointerTick()
{
    if (!pointerMode()) {
        buttons.rightDown = 0;
        buttons.rightFiring = 0;
        buttons.pendingDraw = 0;
        buttons.pendingHolster = 0;
        walkStop();
        return;
    }
    Npc npc(controlledActor);
    /* The target the original chooses is not the player's: the aim is the
     * pointer's. */
    targeting = 0;
    if (buttons.pendingHolster != 0U && !npc.isBusy()) {
        buttons.pendingHolster = 0;
        if (weaponOut(npc)) {
            holster(npc, false);
        }
    }
    if (buttons.pendingDraw != 0U && !npc.isBusy()) {
        buttons.pendingDraw = 0;
        if (!weaponOut(npc)) {
            npc.doAnim(ANIM_READY_SMALL, DIRECTION_CURRENT, ONLY_IF_IDLE, 0);
        }
    }
    if (buttons.rightDown != 0U && buttons.rightFiring == 0U && now() - buttons.rightDownAt >= FIRE_HOLD) {
        buttons.rightFiring = 1;
        fireKey(true, 1);
    } else if (buttons.rightFiring != 0U) {
        fireKey(true, 2);
    }
    walkTick();
    const i16 x = pointerScreenX();
    const i16 y = pointerScreenY();
    u16 mapX;
    u16 mapY;
    screenToMap(x, y, &mapX, &mapY);
    /* The point under the pointer at the height of the Silencer's shots
     * (placeReticle's): the reticle goes there, and the shots fly to it. */
    u16 muzzleX;
    u16 muzzleZ;
    fireOffset(static_cast<u8>(npc.direction()), &muzzleX, &muzzleX, &muzzleZ);
    const u8 height = static_cast<u8>(itemZ[npc.id] + muzzleZ);
    const i16 lift = static_cast<i16>(height * 2);
    const i16 aimX = static_cast<i16>(mapX + lift);
    const i16 aimY = static_cast<i16>(mapY + lift);
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
        }
        if (!walking() && !npc.isBusy() && toward != npc.direction()) {
            npc.doAnim(npc.isKneeling() != 0U ? ANIM_KNEELING_SMALL : ANIM_COMBAT_STAND_SMALL, static_cast<u8>(toward),
                       ONLY_IF_IDLE, 0);
        }
    } else if (!walking() && !npc.isBusy() && buttons.pendingDraw == 0U && toward != npc.direction()) {
        turnNpc(npc.id, toward, 1, 0);
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
    if (npc.isWeaponDrawn() == 0U || npc.activeWeapon() == 0U || reticleShown == 0U) {
        return false;
    }
    u16 offsetX;
    u16 offsetY;
    u16 offsetZ;
    fireOffset(static_cast<u8>(npc.direction()), &offsetX, &offsetY, &offsetZ);
    aim->from = ItemPoint(static_cast<u16>(itemX[npc.id] + offsetX), static_cast<u16>(itemY[npc.id] + offsetY),
                          static_cast<u8>(itemZ[npc.id] + offsetZ));
    const u16 target = pointerShotTarget();
    if (target != 0U) {
        aimPoint(controlledActor, target, &aim->to);
    } else {
        aim->to = ItemPoint(spriteX(reticleSprite), spriteY(reticleSprite), aim->from.z);
    }
    Sweep sweep;
    ItemPoint start = aim->from;
    aim->blocked = !sweep.test(SHAPE_SHOT, start, aim->to, MOVE_NO_FLOOR, npc.id, target) &&
                   sweep.hitItem != target && sweep.hitItem != 0U;
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
