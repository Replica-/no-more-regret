/* The Silencer's colours. See colours.h. */
#include "colours.h"
#include "gamestate.h"
#include "bindings.h"
#include "control.h"
#include "dispatch.h"
#include "dlitem.h"
#include "drawframe.h"
#include "enhanced.h"
#include "gamepal.h"
#include "hud.h"
#include "keyboard.h"
#include "newgump.h"
#include "npc.h"
#include "overlay.h"
#include "screen.h"
#include "shape.h"
#include "shaphand.h"
#include "viewport.h"
#include "world.h"

#include <algorithm>
#include <cstdint>

namespace {

/* The three ramps: the armour's reds, the logo's gold, the pack's browns. */
const int MAIN = 0;
const int LOGO = 1;
const int PACK = 2;
const int RAMPS = 3;

/* A look: its name and each ramp's colour, 0xRRGGBB. */
struct Look {
    const char *name;
    std::uint32_t rgb[RAMPS];
};
const Look LOOKS[] = {
    {"ORIGINAL", {0x8a0404, 0xff9e20, 0x594928}}, {"STEALTH", {0x2c2f36, 0x9aa0aa, 0x1e2024}},
    {"DESERT", {0xc2a25a, 0x6b4a18, 0x7a5a32}},   {"ARCTIC", {0xe6edf5, 0x3c86d6, 0x8a9bb0}},
    {"TOXIC", {0x37c837, 0xf0f028, 0x2e4a22}},    {"ROYAL", {0x3a3ad0, 0xffd23c, 0x2a1e5a}},
    {"NIGHT", {0x16204a, 0x5adcf0, 0x1a1a2a}},    {"SUNSET", {0xe8601c, 0xffe08a, 0x6b2a18}},
    {"ORCHID", {0xc83c9c, 0xf0e6ff, 0x4a2a5a}},   {"GOLD", {0xd4af37, 0x20202a, 0x5c4a1a}},
    {"OCEAN", {0x0e8c8c, 0xf4f0d0, 0x1c3a4a}},    {"BONE", {0xdcd2b8, 0x8a0404, 0x6a5a44}},
};
const int LOOK_COUNT = static_cast<int>(sizeof(LOOKS) / sizeof(LOOKS[0]));
/* The brightest component of the ramp's shade that takes the look's colour
 * as it is. */
const int REFERENCE[RAMPS] = {138, 255, 89};

u8 &chosenLook = gameSetting<u8>(0);

/* The colour each palette entry is drawn in, for the look it was made for
 * (FFh none). */
u8 table[256];
u8 tableLook = 0xff;

/* A colour's component, 8 bits from the palette's 6. */
int full(u8 component)
{
    return (component * 255) / 63;
}

/* The ramp of a colour, judged from it alone: -1 for the blackish and the
 * greys, which stay; reds, pinks and rust the armour; bright oranges and
 * golds the logo; the duller browns, olives and tans the pack; the cold
 * colours (accents of the armour) the armour. */
int rampOf(int r, int g, int b)
{
    const int high = std::max({r, g, b});
    const int low = std::min({r, g, b});
    if (high * 100 < 16 * 255 || high == 0 || (high - low) * 100 < 20 * high) {
        return -1;
    }
    const float d = static_cast<float>(high - low);
    float hue = 0;
    if (high == r) {
        hue = 60 * (static_cast<float>(g - b) / d);
        if (hue < 0) {
            hue += 360;
        }
    } else if (high == g) {
        hue = 60 * ((static_cast<float>(b - r) / d) + 2);
    } else {
        hue = 60 * ((static_cast<float>(r - g) / d) + 4);
    }
    if (hue >= 335 || hue < 22) {
        return MAIN;
    }
    if (hue < 70) {
        const bool bright = high * 100 > 60 * 255;
        const bool saturated = (high - low) * 100 > 55 * high;
        return bright && saturated ? LOGO : PACK;
    }
    return MAIN;
}

/* The palette's nearest entry to r,g,b (8 bits), but the cycled and glowing
 * ones (0-15). */
u8 nearest(int r, int g, int b)
{
    const RGB *colours = gamePalette->data.get();
    int best = 16;
    int bestDistance = 1 << 30;
    for (int i = 16; i < 256; i++) {
        const int dr = full(colours[i].red) - r;
        const int dg = full(colours[i].green) - g;
        const int db = full(colours[i].blue) - b;
        const int distance = (dr * dr) + (dg * dg) + (db * db);
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return static_cast<u8>(best);
}

/* The table for the look: each shade of a ramp given the look's colour,
 * as bright against it as the shade is against the ramp's reference
 * (lighter ones towards white). */
const u8 *tableFor(u8 look)
{
    if (tableLook == look) {
        return table;
    }
    const RGB *colours = gamePalette->data.get();
    for (int i = 0; i < 256; i++) {
        const int r = full(colours[i].red);
        const int g = full(colours[i].green);
        const int b = full(colours[i].blue);
        const int ramp = rampOf(r, g, b);
        if (ramp < 0) {
            table[i] = static_cast<u8>(i);
            continue;
        }
        const float scale = static_cast<float>(std::max({r, g, b})) / static_cast<float>(REFERENCE[ramp]);
        const std::uint32_t colour = LOOKS[look].rgb[ramp];
        auto shade = [scale](std::uint32_t component) {
            const auto v = static_cast<float>(component & 0xffU);
            const float out =
                scale <= 1 ? v * scale : v + ((255 - v) * std::min(1.0F, (scale - 1) * 0.6F));
            return static_cast<int>(std::clamp(out, 0.0F, 255.0F));
        };
        table[i] = nearest(shade(colour >> 16), shade(colour >> 8), shade(colour));
    }
    tableLook = look;
    return table;
}

/* The choice: the look lit, the frame of the Silencer shown. */
class ColoursGump : public NewGump {
public:
    ColoursGump();
    void handleLeftButton(NewGumpEvent *event) override;
    void handleRightButton(NewGumpEvent * /*event*/) override { close(); }
    void handleMouseMotion(NewGumpEvent *event) override;
    void handleKeyboard(NewGumpEvent *event) override;
    void paintSelf(i16 /*x*/, i16 /*y*/) override {}
};

ColoursGump *&openGump = gameGlobal<ColoursGump *>(nullptr);

/* The list: its rows' left, top and size; the preview's middle. */
const i16 LIST_X = 120;
const i16 LIST_Y = 92;
const i16 ROW = 24;
const i16 ROW_WIDTH = 220;
const i16 PREVIEW_X = 470;
const i16 PREVIEW_Y = 250;
const int ZOOM = 3;

i16 rowAt(i16 x, i16 y)
{
    if (x < LIST_X || x >= LIST_X + ROW_WIDTH || y < LIST_Y) {
        return -1;
    }
    const int row = (y - LIST_Y) / ROW;
    return row < LOOK_COUNT ? static_cast<i16>(row) : -1;
}

ColoursGump::ColoursGump() : NewGump(NewGumpId(), nullptr, 0)
{
    eventMask |= EVENT_LEFT_BUTTON | EVENT_RIGHT_BUTTON | EVENT_MOTION | EVENT_KEY;
    bounds.set(0, 0, static_cast<s16>(drawViewport->right() - drawViewport->left()),
               static_cast<s16>(drawViewport->bottom() - drawViewport->top()));
    addAcceptedArguments(-1, 0);
}

void choose(i16 look)
{
    if (look >= 0 && look < LOOK_COUNT && look != chosenLook) {
        chosenLook = static_cast<u8>(look);
        /* The Silencer drawn again in it. */
        redrawItem(1);
    }
}

void ColoursGump::handleLeftButton(NewGumpEvent *event)
{
    if (!event->isMousePress()) {
        return;
    }
    const i16 row = rowAt(eventX(event), eventY(event));
    if (row < 0) {
        close();
        return;
    }
    choose(row);
}

void ColoursGump::handleMouseMotion(NewGumpEvent * /*event*/) {}

void ColoursGump::handleKeyboard(NewGumpEvent *event)
{
    i16 code = event->argument;
    const Action action = worldCode(event->argument, &code) ? worldAction(code) : Action::NONE;
    if (action == Action::CHARACTER_COLOURS) {
        close();
        return;
    }
    switch (event->argument) {
    case ASCII_ESCAPE:
    case '\r':
    case ' ':
        close();
        break;
    case KEYCODE_SCAN | SCAN_UP:
        choose(static_cast<i16>((chosenLook + LOOK_COUNT - 1) % LOOK_COUNT));
        break;
    case KEYCODE_SCAN | SCAN_DOWN:
        choose(static_cast<i16>((chosenLook + 1) % LOOK_COUNT));
        break;
    default:
        break;
    }
}

/* The preview's pixels: the Silencer's frame in the look, then each pixel
 * ZOOM times. */
const int PREVIEW = 160;
u8 previewPixels[PREVIEW * PREVIEW];
u32 previewRows[PREVIEW];
std::uint32_t previewPicture[PREVIEW * ZOOM * PREVIEW * ZOOM];

void preview()
{
    Npc npc(1);
    if (!npc.isValid()) {
        return;
    }
    ShapeFrame *frame = cachedShapeFrame(npc.shape(), npc.frame());
    if (frame == nullptr) {
        return;
    }
    std::fill(std::begin(previewPixels), std::end(previewPixels), static_cast<u8>(TRANSPARENT));
    for (int y = 0; y < PREVIEW; y++) {
        previewRows[y] = static_cast<u32>(y * PREVIEW);
    }
    const ViewportPixels pixels{previewPixels, previewRows, 0, 0, PREVIEW - 1, PREVIEW - 1};
    const u8 *look = tableFor(chosenLook);
    const auto put = [look](u8 &out, u8 in) { out = look[in]; };
    const auto *bytes = reinterpret_cast<const u8 *>(frame);
    /* Its feet a little below the middle. */
    const s32 x = PREVIEW / 2;
    const s32 y = (PREVIEW * 3) / 4;
    if ((itemFlags[npc.id] & ITEM_FLIPPED) != 0) {
        drawframe::draw<true>(pixels, x, y, bytes, put);
    } else {
        drawframe::draw<false>(pixels, x, y, bytes, put);
    }
    const RGB *colours = gamePalette->data.get();
    const int side = PREVIEW * ZOOM;
    for (int y0 = 0; y0 < side; y0++) {
        for (int x0 = 0; x0 < side; x0++) {
            const u8 pixel = previewPixels[((y0 / ZOOM) * PREVIEW) + (x0 / ZOOM)];
            std::uint32_t &out = previewPicture[(y0 * side) + x0];
            if (pixel == TRANSPARENT) {
                out = 0;
                continue;
            }
            const RGB &c = colours[pixel];
            out = (static_cast<std::uint32_t>(c.red) << 26) | (static_cast<std::uint32_t>(c.green) << 18) |
                  (static_cast<std::uint32_t>(c.blue) << 10) | 0xffU;
        }
    }
    overlayPicture(previewPicture, side, side, static_cast<float>(PREVIEW_X - (side / 2)),
                   static_cast<float>(PREVIEW_Y - (side / 2)), static_cast<float>(side), static_cast<float>(side));
}

std::uint32_t opaque(std::uint32_t rgb)
{
    return (rgb << 8) | 0xffU;
}

}

u8 coloursSetting()
{
    return chosenLook;
}

void setColoursSetting(u8 look)
{
    chosenLook = look < LOOK_COUNT ? look : 0;
}

bool drawInColours(Viewport &view, i16 x, i16 y, ShapeFrame *frame, bool mirrored)
{
    if (chosenLook == 0U) {
        return false;
    }
    const u8 *look = tableFor(chosenLook);
    const auto put = [look](u8 &out, u8 in) { out = look[in]; };
    const auto *bytes = reinterpret_cast<const u8 *>(frame);
    if (mirrored) {
        drawframe::draw<true>(view.pixels(), x, y, bytes, put);
    } else {
        drawframe::draw<false>(view.pixels(), x, y, bytes, put);
    }
    return true;
}

void openColours()
{
    if (openGump != nullptr) {
        return;
    }
    freezeWorld(1);
    openGump = new ColoursGump;
    openModalGump(openGump, static_cast<u32>(-1), [](s32 /*result*/) {
        openGump = nullptr;
        thawWorld(1);
        writeEnhancedOptions();
    });
}

bool drawColours()
{
    if (openGump == nullptr) {
        return false;
    }
    overlayBox(-200, 0, SCREEN_WIDTH + 400, SCREEN_HEIGHT, 0x000000a0U);
    overlayBox(LIST_X - 24, LIST_Y - 52, 520, (LOOK_COUNT * ROW) + 96, 0x0a0e16e8U);
    wordsBegin();
    wordsText(14, static_cast<i16>(LIST_X - 8), static_cast<i16>(LIST_Y - 40), "SILENCER COLOURS", 1);
    for (int i = 0; i < LOOK_COUNT; i++) {
        const auto top = static_cast<float>(LIST_Y + (i * ROW));
        if (i == chosenLook) {
            overlayBox(LIST_X - 8, top - 2, ROW_WIDTH + 8, ROW - 2, 0x3476cdc0U);
        }
        for (int ramp = 0; ramp < RAMPS; ramp++) {
            overlayBox(static_cast<float>(LIST_X + (ramp * 18)), top + 2, 14, 14, opaque(LOOKS[i].rgb[ramp]));
        }
        wordsText(13, static_cast<i16>(LIST_X + 64), static_cast<i16>(top + 4), LOOKS[i].name, 1);
    }
    wordsText(13, static_cast<i16>(LIST_X - 8), static_cast<i16>(LIST_Y + (LOOK_COUNT * ROW) + 12),
              "CLICK OR UP AND DOWN CHOOSE. P OR ESC CLOSES.", 1);
    wordsEnd();
    preview();
    return true;
}
