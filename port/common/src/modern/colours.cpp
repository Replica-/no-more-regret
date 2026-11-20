/* The Silencer's colours. See colours.h. */
#include "colours.h"
#include "gamestate.h"
#include "animcach.h"
#include "animprim.h"
#include "bindings.h"
#include "camera.h"
#include "control.h"
#include "crusys.h"
#include "dispatch.h"
#include "dlitem.h"
#include "drawframe.h"
#include "enhanced.h"
#include "enhancements.h"
#include "gamepal.h"
#include "hud.h"
#include "imported.h"
#include "keyboard.h"
#include "newgump.h"
#include "npc.h"
#include "overlay.h"
#include "screen.h"
#include "shape.h"
#include "shaphand.h"
#include "systimer.h"
#include "viewport.h"
#include "widesides.h"
#include "world.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

/* The three ramps: the armour's reds, the logo's gold, the pack's browns. */
const int MAIN = 0;
const int LOGO = 1;
const int PACK = 2;
const int RAMPS = 3;

/* Each ramp's colour, 0xRRGGBB. */
struct Colours {
    std::uint32_t rgb[RAMPS];
};
bool operator==(const Colours &a, const Colours &b)
{
    return a.rgb[0] == b.rgb[0] && a.rgb[1] == b.rgb[1] && a.rgb[2] == b.rgb[2];
}

/* A preset: its name and colours; the first the game's own. */
struct Preset {
    const char *name;
    Colours colours;
};
const Preset PRESETS[] = {
    {"ORIGINAL", {{0x8a0404, 0xff9e20, 0x594928}}}, {"STEALTH", {{0x2c2f36, 0x9aa0aa, 0x1e2024}}},
    {"DESERT", {{0xc2a25a, 0x6b4a18, 0x7a5a32}}},   {"ARCTIC", {{0xe6edf5, 0x3c86d6, 0x8a9bb0}}},
    {"TOXIC", {{0x37c837, 0xf0f028, 0x2e4a22}}},    {"ROYAL", {{0x3a3ad0, 0xffd23c, 0x2a1e5a}}},
    {"NIGHT", {{0x16204a, 0x5adcf0, 0x1a1a2a}}},    {"SUNSET", {{0xe8601c, 0xffe08a, 0x6b2a18}}},
    {"ORCHID", {{0xc83c9c, 0xf0e6ff, 0x4a2a5a}}},   {"GOLD", {{0xd4af37, 0x20202a, 0x5c4a1a}}},
    {"OCEAN", {{0x0e8c8c, 0xf4f0d0, 0x1c3a4a}}},    {"BONE", {{0xdcd2b8, 0x8a0404, 0x6a5a44}}},
};
const int PRESET_COUNT = static_cast<int>(sizeof(PRESETS) / sizeof(PRESETS[0]));
/* The quick colours under the square. */
const std::uint32_t QUICK[] = {0xc01010, 0xe8601c, 0xf0c020, 0x37c837, 0x0e8c8c, 0x3a6ae0,
                               0x7a3ad0, 0xc83c9c, 0xf4f4f4, 0x9aa0aa, 0x2c2f36, 0x6b4a18};
const int QUICK_COUNT = static_cast<int>(sizeof(QUICK) / sizeof(QUICK[0]));
/* The brightest component of the ramp's shade that takes the colour as it
 * is. */
const int REFERENCE[RAMPS] = {138, 255, 89};

Colours &chosen = gameSetting<Colours>(PRESETS[0].colours);

/* The colour each palette entry is drawn in, for the colours it was made
 * for. */
u8 table[256];
Colours tableColours{{0, 0, 0}};
bool tableMade = false;

/* A colour's component, 8 bits from the palette's 6. */
int full(u8 component)
{
    return (component * 255) / 63;
}

/* A hue (0-360), 60 degrees a sixth of the circle, of r,g,b at most high
 * and at least low. */
float hueOf(int r, int g, int b, int high, int low)
{
    const auto d = static_cast<float>(high - low);
    if (d <= 0) {
        return 0;
    }
    float hue = 0;
    if (high == r) {
        hue = 60 * (static_cast<float>(g - b) / d);
    } else if (high == g) {
        hue = 60 * ((static_cast<float>(b - r) / d) + 2);
    } else {
        hue = 60 * ((static_cast<float>(r - g) / d) + 4);
    }
    return hue < 0 ? hue + 360 : hue;
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
    const float hue = hueOf(r, g, b, high, low);
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

/* The table for the colours: each shade of a ramp given its colour, as
 * bright against it as the shade is against the ramp's reference (lighter
 * ones towards white). */
const u8 *tableFor(const Colours &colours)
{
    if (tableMade && tableColours == colours) {
        return table;
    }
    const RGB *palette = gamePalette->data.get();
    for (int i = 0; i < 256; i++) {
        const int r = full(palette[i].red);
        const int g = full(palette[i].green);
        const int b = full(palette[i].blue);
        const int ramp = rampOf(r, g, b);
        if (ramp < 0) {
            table[i] = static_cast<u8>(i);
            continue;
        }
        const float scale = static_cast<float>(std::max({r, g, b})) / static_cast<float>(REFERENCE[ramp]);
        const std::uint32_t colour = colours.rgb[ramp];
        auto shade = [scale](std::uint32_t component) {
            const auto v = static_cast<float>(component & 0xffU);
            const float out = scale <= 1 ? v * scale : v + ((255 - v) * std::min(1.0F, (scale - 1) * 0.6F));
            return static_cast<int>(std::clamp(out, 0.0F, 255.0F));
        };
        table[i] = nearest(shade(colour >> 16), shade(colour >> 8), shade(colour));
    }
    tableColours = colours;
    tableMade = true;
    return table;
}

/* A colour as hue (0-360), saturation and value (0-1). */
struct Hsv {
    float h;
    float s;
    float v;
};

std::uint32_t rgbOf(const Hsv &c)
{
    float h = c.h;
    while (h >= 360) {
        h -= 360;
    }
    h = std::max(0.0F, h) / 60;
    const int sixth = static_cast<int>(h);
    const float f = h - static_cast<float>(sixth);
    const float p = c.v * (1 - c.s);
    const float q = c.v * (1 - (c.s * f));
    const float t = c.v * (1 - (c.s * (1 - f)));
    float r = c.v;
    float g = t;
    float b = p;
    switch (sixth % 6) {
    case 1:
        r = q;
        g = c.v;
        b = p;
        break;
    case 2:
        r = p;
        g = c.v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = c.v;
        break;
    case 4:
        r = t;
        g = p;
        b = c.v;
        break;
    case 5:
        r = c.v;
        g = p;
        b = q;
        break;
    default:
        break;
    }
    const auto byte = [](float v) { return static_cast<std::uint32_t>((std::clamp(v, 0.0F, 1.0F) * 255) + 0.5F); };
    return byte(r) << 16 | byte(g) << 8 | byte(b);
}

Hsv hsvOf(std::uint32_t rgb)
{
    const auto r = static_cast<int>(rgb >> 16 & 0xffU);
    const auto g = static_cast<int>(rgb >> 8 & 0xffU);
    const auto b = static_cast<int>(rgb & 0xffU);
    const int high = std::max({r, g, b});
    const int low = std::min({r, g, b});
    return Hsv{hueOf(r, g, b, high, low), high == 0 ? 0 : static_cast<float>(high - low) / static_cast<float>(high),
               static_cast<float>(high) / 255};
}

/* The choice's places, on the 640x480 screen. */
const float PANEL_X = 40;
const float PANEL_Y = 28;
const float PANEL_W = 560;
const float PANEL_H = 446;
const float PICTURE_X = 62;
const float PICTURE_Y = 96;
const float PICTURE_W = 190;
const float PICTURE_H = 236;
const float TAB_X = 276;
const float TAB_Y = 76;
const float TAB_W = 300;
const float TAB_H = 36;
const float TAB_STEP = 40;
const int SQUARE_X = 276;
const int SQUARE_Y = 204;
const int SQUARE_W = 190;
const int SQUARE_H = 126;
const int HUE_X = 476;
const int HUE_W = 22;
const float QUICK_X = 276;
const float QUICK_Y = 344;
const float QUICK_SIZE = 21;
const float QUICK_GAP = 4;
const float PRESET_X = 276;
const float PRESET_Y = 372;
const float PRESET_W = 102;
const float PRESET_H = 20;
const float PRESET_GAP = 4;
const int PRESETS_ROW = 3;
const float BUTTON_Y = 420;
const float BUTTON_W = 88;
const float BUTTON_H = 22;
const float RESET_X = 62;
const float DONE_X = 160;
const char *const RAMP_NAMES[RAMPS] = {"MAIN COLOUR", "CHEST LOGO", "BACKPACK"};
const char *const RAMP_HINTS[RAMPS] = {"THE ARMOUR", "THE EMBLEM", "THE PACK"};

bool inside(i16 x, i16 y, float left, float top, float w, float h)
{
    const auto fx = static_cast<float>(x);
    const auto fy = static_cast<float>(y);
    return fx >= left && fx < left + w && fy >= top && fy < top + h;
}

/* The choice: the ramp being chosen, its colour as hue, saturation and
 * value (the hue kept while the colour is grey), and the drag going on (1
 * the square, 2 the hue bar). */
class ColoursGump : public NewGump {
public:
    ColoursGump();
    ~ColoursGump() override;
    ColoursGump(const ColoursGump &) = delete;
    ColoursGump &operator=(const ColoursGump &) = delete;
    ColoursGump(ColoursGump &&) = delete;
    ColoursGump &operator=(ColoursGump &&) = delete;
    void handleLeftButton(NewGumpEvent *event) override;
    void handleRightButton(NewGumpEvent * /*event*/) override { close(); }
    void handleMouseMotion(NewGumpEvent *event) override;
    void handleKeyboard(NewGumpEvent *event) override;
    void paintSelf(i16 /*x*/, i16 /*y*/) override {}
    void draw() const;

private:
    void pick(i16 x, i16 y);
    void setColour(std::uint32_t rgb, bool keepHsv);
    void choose(int ramp);
    int ramp_ = MAIN;
    Hsv hsv_{};
    int drag_ = 0;
};

ColoursGump *&openGump = gameGlobal<ColoursGump *>(nullptr);

/* The Silencer drawn again in the colours. */
void colouredAgain()
{
    if (Npc(1).isValid()) {
        redrawItem(1);
    }
}

ColoursGump::ColoursGump() : NewGump(NewGumpId(), nullptr, 0)
{
    eventMask |= EVENT_LEFT_BUTTON | EVENT_RIGHT_BUTTON | EVENT_MOTION | EVENT_KEY;
    bounds.set(0, 0, static_cast<s16>(drawViewport->right() - drawViewport->left()),
               static_cast<s16>(drawViewport->bottom() - drawViewport->top()));
    addAcceptedArguments(-1, 0);
    hsv_ = hsvOf(chosen.rgb[ramp_]);
    openGump = this;
}

ColoursGump::~ColoursGump()
{
    if (openGump == this) {
        openGump = nullptr;
    }
}

void ColoursGump::choose(int ramp)
{
    ramp_ = ramp;
    hsv_ = hsvOf(chosen.rgb[ramp_]);
}

void ColoursGump::setColour(std::uint32_t rgb, bool keepHsv)
{
    chosen.rgb[ramp_] = rgb;
    if (!keepHsv) {
        hsv_ = hsvOf(rgb);
    }
    colouredAgain();
}

void ColoursGump::pick(i16 x, i16 y)
{
    if (drag_ == 1) {
        hsv_.s = std::clamp(static_cast<float>(x - SQUARE_X) / (SQUARE_W - 1), 0.0F, 1.0F);
        hsv_.v = 1 - std::clamp(static_cast<float>(y - SQUARE_Y) / (SQUARE_H - 1), 0.0F, 1.0F);
        setColour(rgbOf(hsv_), true);
    } else if (drag_ == 2) {
        hsv_.h = std::clamp(static_cast<float>(y - SQUARE_Y) / (SQUARE_H - 1), 0.0F, 1.0F) * 359;
        setColour(rgbOf(hsv_), true);
    }
}

void ColoursGump::handleLeftButton(NewGumpEvent *event)
{
    if (event->isMouseRelease()) {
        drag_ = 0;
        return;
    }
    if (!event->isMousePress()) {
        return;
    }
    const i16 x = eventX(event);
    const i16 y = eventY(event);
    for (int ramp = 0; ramp < RAMPS; ramp++) {
        if (inside(x, y, TAB_X, TAB_Y + (TAB_STEP * static_cast<float>(ramp)), TAB_W, TAB_H)) {
            choose(ramp);
            return;
        }
    }
    if (inside(x, y, SQUARE_X, SQUARE_Y, SQUARE_W, SQUARE_H)) {
        drag_ = 1;
        pick(x, y);
        return;
    }
    if (inside(x, y, HUE_X, SQUARE_Y, HUE_W, SQUARE_H)) {
        drag_ = 2;
        pick(x, y);
        return;
    }
    for (int i = 0; i < QUICK_COUNT; i++) {
        if (inside(x, y, QUICK_X + ((QUICK_SIZE + QUICK_GAP) * static_cast<float>(i)), QUICK_Y, QUICK_SIZE,
                   QUICK_SIZE)) {
            setColour(QUICK[i], false);
            return;
        }
    }
    for (int i = 0; i < PRESET_COUNT; i++) {
        const float left = PRESET_X + ((PRESET_W + PRESET_GAP) * static_cast<float>(i % PRESETS_ROW));
        const float top = PRESET_Y + ((PRESET_H + PRESET_GAP) * static_cast<float>(i / PRESETS_ROW));
        if (inside(x, y, left, top, PRESET_W, PRESET_H)) {
            chosen = PRESETS[i].colours;
            choose(ramp_);
            colouredAgain();
            return;
        }
    }
    if (inside(x, y, RESET_X, BUTTON_Y, BUTTON_W, BUTTON_H)) {
        chosen = PRESETS[0].colours;
        choose(ramp_);
        colouredAgain();
    } else if (inside(x, y, DONE_X, BUTTON_Y, BUTTON_W, BUTTON_H)) {
        close();
    }
}

void ColoursGump::handleMouseMotion(NewGumpEvent *event)
{
    if (drag_ != 0) {
        pick(eventX(event), eventY(event));
    }
}

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
        close();
        break;
    case KEYCODE_SCAN | SCAN_UP:
        choose((ramp_ + RAMPS - 1) % RAMPS);
        break;
    case KEYCODE_SCAN | SCAN_DOWN:
        choose((ramp_ + 1) % RAMPS);
        break;
    default:
        break;
    }
}

std::uint32_t opaque(std::uint32_t rgb)
{
    return (rgb << 8) | 0xffU;
}

/* An outline of a box, a pixel wide. */
void outline(float x, float y, float w, float h, std::uint32_t colour)
{
    overlayBox(x, y, w, 1, colour);
    overlayBox(x, y + h - 1, w, 1, colour);
    overlayBox(x, y, 1, h, colour);
    overlayBox(x + w - 1, y, 1, h, colour);
}

/* The preview's pixels: the Silencer standing, turning round a direction
 * at a time, in the colours, each pixel ZOOM times. */
const int PREVIEW = 120;
const int ZOOM = 2;
u8 previewPixels[PREVIEW * PREVIEW];
u32 previewRows[PREVIEW];
std::uint32_t previewPicture[PREVIEW * ZOOM * PREVIEW * ZOOM];
/* The Silencer's shape (animcach.h). */
const u16 SILENCER_SHAPE = 1;

void exactColours(std::uint32_t *out, bool faded);

void preview()
{
    AnimFrame step;
    const auto direction = static_cast<u8>(((systemTimer->time() / 40) % 8) * 2);
    getAnimFrame(SILENCER_SHAPE, ANIM_STAND, direction, 0, &step);
    ShapeFrame *frame = cachedShapeFrame(SILENCER_SHAPE, animShapeFrame(step));
    if (frame == nullptr) {
        return;
    }
    std::fill(std::begin(previewPixels), std::end(previewPixels), static_cast<u8>(TRANSPARENT));
    for (int y = 0; y < PREVIEW; y++) {
        previewRows[y] = static_cast<u32>(y * PREVIEW);
    }
    const ViewportPixels pixels{previewPixels, previewRows, 0, 0, PREVIEW - 1, PREVIEW - 1};
    /* With the hardware scene, in the game's colours, made his exactly
     * below; else in the palette's nearest. */
    const bool exactly = hardwareSetting();
    static std::uint32_t exact[256];
    if (exactly) {
        exactColours(exact, false);
    }
    const u8 *look = tableFor(chosen);
    const auto put = [look, exactly](u8 &out, u8 in) { out = exactly ? in : look[in]; };
    const auto *bytes = reinterpret_cast<const u8 *>(frame);
    /* Its feet near the bottom. */
    const s32 x = PREVIEW / 2;
    const s32 y = PREVIEW - 12;
    if (step.flipped != 0U) {
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
            if (exactly && exact[pixel] != 0U) {
                out = exact[pixel];
                continue;
            }
            const RGB &c = colours[pixel];
            out = (static_cast<std::uint32_t>(full(c.red)) << 24) | (static_cast<std::uint32_t>(full(c.green)) << 16) |
                  (static_cast<std::uint32_t>(full(c.blue)) << 8) | 0xffU;
        }
    }
    const float middle = PICTURE_X + (PICTURE_W / 2);
    overlayPicture(previewPicture, side, side, middle - static_cast<float>(side / 2), PICTURE_Y + PICTURE_H - side - 4,
                   static_cast<float>(side), static_cast<float>(side));
}

/* The square of the hue's shades (saturation across, value down) and the
 * bar of hues. */
std::uint32_t squarePicture[SQUARE_W * SQUARE_H];
std::uint32_t huePicture[HUE_W * SQUARE_H];

const std::uint32_t PANEL = 0x0a0e16f0;
const std::uint32_t EDGE = 0x506ea0ff;

void ColoursGump::draw() const
{
    overlayBox(-1000, -1000, 3000, 3000, 0x000000ff);
    overlayBox(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL);
    outline(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, EDGE);
    overlayBox(PICTURE_X, PICTURE_Y, PICTURE_W, PICTURE_H, 0x1a202cff);
    outline(PICTURE_X, PICTURE_Y, PICTURE_W, PICTURE_H, EDGE);
    for (int ramp = 0; ramp < RAMPS; ramp++) {
        const float top = TAB_Y + (TAB_STEP * static_cast<float>(ramp));
        const bool on = ramp == ramp_;
        overlayBox(TAB_X, top, TAB_W, TAB_H, on ? 0x1e3a66ffU : 0x18202effU);
        outline(TAB_X, top, TAB_W, TAB_H, on ? 0x96c8ffffU : EDGE);
        overlayBox(TAB_X + 5, top + 7, 22, 22, opaque(chosen.rgb[ramp]));
        outline(TAB_X + 5, top + 7, 22, 22, 0x000000ff);
    }
    for (int y = 0; y < SQUARE_H; y++) {
        const float value = 1 - (static_cast<float>(y) / (SQUARE_H - 1));
        for (int x = 0; x < SQUARE_W; x++) {
            squarePicture[(y * SQUARE_W) + x] =
                opaque(rgbOf(Hsv{hsv_.h, static_cast<float>(x) / (SQUARE_W - 1), value}));
        }
        const std::uint32_t hue = opaque(rgbOf(Hsv{static_cast<float>(y) * 359 / (SQUARE_H - 1), 1, 1}));
        std::fill_n(&huePicture[y * HUE_W], HUE_W, hue);
    }
    overlayPicture(squarePicture, SQUARE_W, SQUARE_H, SQUARE_X, SQUARE_Y, SQUARE_W, SQUARE_H);
    outline(SQUARE_X, SQUARE_Y, SQUARE_W, SQUARE_H, EDGE);
    overlayPicture(huePicture, HUE_W, SQUARE_H, HUE_X, SQUARE_Y, HUE_W, SQUARE_H);
    outline(HUE_X, SQUARE_Y, HUE_W, SQUARE_H, EDGE);
    const float markX = SQUARE_X + (hsv_.s * (SQUARE_W - 1));
    const float markY = SQUARE_Y + ((1 - hsv_.v) * (SQUARE_H - 1));
    outline(markX - 5, markY - 5, 10, 10, 0x000000ff);
    outline(markX - 4, markY - 4, 8, 8, 0xffffffff);
    const float hueY = SQUARE_Y + (hsv_.h / 359 * (SQUARE_H - 1));
    outline(HUE_X - 3, hueY - 3, HUE_W + 6, 6, 0x000000ff);
    outline(HUE_X - 2, hueY - 2, HUE_W + 4, 4, 0xffffffff);
    for (int i = 0; i < QUICK_COUNT; i++) {
        const float left = QUICK_X + ((QUICK_SIZE + QUICK_GAP) * static_cast<float>(i));
        overlayBox(left, QUICK_Y, QUICK_SIZE, QUICK_SIZE, opaque(QUICK[i]));
        outline(left, QUICK_Y, QUICK_SIZE, QUICK_SIZE, 0x000000ff);
    }
    for (int i = 0; i < PRESET_COUNT; i++) {
        const float left = PRESET_X + ((PRESET_W + PRESET_GAP) * static_cast<float>(i % PRESETS_ROW));
        const float top = PRESET_Y + ((PRESET_H + PRESET_GAP) * static_cast<float>(i / PRESETS_ROW));
        const bool on = chosen == PRESETS[i].colours;
        overlayBox(left, top, PRESET_W, PRESET_H, on ? 0x1e3a66ffU : 0x18202effU);
        outline(left, top, PRESET_W, PRESET_H, on ? 0x96c8ffffU : EDGE);
        for (int ramp = 0; ramp < RAMPS; ramp++) {
            overlayBox(left + 3, top + 3 + static_cast<float>(ramp * 5), 6, 4, opaque(PRESETS[i].colours.rgb[ramp]));
        }
    }
    for (const float left : {RESET_X, DONE_X}) {
        overlayBox(left, BUTTON_Y, BUTTON_W, BUTTON_H, 0x18202effU);
        outline(left, BUTTON_Y, BUTTON_W, BUTTON_H, EDGE);
    }
    wordsBegin();
    wordsText(7, 320, static_cast<i16>(PANEL_Y + 18), "CHARACTER COLOURS", 0);
    for (int ramp = 0; ramp < RAMPS; ramp++) {
        const auto top = static_cast<i16>(TAB_Y + (TAB_STEP * static_cast<float>(ramp)));
        wordsText(7, static_cast<i16>(TAB_X + 34), static_cast<i16>(top + 5), RAMP_NAMES[ramp], 1);
        wordsText(7, static_cast<i16>(TAB_X + 34), static_cast<i16>(top + 19), RAMP_HINTS[ramp], 1);
        char hex[8];
        std::snprintf(hex, sizeof hex, "%06X", static_cast<unsigned>(chosen.rgb[ramp]));
        wordsText(7, static_cast<i16>(TAB_X + TAB_W - 8), static_cast<i16>(top + 12), hex, -1);
    }
    for (int i = 0; i < PRESET_COUNT; i++) {
        const float left = PRESET_X + ((PRESET_W + PRESET_GAP) * static_cast<float>(i % PRESETS_ROW));
        const float top = PRESET_Y + ((PRESET_H + PRESET_GAP) * static_cast<float>(i / PRESETS_ROW));
        wordsText(7, static_cast<i16>(left + 13), static_cast<i16>(top + 5), PRESETS[i].name, 1);
    }
    wordsText(7, static_cast<i16>(PICTURE_X + (PICTURE_W / 2)), static_cast<i16>(PICTURE_Y + PICTURE_H + 10),
              "PREVIEW", 0);
    wordsText(7, static_cast<i16>(RESET_X + (BUTTON_W / 2)), static_cast<i16>(BUTTON_Y + 6), "RESET", 0);
    wordsText(7, static_cast<i16>(DONE_X + (BUTTON_W / 2)), static_cast<i16>(BUTTON_Y + 6), "DONE", 0);
    wordsText(7, static_cast<i16>(RESET_X), static_cast<i16>(BUTTON_Y + 32), "ESC CLOSES", 1);
    wordsEnd();
    preview();
}

/* The hardware scene's layer: the pixels drawn in the palette that are
 * shown in their colours exactly, the Silencer's and the blood's: for each
 * pixel of the frame the palette entry written and 1 (0 for none), its
 * colour (0 for the Silencer's colours of the entry, else 0xRRGGBBFF), and
 * those pixels. Its pixels are the wide screen's frame's (widesides.h): the
 * screen in its middle, the sides round it; LAYER_FRAME_WIDTH a row. */
const int LAYER_FRAME_WIDTH = WIDE_FRAME_MOST;
const int LAYER_FRAME_HEIGHT = TALL_FRAME_MOST;
u16 layerMap[LAYER_FRAME_WIDTH * LAYER_FRAME_HEIGHT];
std::uint32_t layerColour[LAYER_FRAME_WIDTH * LAYER_FRAME_HEIGHT];
/* The Silencer's pixels' entries of the game's colours (layerMap has the
 * palette's nearest to his colours, which the screen shows under). */
u8 layerSource[LAYER_FRAME_WIDTH * LAYER_FRAME_HEIGHT];
/* The camera's scroll the screen's pixels were kept at: the camera moves
 * the screen's pixels when it scrolls, without drawing them again. */
i16 layerScrollX = 0;
i16 layerScrollY = 0;
/* For the move of the pixels kept. */
u32 movedAt[262144];
u16 movedMap[262144];
std::uint32_t movedColour[262144];
u8 movedSource[262144];
/* The frame they were kept for: its columns left of the screen and rows
 * above it. */
int layerLeft = 0;
int layerTop = 0;
const std::size_t LAYER_MOST = 262144;
void followScroll();
u32 layerPixels[LAYER_MOST];
std::size_t layerCount = 0;
/* The layer's picture, its box's pixels. */
std::uint32_t layerPicture[LAYER_FRAME_WIDTH * LAYER_FRAME_HEIGHT];

/* The box of the pixels his last drawing wrote (the camera's draws of a
 * tick: its dirty areas one after the other), and that tick: the layer's
 * pixels are only his within it (another of the screen's may have his
 * colour by chance where he stood before). */
int passLeft = 0;
int passTop = 0;
int passRight = -1;
int passBottom = -1;
u32 passTime = 0;

/* The frame now: its columns left of the screen and rows above it; the
 * layer cleared when they changed. */
void clearLayer();
void followFrame()
{
    const int left = (std::min<int>(screenWideWidth(), LAYER_FRAME_WIDTH) - SCREEN_WIDTH) / 2;
    const int top = (std::min<int>(screenWideHeight(), LAYER_FRAME_HEIGHT) - SCREEN_HEIGHT) / 2;
    if (left != layerLeft || top != layerTop) {
        clearLayer();
        layerLeft = left;
        layerTop = top;
    }
}

void clearLayer()
{
    for (std::size_t i = 0; i < layerCount; i++) {
        layerMap[layerPixels[i]] = 0;
    }
    layerCount = 0;
}

/* A pixel at address of a view's pixels as the layer's place on the frame:
 * the screen's (drawViewport's) or the wide screen's sides'; false for
 * another view or off the frame. */
bool framePixel(const ViewportPixels &pixels, const u8 *address, u32 *at)
{
    int sidesWidth = 0;
    int sidesHeight = 0;
    const u8 *sides = wideSidesPixels(&sidesWidth, &sidesHeight);
    const bool onScreen = drawViewport != nullptr && pixels.pixels == drawViewport->pixels().pixels;
    if (!onScreen && (sides == nullptr || pixels.pixels != sides)) {
        return false;
    }
    followFrame();
    const u32 pitch = pixels.rows[1] - pixels.rows[0];
    const auto offset = static_cast<u32>(address - pixels.pixels);
    const auto column = static_cast<int>(offset % pitch) + (onScreen ? layerLeft : 0);
    const auto row = static_cast<int>(offset / pitch) + (onScreen ? layerTop : 0);
    if (column >= LAYER_FRAME_WIDTH || row >= LAYER_FRAME_HEIGHT) {
        return false;
    }
    *at = static_cast<u32>((row * LAYER_FRAME_WIDTH) + column);
    return true;
}

/* The pixels kept of the screen moved as the camera's scroll moved since
 * (those of the sides are drawn again each frame, and go as they are no
 * longer theirs). */
void followScroll()
{
    const int dx = layerScrollX - scrollX;
    const int dy = layerScrollY - scrollY;
    layerScrollX = scrollX;
    layerScrollY = scrollY;
    if ((dx == 0 && dy == 0) || layerCount == 0) {
        return;
    }
    const std::size_t count = layerCount;
    for (std::size_t i = 0; i < count; i++) {
        const u32 at = layerPixels[i];
        movedAt[i] = at;
        movedMap[i] = layerMap[at];
        movedColour[i] = layerColour[at];
        movedSource[i] = layerSource[at];
        layerMap[at] = 0;
    }
    layerCount = 0;
    passLeft += dx;
    passRight += dx;
    passTop += dy;
    passBottom += dy;
    for (std::size_t i = 0; i < count; i++) {
        const int column = static_cast<int>(movedAt[i] % LAYER_FRAME_WIDTH) + dx;
        const int row = static_cast<int>(movedAt[i] / LAYER_FRAME_WIDTH) + dy;
        if (column < 0 || column >= LAYER_FRAME_WIDTH || row < 0 || row >= LAYER_FRAME_HEIGHT) {
            continue;
        }
        const auto at = static_cast<u32>((row * LAYER_FRAME_WIDTH) + column);
        if (layerMap[at] == 0U) {
            layerPixels[layerCount++] = at;
        }
        layerMap[at] = movedMap[i];
        layerColour[at] = movedColour[i];
        layerSource[at] = movedSource[i];
    }
}

/* A pixel of the layer: the entry written there and its colour. */
void keepPixel(u32 at, u8 written, std::uint32_t colour)
{
    if (layerMap[at] == 0U) {
        if (layerCount == LAYER_MOST) {
            return;
        }
        layerPixels[layerCount++] = at;
    }
    layerMap[at] = static_cast<u16>(written + 1);
    layerColour[at] = colour;
}

/* Each palette entry of a ramp in the colours as red, green, blue and an
 * alpha of FFh, the others 0: the shade the table gives, but the colour
 * itself, not the palette's nearest; faded, scaled as the screen's palette
 * is against the game's (its fades: not over a menu, whose palette is its
 * own). */
void exactColours(std::uint32_t *out, bool faded)
{
    u8 now[256 * 3];
    screenGetColors(0, 256, now);
    const RGB *palette = gamePalette->data.get();
    for (int i = 0; i < 256; i++) {
        const int r = full(palette[i].red);
        const int g = full(palette[i].green);
        const int b = full(palette[i].blue);
        const int ramp = rampOf(r, g, b);
        if (ramp < 0) {
            out[i] = 0;
            continue;
        }
        const float scale = static_cast<float>(std::max({r, g, b})) / static_cast<float>(REFERENCE[ramp]);
        const std::uint32_t colour = chosen.rgb[ramp];
        const int base[3] = {r, g, b};
        std::uint32_t made = 0xffU;
        for (int k = 0; k < 3; k++) {
            const auto v = static_cast<float>((colour >> (16 - (8 * k))) & 0xffU);
            float shade = scale <= 1 ? v * scale : v + ((255 - v) * std::min(1.0F, (scale - 1) * 0.6F));
            /* The fade: the screen's component against the game's. */
            const int shown = full(static_cast<u8>(now[(i * 3) + k] & 0x3fU));
            if (faded) {
                shade = base[k] > 0 ? shade * static_cast<float>(shown) / static_cast<float>(base[k]) : 0;
            }
            made |= static_cast<std::uint32_t>(std::clamp(shade, 0.0F, 255.0F)) << (24 - (8 * k));
        }
        out[i] = made;
    }
}

/* A colour of six hex digits; false for another text. */
bool hexColour(const std::string &text, std::uint32_t *rgb)
{
    if (text.size() != 6) {
        return false;
    }
    std::uint32_t value = 0;
    for (const char c : text) {
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        }
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    *rgb = value;
    return true;
}

}

std::string coloursText()
{
    char text[32];
    std::snprintf(text, sizeof text, "%06x,%06x,%06x", static_cast<unsigned>(chosen.rgb[0]),
                  static_cast<unsigned>(chosen.rgb[1]), static_cast<unsigned>(chosen.rgb[2]));
    return text;
}

void setColoursText(const std::string &text)
{
    Colours read{};
    std::size_t start = 0;
    for (int ramp = 0; ramp < RAMPS; ramp++) {
        const std::size_t comma = text.find(',', start);
        const std::string part = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!hexColour(part, &read.rgb[ramp])) {
            /* The preset's number, as the port wrote it first. */
            const int preset = std::atoi(text.c_str());
            chosen = PRESETS[preset >= 0 && preset < PRESET_COUNT ? preset : 0].colours;
            return;
        }
        start = comma == std::string::npos ? text.size() : comma + 1;
    }
    chosen = read;
}

bool drawInColours(Viewport &view, i16 x, i16 y, ShapeFrame *frame, bool mirrored)
{
    if (chosen == PRESETS[0].colours) {
        /* His pixels kept before go as the screen no longer shows them
         * (drawColoursLayer); the blood's stay. */
        return false;
    }
    /* With the hardware scene, on the screen or the wide screen's sides: in
     * the game's colours, his pixels kept for the layer at their places on
     * the frame. */
    const ViewportPixels pixels = view.pixels();
    u32 first = 0;
    if (hardwareSetting() && framePixel(pixels, pixels.pixels, &first)) {
        const u32 time = systemTimer->time();
        if (time != passTime) {
            passTime = time;
            passLeft = LAYER_FRAME_WIDTH;
            passTop = LAYER_FRAME_HEIGHT;
            passRight = -1;
            passBottom = -1;
        }
        const u8 *look = tableFor(chosen);
        followScroll();
        const auto keep = [&pixels, look](u8 &out, u8 in) {
            out = look[in];
            u32 at = 0;
            if (!framePixel(pixels, &out, &at)) {
                return;
            }
            const auto column = static_cast<int>(at % LAYER_FRAME_WIDTH);
            const auto row = static_cast<int>(at / LAYER_FRAME_WIDTH);
            passLeft = std::min(passLeft, column);
            passRight = std::max(passRight, column);
            passTop = std::min(passTop, row);
            passBottom = std::max(passBottom, row);
            keepPixel(at, look[in], 0);
            layerSource[at] = in;
        };
        const auto *bytes = reinterpret_cast<const u8 *>(frame);
        if (mirrored) {
            drawframe::draw<true>(pixels, x, y, bytes, keep);
        } else {
            drawframe::draw<false>(pixels, x, y, bytes, keep);
        }
        return true;
    }
    /* Else in the palette's nearest (without the hardware scene). */
    if (!hardwareSetting()) {
        clearLayer();
    }
    const u8 *look = tableFor(chosen);
    const auto put = [look](u8 &out, u8 in) { out = look[in]; };
    const auto *bytes = reinterpret_cast<const u8 *>(frame);
    if (mirrored) {
        drawframe::draw<true>(view.pixels(), x, y, bytes, put);
    } else {
        drawframe::draw<false>(view.pixels(), x, y, bytes, put);
    }
    return true;
}

void keepExact(Viewport &view, u8 *address, u8 written, std::uint32_t rgb)
{
    u32 at = 0;
    if (hardwareSetting() && framePixel(view.pixels(), address, &at)) {
        followScroll();
        keepPixel(at, written, (rgb << 8) | 0xffU);
    }
}

void drawColoursLayer()
{
    if (!hardwareSetting() || layerCount == 0 || worldActive == 0U || worldFrozen != 0U || drawViewport == nullptr) {
        return;
    }
    followFrame();
    followScroll();
    int sidesWidth = 0;
    int sidesHeight = 0;
    const u8 *sides = wideSidesPixels(&sidesWidth, &sidesHeight);
    /* The pixels still his on the frame drawn (the screen in its middle, the
     * sides round it; another item over him, or him moved away: no longer),
     * and their box. */
    const ViewportPixels screen = drawViewport->pixels();
    int left = LAYER_FRAME_WIDTH;
    int top = LAYER_FRAME_HEIGHT;
    int right = -1;
    int bottom = -1;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < layerCount; i++) {
        const u32 at = layerPixels[i];
        const auto column = static_cast<int>(at % LAYER_FRAME_WIDTH);
        const auto row = static_cast<int>(at / LAYER_FRAME_WIDTH);
        const int screenX = column - layerLeft;
        const int screenY = row - layerTop;
        int shown = -1;
        if (screenX >= 0 && screenX < SCREEN_WIDTH && screenY >= 0 && screenY < SCREEN_HEIGHT) {
            shown = screen.pixels[screen.rows[screenY] + screenX];
        } else if (sides != nullptr && column < sidesWidth && row < sidesHeight) {
            shown = sides[(static_cast<std::size_t>(row) * sidesWidth) + column];
        }
        /* The Silencer's only within his last drawing; the blood's as long
         * as the screen keeps them. */
        const bool his = layerColour[at] == 0U;
        if (shown + 1 != layerMap[at] ||
            (his && (column < passLeft || column > passRight || row < passTop || row > passBottom))) {
            layerMap[at] = 0;
            continue;
        }
        layerPixels[kept++] = at;
        left = std::min(left, column);
        right = std::max(right, column);
        top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    layerCount = kept;
    if (right < left) {
        return;
    }
    std::uint32_t exact[256];
    exactColours(exact, true);
    /* The palette's fade of each entry (the screen's against the game's,
     * of 256), for the colours of their own. */
    u8 now[256 * 3];
    screenGetColors(0, 256, now);
    const RGB *palette = gamePalette->data.get();
    const auto fadeOf = [&](u8 entry, int k) {
        const u8 base = k == 0 ? palette[entry].red : k == 1 ? palette[entry].green : palette[entry].blue;
        return base == 0U ? 256 : ((now[(entry * 3) + k] & 0x3f) * 256) / base;
    };
    const int width = right - left + 1;
    const int height = bottom - top + 1;
    std::fill_n(layerPicture, static_cast<std::size_t>(width) * height, 0U);
    for (std::size_t i = 0; i < layerCount; i++) {
        const u32 at = layerPixels[i];
        const auto column = static_cast<int>(at % LAYER_FRAME_WIDTH);
        const auto row = static_cast<int>(at / LAYER_FRAME_WIDTH);
        const auto entry = static_cast<u8>(layerMap[at] - 1);
        std::uint32_t colour = layerColour[at];
        if (colour == 0U) {
            colour = exact[layerSource[at]];
        } else {
            std::uint32_t faded = 0xffU;
            for (int k = 0; k < 3; k++) {
                const int component = static_cast<int>((colour >> (24 - (8 * k))) & 0xffU);
                faded |= static_cast<std::uint32_t>(std::min(255, (component * fadeOf(entry, k)) / 256))
                         << (24 - (8 * k));
            }
            colour = faded;
        }
        layerPicture[(static_cast<std::size_t>(row - top) * width) + (column - left)] = colour;
    }
    overlayPictureAs(layerPicture, width, height, static_cast<float>(left - layerLeft),
                     static_cast<float>(top - layerTop), static_cast<float>(width), static_cast<float>(height),
                     OverlayBlend::BLEND, true);
}

void openColours()
{
    if (openGump != nullptr) {
        return;
    }
    freezeWorld(1);
    openModalGump(new ColoursGump, static_cast<u32>(-1), [](s32 /*result*/) {
        thawWorld(1);
        writeEnhancedOptions();
    });
}

NewGump *coloursGump()
{
    return new ColoursGump;
}

bool drawColours()
{
    if (openGump == nullptr) {
        return false;
    }
    openGump->draw();
    drawMenuPointer();
    return true;
}

void silencerColours(std::uint32_t *out)
{
    exactColours(out, true);
}
