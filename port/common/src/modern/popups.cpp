/* The pickup display over the places the items came from. See popups.h. */
#include "popups.h"
#include "gamestate.h"
#include "control.h"
#include "crusys.h"
#include "display.h"
#include "hud.h"
#include "item.h"
#include "itemdata.h"
#include "npc.h"
#include "overlay.h"
#include "type.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace {

/* Where the item taken now came from, and where each line's did, the
 * arena's; known 0 for nowhere on the map. */
struct Place {
    u16 x;
    u16 y;
    i16 z;
    u8 known;
};
struct Sources {
    Place taking;
    Place lines[PICKUPS];
};
Sources &sources = gameGlobal<Sources>();

/* A line: its height, the room before its name, the gap between lines,
 * and how high above the place the first sits; its picture, count and
 * name where display.cpp's drawPickups puts them. */
const i16 LINE = 28;
const i16 PICTURE = 66;
const i16 GAP = 3;
const i16 ABOVE = 18;
const i16 NAME_FONT = 13;
const i16 COUNT_FONT = 12;
const std::uint32_t BACK = 0x101418b0;
const std::uint32_t EDGE = 0x60708090;

}

void notePickupSource(u16 item)
{
    sources.taking.known = 0;
    Item top(item);
    if (!top.isValid()) {
        return;
    }
    for (u16 outer = top.container(); outer != 0U; outer = top.container()) {
        top = Item(outer);
    }
    /* Taken from the Silencer's own pack (a usecode's gift): nowhere. */
    if (top.id == controlledNpc.id || (itemFlags[top.id] & ITEM_CONTAINED) != 0) {
        return;
    }
    const ShapeInfo &info = typeFlags.infos[top.shape()];
    sources.taking = Place{top.centreX(), top.centreY(), static_cast<i16>(top.z() + (info.z * 8)), 1};
}

void notePickupLine(i16 line)
{
    if (line >= 0 && line < PICKUPS) {
        sources.lines[line] = sources.taking;
    }
}

void drawPopups()
{
    if (worldActive == 0U ||
        std::none_of(std::begin(pickups), std::end(pickups), [](const Pickup &p) { return p.name != nullptr; })) {
        return;
    }
    /* Each line's top left corner, those from one place (within a line's
     * height of it) stacked upwards; the others down the top left. */
    i16 width[PICKUPS];
    i16 corner = 0;
    wordsBegin();
    for (i16 i = 0; i < PICKUPS; i++) {
        const Pickup &pickup = pickups[i];
        if (pickup.name == nullptr) {
            width[i] = 0;
            continue;
        }
        width[i] = static_cast<i16>(PICTURE + wordsWidth(NAME_FONT, pickup.name) + 10);
        float x = 4;
        float y = static_cast<float>(4 + (corner * (LINE + GAP)));
        const Place &from = sources.lines[i];
        if (from.known != 0U) {
            mapToOverlay(from.x, from.y, from.z, &x, &y);
            x -= static_cast<float>(width[i]) / 2;
            y -= ABOVE + LINE;
            for (i16 j = 0; j < i; j++) {
                const Place &other = sources.lines[j];
                if (width[j] != 0 && other.known != 0U && std::abs(other.x - from.x) < 24 &&
                    std::abs(other.y - from.y) < 24) {
                    y -= LINE + GAP;
                }
            }
            x = std::clamp(x, 4.0F, static_cast<float>(636 - width[i]));
            y = std::max(y, 4.0F);
        } else {
            corner++;
        }
        overlayBox(x, y, width[i], LINE, BACK);
        overlayLine(x, y + LINE, x + width[i], y + LINE, 1, EDGE);
        wordsGump(pickup.shape, pickup.frame, static_cast<i16>(x + 33), static_cast<i16>(y + 14));
        if (pickup.count > 1) {
            char count[8];
            std::snprintf(count, sizeof(count), "%d", pickup.count);
            wordsText(COUNT_FONT, static_cast<i16>(x + 53), static_cast<i16>(y + 20), count, 1);
        }
        wordsText(NAME_FONT, static_cast<i16>(x + PICTURE), static_cast<i16>(y + 10), pickup.name, 1);
    }
    wordsEnd();
}
