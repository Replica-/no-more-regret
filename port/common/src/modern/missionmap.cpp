/* The port's mission map. See missionmap.h. */
#include "missionmap.h"
#include "gamestate.h"
#include "bindings.h"
#include "camera.h"
#include "control.h"
#include "crusys.h"
#include "dispatch.h"
#include "hud.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "itemutil.h"
#include "keyboard.h"
#include "newgump.h"
#include "npc.h"
#include "overlay.h"
#include "type.h"
#include "uprocess.h"
#include "world.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace {

/* The panel the map is drawn in, in the screen's pixels. */
constexpr float PANEL_LEFT = 16;
constexpr float PANEL_TOP = 16;
constexpr float PANEL_RIGHT = 624;
constexpr float PANEL_BOTTOM = 436;
constexpr float MOST_ZOOM = 2.0F;
constexpr float LEAST_ZOOM = 0.06F;

const std::uint32_t PANEL = 0x05090fe8;
const std::uint32_t FRAME = 0x7896b4eb;
const std::uint32_t FLOOR_COLOUR = 0x344e6296;
const std::uint32_t DOOR_COLOUR = 0xffaa28ff;
const std::uint32_t FORCE_FIELD_COLOUR = 0xc85fffff;
const std::uint32_t RIM = 0x000000d2;
const std::uint32_t YOU = 0x5aff82ff;
const std::uint32_t ENEMY = 0xff4637ff;
const std::uint32_t KEY_ITEM = 0x46e1ffff;
const std::uint32_t WEAPON = 0xffaf37ff;
const std::uint32_t USE = 0x6e96ffff;

/* What the screen has shown of the map, in cells of SEEN_CELL units, and
 * the map it is of: the drawing's own, outside the game's state. */
constexpr int SEEN_CELL = 128;
constexpr int SEEN_CELLS = (CHUNKS * CHUNK_SIZE) / SEEN_CELL;
u8 seen[SEEN_CELLS][SEEN_CELLS];
u16 seenMap = 0xffff;

bool seenAt(u16 x, u16 y)
{
    return seen[y / SEEN_CELL][x / SEEN_CELL] != 0U;
}

/* A map point from above: the screen's projection without the height
 * (itemutil.h's mapToScreen without the scroll). */
void project(std::int32_t x, std::int32_t y, float *px, float *py)
{
    *px = static_cast<float>(x - y) / 2;
    *py = static_cast<float>(x + y) / 4;
}

class MapGump : public NewGump {
public:
    MapGump();
    void handleLeftButton(NewGumpEvent *event) override;
    void handleRightButton(NewGumpEvent * /*event*/) override { close(); }
    void handleMouseMotion(NewGumpEvent *event) override;
    void handleKeyboard(NewGumpEvent *event) override;
    void paintSelf(i16 /*x*/, i16 /*y*/) override {}

    void centreOnSilencer();
    void zoomBy(float factor);

    /* The point from above at the panel's middle, and the screen's pixels
     * a pixel of it. */
    float middleX = 0;
    float middleY = 0;
    float zoom = 0.5F;
    bool dragging = false;
    i16 lastX = 0;
    i16 lastY = 0;
};

MapGump *&openGump = gameGlobal<MapGump *>(nullptr);

MapGump::MapGump() : NewGump(NewGumpId(), nullptr, 0)
{
    eventMask |= EVENT_LEFT_BUTTON | EVENT_RIGHT_BUTTON | EVENT_MOTION | EVENT_KEY;
    bounds.set(0, 0, static_cast<s16>(drawViewport->right() - drawViewport->left()),
               static_cast<s16>(drawViewport->bottom() - drawViewport->top()));
    addAcceptedArguments(-1, 0);
    centreOnSilencer();
}

void MapGump::centreOnSilencer()
{
    project(itemX[1], itemY[1], &middleX, &middleY);
}

void MapGump::zoomBy(float factor)
{
    zoom = std::clamp(zoom * factor, LEAST_ZOOM, MOST_ZOOM);
}

void MapGump::handleLeftButton(NewGumpEvent *event)
{
    /* The button's press starts a drag, its release ends it. */
    dragging = (event->input & 0xffff) != 3;
    lastX = eventX(event);
    lastY = eventY(event);
}

void MapGump::handleMouseMotion(NewGumpEvent *event)
{
    const i16 x = eventX(event);
    const i16 y = eventY(event);
    if (dragging) {
        middleX -= static_cast<float>(x - lastX) / zoom;
        middleY -= static_cast<float>(y - lastY) / zoom;
    }
    lastX = x;
    lastY = y;
}

void MapGump::handleKeyboard(NewGumpEvent *event)
{
    /* The key as the world's gump would take it (dispatch.cpp). */
    i16 code = event->argument;
    const Action action = worldCode(event->argument, &code) ? worldAction(code) : Action::NONE;
    const float step = 60 / zoom;
    if (action == Action::MISSION_MAP) {
        close();
        return;
    }
    if (action == Action::PREVIOUS_WEAPON) {
        zoomBy(1.18F);
        return;
    }
    if (action == Action::NEXT_WEAPON) {
        zoomBy(1 / 1.18F);
        return;
    }
    switch (event->argument) {
    case ASCII_ESCAPE:
        close();
        break;
    case '+':
    case '=':
        zoomBy(1.18F);
        break;
    case '-':
    case '_':
        zoomBy(1 / 1.18F);
        break;
    case 'c':
    case 'C':
    case '5':
        centreOnSilencer();
        break;
    case KEYCODE_SCAN | SCAN_LEFT:
    case '4':
        middleX -= step;
        break;
    case KEYCODE_SCAN | SCAN_RIGHT:
    case '6':
        middleX += step;
        break;
    case KEYCODE_SCAN | SCAN_UP:
    case '8':
        middleY -= step;
        break;
    case KEYCODE_SCAN | SCAN_DOWN:
    case '2':
        middleY += step;
        break;
    default:
        break;
    }
}

/* What a shape is on the map: a door (its usecode class is one) or a force
 * field, which close a way, a floor, a wall or nothing. */
enum class Part : u8 { NONE, FLOOR, WALL, DOOR, FORCE_FIELD };

Part partOf(Item &item, const ShapeInfo &info)
{
    if (classes != nullptr && classes->unitFile != nullptr) {
        char name[10] = {};
        std::memcpy(name, classes->info(classOf(&item)).name, 9);
        if (std::strstr(name, "EWALL") != nullptr || std::strstr(name, "FFIELD") != nullptr ||
            std::strstr(name, "FORCEF") != nullptr) {
            return Part::FORCE_FIELD;
        }
        if (std::strstr(name, "DOOR") != nullptr || std::strstr(name, "GATE") != nullptr ||
            std::strstr(name, "HATCH") != nullptr || std::strstr(name, "SHUTTER") != nullptr) {
            return Part::DOOR;
        }
    }
    if (info.land != 0U) {
        return Part::FLOOR;
    }
    if ((info.solid != 0U || info.occludes != 0U) && info.z >= 2) {
        return Part::WALL;
    }
    return Part::NONE;
}

/* The view: the panel's place of a point from above. */
struct View {
    float originX;
    float originY;
    float zoom;
    void place(std::int32_t x, std::int32_t y, float *sx, float *sy) const
    {
        float px;
        float py;
        project(x, y, &px, &py);
        *sx = originX + (px * zoom);
        *sy = originY + (py * zoom);
    }
};

bool inPanel(float x, float y)
{
    return x >= PANEL_LEFT && x <= PANEL_RIGHT && y >= PANEL_TOP && y <= PANEL_BOTTOM;
}

/* An item's footprint, grown by grow units each way, when all of it is in
 * the panel. */
void footprint(const View &view, u16 item, const ShapeInfo &info, std::int32_t grow, std::uint32_t colour)
{
    const bool flipped = (itemFlags[item] & ITEM_FLIPPED) != 0;
    i16 boxWidth;
    i16 boxDepth;
    i16 boxHeight;
    info.box(boxWidth, boxDepth, boxHeight);
    const std::int32_t width = flipped ? boxDepth : boxWidth;
    const std::int32_t depth = flipped ? boxWidth : boxDepth;
    const std::int32_t x = itemX[item] + grow;
    const std::int32_t y = itemY[item] + grow;
    const std::int32_t left = itemX[item] - width - grow;
    const std::int32_t far = itemY[item] - depth - grow;
    float sx[4];
    float sy[4];
    view.place(left, far, &sx[0], &sy[0]);
    view.place(x, far, &sx[1], &sy[1]);
    view.place(x, y, &sx[2], &sy[2]);
    view.place(left, y, &sx[3], &sy[3]);
    for (int i = 0; i < 4; i++) {
        if (!inPanel(sx[i], sy[i])) {
            return;
        }
    }
    overlayTriangle(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], colour);
    overlayTriangle(sx[0], sy[0], sx[2], sy[2], sx[3], sy[3], colour);
}

/* Each item on the map that the screen has shown, but the NPCs. */
template <typename Each> void eachSeenItem(Each each)
{
    for (int column = 0; column < CHUNKS; column++) {
        for (int row = 0; row < CHUNKS; row++) {
            for (u16 item = mapChunks[column][row]; item != 0U; item = itemNext[item]) {
                if (item < FIRST_ITEM || itemShape[item] == 0U ||
                    (itemFlags[item] & (ITEM_CONTAINED | ITEM_ETHEREAL)) != 0 || !seenAt(itemX[item], itemY[item])) {
                    continue;
                }
                each(item);
            }
        }
    }
}

void marker(const View &view, u16 x, u16 y, float radius, std::uint32_t colour)
{
    float sx;
    float sy;
    view.place(x, y, &sx, &sy);
    if (!inPanel(sx, sy)) {
        return;
    }
    overlayDisc(sx, sy, radius + 1.5F, RIM);
    overlayDisc(sx, sy, radius, colour);
}

/* The legend down the left and the hints along the bottom. */
void legend()
{
    struct Entry {
        std::uint32_t colour;
        const char *name;
        bool square;
    };
    const Entry entries[] = {{YOU, "YOU", false},        {ENEMY, "ENEMY", false},
                             {KEY_ITEM, "ITEM", false},  {WEAPON, "WEAPON, AMMUNITION", false},
                             {USE, "TO USE", false},     {DOOR_COLOUR, "DOOR", true},
                             {FORCE_FIELD_COLOUR, "FORCE FIELD", true}};
    overlayBox(PANEL_LEFT + 6, PANEL_TOP + 6, 170, 20 + (16 * static_cast<float>(std::size(entries))), 0x0000008c);
    wordsBegin();
    float y = PANEL_TOP + 20;
    for (const Entry &entry : entries) {
        if (entry.square) {
            overlayBox(PANEL_LEFT + 14, y - 4, 10, 10, RIM);
            overlayBox(PANEL_LEFT + 15, y - 3, 8, 8, entry.colour);
        } else {
            overlayDisc(PANEL_LEFT + 19, y + 1, 5, RIM);
            overlayDisc(PANEL_LEFT + 19, y + 1, 3.8F, entry.colour);
        }
        wordsText(7, static_cast<i16>(PANEL_LEFT + 32), static_cast<i16>(y - 5), entry.name, 1);
        y += 16;
    }
    wordsText(7, 320, static_cast<i16>(PANEL_BOTTOM - 16), "DRAG OR ARROWS MOVE   WHEEL ZOOMS   C CENTRES   M CLOSES", 0);
    wordsEnd();
}

}

void openMissionMap()
{
    if (openGump != nullptr) {
        return;
    }
    freezeWorld(1);
    openGump = new MapGump;
    openModalGump(openGump, static_cast<u32>(-1), [](s32 /*result*/) {
        openGump = nullptr;
        thawWorld(1);
    });
}

void noteSeen()
{
    if (currentMap != seenMap) {
        std::memset(seen, 0, sizeof seen);
        seenMap = currentMap;
    }
    if (currentMap == 0xffff || controlledActor == 0U) {
        return;
    }
    /* The cells round the Silencer whose middles the screen shows, at his
     * height. */
    const u16 actor = controlledActor;
    const int middleColumn = itemX[actor] / SEEN_CELL;
    const int middleRow = itemY[actor] / SEEN_CELL;
    for (int row = std::max(0, middleRow - 14); row <= std::min(SEEN_CELLS - 1, middleRow + 14); row++) {
        for (int column = std::max(0, middleColumn - 14); column <= std::min(SEEN_CELLS - 1, middleColumn + 14);
             column++) {
            i16 sx;
            i16 sy;
            mapToScreen(static_cast<u16>((column * SEEN_CELL) + (SEEN_CELL / 2)),
                        static_cast<u16>((row * SEEN_CELL) + (SEEN_CELL / 2)), &sx, &sy);
            sy = static_cast<i16>(sy - itemZ[actor]);
            if (sx > -48 && sx < 688 && sy > -48 && sy < 528) {
                seen[row][column] = 1;
            }
        }
    }
}

bool drawMissionMap()
{
    if (openGump == nullptr) {
        return false;
    }
    const MapGump &map = *openGump;
    overlayBox(-200, 0, 1040, 480, 0x00000078);
    overlayBox(PANEL_LEFT, PANEL_TOP, PANEL_RIGHT - PANEL_LEFT, PANEL_BOTTOM - PANEL_TOP, PANEL);
    const View view{((PANEL_LEFT + PANEL_RIGHT) / 2) - (map.middleX * map.zoom),
                    ((PANEL_TOP + PANEL_BOTTOM) / 2) - (map.middleY * map.zoom), map.zoom};
    /* The floors, then the walls, then the doors over them. */
    for (const Part pass : {Part::FLOOR, Part::WALL, Part::DOOR}) {
        eachSeenItem([&](u16 id) {
            if ((itemFlags[id] & ITEM_INVISIBLE) != 0) {
                return;
            }
            const ShapeInfo info = typeFlags.info(static_cast<i16>(itemShape[id]));
            if (info.roof != 0U || info.editor != 0U) {
                return;
            }
            Item item(id);
            const Part part = partOf(item, info);
            if (pass == Part::DOOR && (part == Part::DOOR || part == Part::FORCE_FIELD)) {
                footprint(view, id, info, 6, part == Part::DOOR ? DOOR_COLOUR : FORCE_FIELD_COLOUR);
            } else if (part == pass && pass == Part::FLOOR) {
                footprint(view, id, info, 0, FLOOR_COLOUR);
            } else if (part == pass && pass == Part::WALL) {
                const auto shade = static_cast<std::uint32_t>(std::min(235, 130 + (info.z * 6)));
                footprint(view, id, info, 0,
                          (shade << 24) | (shade << 16) | (std::min(255U, shade + 12) << 8) | 0xebU);
            }
        });
    }
    /* What matters on it: the things, the NPCs alive, the Silencer last. */
    eachSeenItem([&](u16 id) {
        if ((itemFlags[id] & ITEM_INVISIBLE) != 0) {
            return;
        }
        const ShapeInfo info = typeFlags.info(static_cast<i16>(itemShape[id]));
        if (info.editor != 0U) {
            return;
        }
        if (info.family == FAMILY_WEAPON || info.family == FAMILY_AMMO) {
            marker(view, itemX[id], itemY[id], 3, WEAPON);
        } else if (info.family == FAMILY_BOMB || info.family == FAMILY_INVENTORY) {
            marker(view, itemX[id], itemY[id], 4, KEY_ITEM);
        } else if (info.selectable != 0U) {
            marker(view, itemX[id], itemY[id], 3.5F, USE);
        }
    });
    for (u16 id = 2; id < FIRST_ITEM; id++) {
        if (itemShape[id] == 0U || itemZ[id] == FREE_Z || (itemFlags[id] & ITEM_CONTAINED) != 0 ||
            !seenAt(itemX[id], itemY[id])) {
            continue;
        }
        Npc npc(id);
        if (npc.isDead() == 0U) {
            marker(view, itemX[id], itemY[id], 4, ENEMY);
        }
    }
    marker(view, itemX[1], itemY[1], 6, YOU);
    overlayLine(PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_TOP, 1, FRAME);
    overlayLine(PANEL_LEFT, PANEL_BOTTOM, PANEL_RIGHT, PANEL_BOTTOM, 1, FRAME);
    overlayLine(PANEL_LEFT, PANEL_TOP, PANEL_LEFT, PANEL_BOTTOM, 1, FRAME);
    overlayLine(PANEL_RIGHT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM, 1, FRAME);
    legend();
    return true;
}
