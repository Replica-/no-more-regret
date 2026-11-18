/* The port's weapon and item wheels. See wheel.h. */
#include "wheel.h"
#include "gamestate.h"
#include "animprim.h"
#include "bindings.h"
#include "cafont.h"
#include "control.h"
#include "dispatch.h"
#include "dtable.h"
#include "gamepal.h"
#include "gumpshap.h"
#include "hud.h"
#include "itemdata.h"
#include "keyboard.h"
#include "newgump.h"
#include "npc.h"
#include "overlay.h"
#include "pfloat.h"
#include "screen.h"
#include "status.h"
#include "viewport.h"
#include "world.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace {

constexpr int MOST = 24;
/* The wheel's middle and radii, in the screen's pixels. */
constexpr float MIDDLE_X = 320;
constexpr float MIDDLE_Y = 232;
constexpr float INNER = 64;
constexpr float OUTER = 150;

const std::uint32_t DIM = 0x00000078;
const std::uint32_t MIDDLE = 0x080c14e8;
const std::uint32_t SECTOR = 0x121e2ed8;
const std::uint32_t LIT = 0x3476cdf0;
const std::uint32_t IN_HAND = 0xffcd46ff;

/* A thing on the wheel: its item and its equipment slot. */
struct Entry {
    u16 item;
    i16 slot;
};

class WheelGump : public NewGump {
public:
    explicit WheelGump(u8 wheelKind);
    void handleLeftButton(NewGumpEvent *event) override;
    void handleRightButton(NewGumpEvent *event) override;
    void handleMouseMotion(NewGumpEvent *event) override;
    void handleKeyboard(NewGumpEvent *event) override;
    void paintSelf(i16 /*x*/, i16 /*y*/) override {}

    void fill(u8 wheelKind);
    void choose();
    [[nodiscard]] i16 under(i16 x, i16 y) const;

    u8 kind = 0;
    Entry entries[MOST] = {};
    i16 count = 0;
    i16 lit = -1;
    i16 inHand = -1;
};

WheelGump *&openGump = gameGlobal<WheelGump *>(nullptr);

/* The name and the gump shape and frame that show an item: the status
 * display's (status.cpp's statusInfo). */
const char *itemInfo(u16 id, u16 *shape, u16 *frame)
{
    const Item item(id);
    const u16 type = item.shape();
    *shape = 5;
    *frame = 0;
    if (type == SHAPE_CREDITS) {
        *shape = 3;
        *frame = 13;
        return "CREDITS";
    }
    switch (item.family()) {
    case FAMILY_WEAPON:
        *shape = 3;
        *frame = weaponFrame(type);
        return weaponName(type);
    case FAMILY_AMMO:
        *frame = ammoFrame(type);
        return ammoName(type);
    case FAMILY_BOMB:
        *shape = 4;
        *frame = bombFrame(type);
        return bombName(type);
    case FAMILY_INVENTORY:
        *frame = inventoryFrame(type);
        return inventoryName(type);
    default:
        return nullptr;
    }
}

WheelGump::WheelGump(u8 wheelKind) : NewGump(NewGumpId(), nullptr, 0)
{
    eventMask |= EVENT_LEFT_BUTTON | EVENT_RIGHT_BUTTON | EVENT_MOTION | EVENT_KEY;
    bounds.set(0, 0, static_cast<s16>(drawViewport->right() - drawViewport->left()),
               static_cast<s16>(drawViewport->bottom() - drawViewport->top()));
    addAcceptedArguments(-1, 0);
    fill(wheelKind);
}

/* The weapons in the weapon slots, or the items in the pack, as the next
 * weapon and next item keys go through them. */
void WheelGump::fill(u8 wheelKind)
{
    kind = wheelKind;
    count = 0;
    lit = -1;
    inHand = -1;
    const SlotRange range = kind == 0 ? WEAPON_SLOTS : PACK_SLOTS;
    const u16 current = kind == 0 ? controlledNpc.activeWeaponSlot() : controlledNpc.activeSlot3();
    for (i16 slot = range.first; slot <= range.last && count < MOST; slot++) {
        const u16 item = controlledNpc.equipment(slot);
        if (item == 0U) {
            continue;
        }
        if (slot == static_cast<i16>(current)) {
            inHand = count;
            lit = count;
        }
        entries[count++] = Entry{item, slot};
    }
}

/* The thing in hand: the weapon, as nextWeapon takes it (the combat
 * stance again for its size), or the item, as nextInventoryItem. */
void WheelGump::choose()
{
    if (lit < 0 || lit >= count) {
        return;
    }
    const Entry &entry = entries[lit];
    if (kind == 1) {
        controlledNpc.setActiveSlot3(static_cast<u16>(entry.slot));
        updateStatus(STATUS_ITEM);
        return;
    }
    controlledNpc.setActiveWeapon(static_cast<u16>(entry.slot));
    updateStatus(STATUS_WEAPON);
    updateStatus(STATUS_AMMO);
    i16 anim = ANIMS;
    if (controlledNpc.isKneeling() != 0U) {
        anim = controlledNpc.usesSmallAnims() ? ANIM_KNEELING_SMALL : ANIM_KNEELING_LARGE;
    } else if (!inPeacefulStance()) {
        anim = controlledNpc.usesSmallAnims() ? ANIM_COMBAT_STAND_SMALL : ANIM_COMBAT_STAND_LARGE;
    }
    if (anim != ANIMS && !controlledNpc.isBusy()) {
        setFirstFrame(1, anim);
    }
}

/* The entry at screen point x,y: the first at the top, the rest clockwise;
 * -1 inside the middle or outside the wheel. */
i16 WheelGump::under(i16 x, i16 y) const
{
    const float dx = static_cast<float>(x) - MIDDLE_X;
    const float dy = static_cast<float>(y) - MIDDLE_Y;
    const float distance = (dx * dx) + (dy * dy);
    if (count == 0 || distance < (INNER * INNER * 0.5F) || distance > ((OUTER + 60) * (OUTER + 60))) {
        return -1;
    }
    const i16 angle = angleOf(static_cast<std::int32_t>(dx), static_cast<std::int32_t>(dy));
    const i16 fromTop = static_cast<i16>((angle + 90 + (180 / count)) % 360);
    return static_cast<i16>((fromTop * count) / 360);
}

void WheelGump::handleMouseMotion(NewGumpEvent *event)
{
    const i16 entry = under(eventX(event), eventY(event));
    if (entry >= 0) {
        lit = entry;
    }
}

void WheelGump::handleLeftButton(NewGumpEvent *event)
{
    const i16 entry = under(eventX(event), eventY(event));
    if (entry >= 0) {
        lit = entry;
        choose();
    }
    close();
}

void WheelGump::handleRightButton(NewGumpEvent * /*event*/)
{
    close();
}

void WheelGump::handleKeyboard(NewGumpEvent *event)
{
    /* The key as the world's gump would take it (dispatch.cpp). */
    i16 code = event->argument;
    const Action action = worldCode(event->argument, &code) ? worldAction(code) : Action::NONE;
    if (action == Action::WEAPON_WHEEL || action == Action::ITEM_WHEEL) {
        const u8 wanted = action == Action::WEAPON_WHEEL ? 0 : 1;
        if (wanted == kind) {
            close();
        } else {
            fill(wanted);
        }
        return;
    }
    switch (event->argument) {
    case ASCII_ESCAPE:
        close();
        break;
    case '\r':
    case ' ':
        choose();
        close();
        break;
    case KEYCODE_SCAN | SCAN_LEFT:
    case KEYCODE_SCAN | SCAN_UP:
        if (count > 0) {
            lit = static_cast<i16>(lit <= 0 ? count - 1 : lit - 1);
        }
        break;
    case KEYCODE_SCAN | SCAN_RIGHT:
    case KEYCODE_SCAN | SCAN_DOWN:
        if (count > 0) {
            lit = static_cast<i16>((lit + 1) % count);
        }
        break;
    default:
        break;
    }
}

/* The point at degrees (0 along the screen's x, 90 down) radius from the
 * middle, by the game's sines (pfloat.h). */
void around(float degrees, float radius, float *x, float *y)
{
    PFloat angle(static_cast<i16>((static_cast<int>(degrees + 360.5F)) % 360), 0);
    *x = MIDDLE_X + (radius * static_cast<float>(cosine(angle).value) / 65536.0F);
    *y = MIDDLE_Y + (radius * static_cast<float>(sine(angle).value) / 65536.0F);
}

/* A ring's sector from degree a0 to a1 between two radii. */
void sector(float a0, float a1, float r0, float r1, std::uint32_t colour)
{
    const int steps = std::max(2, static_cast<int>((a1 - a0) / 4));
    for (int i = 0; i < steps; i++) {
        const float b0 = a0 + ((a1 - a0) * static_cast<float>(i) / static_cast<float>(steps));
        const float b1 = a0 + ((a1 - a0) * static_cast<float>(i + 1) / static_cast<float>(steps));
        float x0;
        float y0;
        float x1;
        float y1;
        float x2;
        float y2;
        float x3;
        float y3;
        around(b0, r0, &x0, &y0);
        around(b0, r1, &x1, &y1);
        around(b1, r0, &x2, &y2);
        around(b1, r1, &x3, &y3);
        overlayTriangle(x0, y0, x1, y1, x3, y3, colour);
        overlayTriangle(x0, y0, x3, y3, x2, y2, colour);
    }
}

/* The pictures and the words: drawn by the game into pixels of its own
 * (the game's fonts and gump shapes), then given to the overlay in the
 * palette's colours. */
constexpr std::size_t PIXELS = static_cast<std::size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT;
u8 canvas[PIXELS];
std::uint32_t picture[PIXELS];

void centredText(CAFont &font, i16 x, i16 y, const char *text)
{
    font.draw(static_cast<i16>(x - (font.textWidth(text) / 2)), y, "%s", text);
}

void paintWords(const WheelGump &wheel)
{
    std::fill(std::begin(canvas), std::end(canvas), TRANSPARENT);
    Viewport view(VIEWPORT_LARGE, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, canvas);
    const DrawTarget target(&view);
    CAFont title(14);
    CAFont names(7);
    centredText(title, static_cast<i16>(MIDDLE_X), static_cast<i16>(MIDDLE_Y - 26),
                wheel.kind == 0 ? "WEAPONS" : "ITEMS");
    const float span = 360.0F / static_cast<float>(std::max<i16>(wheel.count, 1));
    for (i16 i = 0; i < wheel.count; i++) {
        u16 shape;
        u16 frame;
        const char *name = itemInfo(wheel.entries[i].item, &shape, &frame);
        const float middle = -90 + (span * static_cast<float>(i));
        float x;
        float y;
        around(middle, (INNER + OUTER) / 2, &x, &y);
        GumpShape icon(shape);
        icon.setFrame(frame);
        const Rect area = icon.dimensions();
        icon.draw(static_cast<u16>(static_cast<i16>(x) - ((area.left + area.right) / 2)),
                  static_cast<u16>(static_cast<i16>(y) - ((area.top + area.bottom) / 2)));
        if (name == nullptr) {
            continue;
        }
        float nx;
        float ny;
        around(middle, OUTER + 10, &nx, &ny);
        const i16 width = names.textWidth(name);
        i16 left = static_cast<i16>(nx);
        if (nx < MIDDLE_X - 20) {
            left = static_cast<i16>(left - width);
        } else if (nx <= MIDDLE_X + 20) {
            left = static_cast<i16>(left - (width / 2));
        }
        const i16 top = static_cast<i16>(ny < MIDDLE_Y ? ny - 10 : ny);
        names.draw(left, top, "%s", name);
    }
    if (wheel.count == 0) {
        centredText(names, static_cast<i16>(MIDDLE_X), static_cast<i16>(MIDDLE_Y), "NONE CARRIED");
    } else if (wheel.lit >= 0) {
        const Item item(wheel.entries[wheel.lit].item);
        u16 shape;
        u16 frame;
        const char *name = itemInfo(item.id, &shape, &frame);
        if (name != nullptr) {
            centredText(names, static_cast<i16>(MIDDLE_X), static_cast<i16>(MIDDLE_Y - 4), name);
        }
        char detail[40] = "";
        if (wheel.kind == 0) {
            const u16 ammo = ammoShape(item.shape());
            if (ammo != 0U) {
                const Item clips(controlledNpc.findItem(ammo));
                std::snprintf(detail, sizeof detail, "%d ROUNDS, %d CLIPS", item.quality(),
                              clips.isValid() ? clips.quality() : 0);
            }
        } else if (item.quality() > 1) {
            std::snprintf(detail, sizeof detail, "X%d", item.quality());
        }
        if (detail[0] != '\0') {
            centredText(names, static_cast<i16>(MIDDLE_X), static_cast<i16>(MIDDLE_Y + 12), detail);
        }
    }
    centredText(names, static_cast<i16>(MIDDLE_X), static_cast<i16>(MIDDLE_Y + OUTER + 34),
                "CLICK OR ENTER CHOOSES   ARROWS MOVE   ESC CLOSES");
    std::fill(std::begin(picture), std::end(picture), 0U);
    const RGB *colours = gamePalette->data.get();
    for (std::size_t i = 0; i < PIXELS; i++) {
        if (canvas[i] != TRANSPARENT) {
            const RGB &c = colours[canvas[i]];
            picture[i] = (static_cast<std::uint32_t>(c.red) << 26) | (static_cast<std::uint32_t>(c.green) << 18) |
                         (static_cast<std::uint32_t>(c.blue) << 10) | 0xffU;
        }
    }
    overlayPicture(picture, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

}

void openWheel(u8 kind)
{
    if (openGump != nullptr) {
        return;
    }
    freezeWorld(1);
    openGump = new WheelGump(kind);
    openModalGump(openGump, static_cast<u32>(-1), [](s32 /*result*/) {
        openGump = nullptr;
        thawWorld(1);
    });
}

bool drawWheel()
{
    if (openGump == nullptr) {
        return false;
    }
    const WheelGump &wheel = *openGump;
    overlayBox(-200, 0, SCREEN_WIDTH + 400, SCREEN_HEIGHT, DIM);
    sector(0, 359.5F, 0, INNER - 4, MIDDLE);
    if (wheel.count > 0) {
        const float span = 360.0F / static_cast<float>(wheel.count);
        const float gap = std::min(1.5F, span * 0.1F);
        for (i16 i = 0; i < wheel.count; i++) {
            const float a0 = -90 + (span * (static_cast<float>(i) - 0.5F)) + gap;
            const float a1 = -90 + (span * (static_cast<float>(i) + 0.5F)) - gap;
            sector(a0, a1, INNER, OUTER, i == wheel.lit ? LIT : SECTOR);
            if (i == wheel.inHand) {
                sector(a0, a1, OUTER - 4, OUTER, IN_HAND);
            }
        }
    }
    paintWords(wheel);
    return true;
}
