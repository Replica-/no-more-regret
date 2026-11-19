/* The Silencer's death's end. See death.h. */
#include "death.h"
#include "gamestate.h"
#include "crusys.h"
#include "dispatch.h"
#include "hud.h"
#include "keyboard.h"
#include "newgump.h"
#include "npc.h"
#include "overlay.h"
#include "pointer.h"
#include "screen.h"
#include "systimer.h"
#include "world.h"
#include "control.h"

#include <algorithm>

namespace {

/* Ticks he lies dead and still before the screen. */
const u32 STILL = 120;

/* Since when he is dead (0 alive), whether the screen came, and whether
 * he was killed (a hit's death, not a Silencer a map's usecode lays down
 * with no hit points, as mission 2's start), the arena's. */
struct Death {
    u32 since;
    u8 shown;
    u8 killed;
};
Death &death = gameGlobal<Death>();

/* The choices: load a game, the main menu. */
const char *const CHOICES[] = {"LOAD GAME", "MAIN MENU"};
const i16 CHOICE_COUNT = 2;
const i16 MIDDLE_X = SCREEN_WIDTH / 2;
const i16 TITLE_Y = 170;
const i16 FIRST_Y = 240;
const i16 ROW = 30;
const i16 ROW_WIDTH = 200;

class DeathGump : public NewGump {
public:
    DeathGump();
    void handleLeftButton(NewGumpEvent *event) override;
    void handleMouseMotion(NewGumpEvent *event) override;
    void handleKeyboard(NewGumpEvent *event) override;
    void paintSelf(i16 /*x*/, i16 /*y*/) override {}
    /* Closes it with the lit choice taken: the gump is gone by the time
     * the wait's rest runs, so the choice is kept apart (chosen). */
    void choose();

    i16 lit = 0;
};

DeathGump *&openGump = gameGlobal<DeathGump *>(nullptr);
/* The choice the gump closed with. */
i16 &chosen = gameGlobal<i16>(0);

i16 choiceAt(i16 x, i16 y)
{
    if (x < MIDDLE_X - (ROW_WIDTH / 2) || x > MIDDLE_X + (ROW_WIDTH / 2) || y < FIRST_Y - 6) {
        return -1;
    }
    const int row = (y - (FIRST_Y - 6)) / ROW;
    return row < CHOICE_COUNT ? static_cast<i16>(row) : -1;
}

DeathGump::DeathGump() : NewGump(NewGumpId(), nullptr, 0)
{
    eventMask |= EVENT_LEFT_BUTTON | EVENT_MOTION | EVENT_KEY;
    bounds.set(0, 0, static_cast<s16>(drawViewport->right() - drawViewport->left()),
               static_cast<s16>(drawViewport->bottom() - drawViewport->top()));
    addAcceptedArguments(-1, 0);
}

void DeathGump::handleLeftButton(NewGumpEvent *event)
{
    if (!event->isMousePress()) {
        return;
    }
    const i16 choice = choiceAt(eventX(event), eventY(event));
    if (choice >= 0) {
        lit = choice;
        choose();
    }
}

void DeathGump::handleMouseMotion(NewGumpEvent *event)
{
    const i16 choice = choiceAt(eventX(event), eventY(event));
    if (choice >= 0) {
        lit = choice;
    }
}

void DeathGump::handleKeyboard(NewGumpEvent *event)
{
    switch (event->argument) {
    case '\r':
    case ' ':
        choose();
        break;
    case ASCII_ESCAPE:
        lit = 1;
        choose();
        break;
    case KEYCODE_SCAN | SCAN_UP:
    case KEYCODE_SCAN | SCAN_DOWN:
        lit = static_cast<i16>(1 - lit);
        break;
    default:
        break;
    }
}

void DeathGump::choose()
{
    chosen = lit;
    openGump = nullptr;
    close();
}

void openDeath()
{
    openGump = new DeathGump;
    openModalGump(openGump, static_cast<u32>(-1), [](s32 /*result*/) {
        if (chosen == 0) {
            openLoadGame();
        } else {
            static_cast<WorldGump *>(modals[modalTop])->gameMenu();
        }
    });
}

}

void noteSilencerKilled()
{
    /* Not a death the world was frozen for (a mission's start's). */
    if (worldActive != 0U) {
        death = Death{0, 0, 1};
    }
}

void deathTick()
{
    Npc silencer(1);
    if (!silencer.isValid() || silencer.isDead() == 0U) {
        death = Death{0, 0, 0};
        return;
    }
    if (death.shown != 0U || death.killed == 0U || worldActive == 0U || controlledNpcNumber() != 1 ||
        !pointerSetting()) {
        return;
    }
    const u32 now = systemTimer->time();
    if (death.since == 0U) {
        death.since = std::max<u32>(now, 1);
        return;
    }
    if (silencer.isBusy() || now - death.since < STILL) {
        return;
    }
    death.shown = 1;
    openDeath();
}

bool drawDeath()
{
    if (openGump == nullptr) {
        return false;
    }
    overlayBox(-200, 0, SCREEN_WIDTH + 400, SCREEN_HEIGHT, 0x000000a0U);
    overlayBox(MIDDLE_X - 170, TITLE_Y - 30, 340, (FIRST_Y - TITLE_Y) + (CHOICE_COUNT * ROW) + 40, 0x0a0e16e8U);
    wordsBegin();
    wordsText(14, MIDDLE_X, TITLE_Y, "SILENCER TERMINATED", 0);
    for (i16 i = 0; i < CHOICE_COUNT; i++) {
        const auto top = static_cast<float>(FIRST_Y + (i * ROW));
        if (i == openGump->lit) {
            overlayBox(MIDDLE_X - (ROW_WIDTH / 2), top - 6, ROW_WIDTH, ROW - 6, 0x1c3a6ae0U);
        }
        wordsText(13, MIDDLE_X, static_cast<i16>(top), CHOICES[i], 0);
    }
    wordsEnd();
    return true;
}
