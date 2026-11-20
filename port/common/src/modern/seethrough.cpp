/* Seeing through. See seethrough.h. */
#include "seethrough.h"
#include "bindings.h"
#include "camera.h"
#include "control.h"
#include "crusys.h"
#include "dlist.h"
#include "dlitem.h"
#include "drawframe.h"
#include "gamepal.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "npc.h"
#include "screen.h"
#include "type.h"
#include "uprocess.h"
#include "viewport.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

/* Each pair of colours mixed half and half (the palette's nearest, but the
 * cycled and glowing entries, 0-15, and the transparent one), the
 * picture's first: worked out when first met, 0 in known until then; for
 * the map they were worked out on. */
u8 mixed[256 * 256];
u8 mixedKnown[256 * 256];
u16 mixedMap = 0xffff;

u8 mixOf(u8 over, u8 under)
{
    const int at = (over << 8) | under;
    if (mixedKnown[at] == 0U) {
        const RGB *colours = gamePalette->data.get();
        const int red = colours[over].red + colours[under].red;
        const int green = colours[over].green + colours[under].green;
        const int blue = colours[over].blue + colours[under].blue;
        int best = 16;
        int bestDistance = 1 << 30;
        for (int i = 16; i < TRANSPARENT; i++) {
            /* Twice each component: the sum of the two against it. */
            const int dr = (colours[i].red * 2) - red;
            const int dg = (colours[i].green * 2) - green;
            const int db = (colours[i].blue * 2) - blue;
            const int distance = (dr * dr) + (dg * dg) + (db * db);
            if (distance < bestDistance) {
                best = i;
                bestDistance = distance;
            }
        }
        mixed[at] = static_cast<u8>(best);
        mixedKnown[at] = 1;
    }
    return mixed[at];
}

/* The highest top to stand on (ShapeInfo::z, 8 map units each) that is
 * floor rather than a thing on it. */
const u16 LOW_LAND = 2;

/* Whether the player sees through, as noteSeeThrough last took it. */
bool seeing = false;

bool isBody(u16 item)
{
    Item it(item);
    if (!it.isValid() || item == controlledActor) {
        return false;
    }
    if (it.isNpc()) {
        return Npc(item).isDead() != 0U;
    }
    return it.family() == FAMILY_CONTAINER;
}

/* A corpse: an NPC dead or dying, or a dead one's container (a D_ usecode
 * class's). */
bool isCorpse(u16 item)
{
    Item it(item);
    if (!it.isValid() || item == controlledActor) {
        return false;
    }
    if (it.isNpc()) {
        return Npc(item).isDead() != 0U;
    }
    if (it.family() != FAMILY_CONTAINER || classes == nullptr || classes->unitFile == nullptr) {
        return false;
    }
    return std::strncmp(classes->info(classOf(&it)).name, "D_", 2) == 0;
}

}

bool corpseAt(u16 *item, i16 x, i16 y)
{
    if (worldActive == 0U || x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return false;
    }
    const u16 node = wantedNodeAt(x, y, isCorpse);
    if (node == 0U) {
        return false;
    }
    *item = static_cast<DL_ItemNode *>(nodeOf(node))->item;
    return true;
}

bool seeThroughBodyAt(u16 *item, i16 x, i16 y)
{
    if (!seeing || worldActive == 0U || x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return false;
    }
    const u16 node = wantedNodeAt(x, y, isBody);
    if (node == 0U) {
        return false;
    }
    *item = static_cast<DL_ItemNode *>(nodeOf(node))->item;
    return true;
}

void noteSeeThrough()
{
    const bool now = worldActive != 0U && actionHeld(Action::SEE_THROUGH) != 0U;
    if (now == seeing) {
        return;
    }
    seeing = now;
    if (gameCamera != nullptr) {
        gameCamera->invalidate(screenRect, 0);
    }
}

bool seeThrough(u16 item)
{
    if (!seeing || worldActive == 0U) {
        return false;
    }
    Item it(item);
    if (!it.isValid() || it.isNpc()) {
        return false;
    }
    const ShapeInfo &info = typeFlags.infos[itemShape[item]];
    /* No height: the floors and what lies flat on them; and a low top to
     * stand on (the land flag): the raised floors, steps and platforms,
     * not the crates' and barrels' higher ones. */
    if (info.npc != 0U || info.translucent != 0U || info.z == 0U || (info.land != 0U && info.z <= LOW_LAND)) {
        return false;
    }
    switch (info.family) {
    case FAMILY_QUANTITY:
    case FAMILY_CONTAINER:
    case FAMILY_REAGENT:
    case FAMILY_WEAPON:
    case FAMILY_AMMO:
    case FAMILY_BOMB:
    case FAMILY_INVENTORY:
        return false;
    default:
        return true;
    }
}

void drawSeeThrough(Viewport &view, i16 x, i16 y, ShapeFrame *frame, bool mirrored)
{
    if (mixedMap != currentMap) {
        std::fill(std::begin(mixedKnown), std::end(mixedKnown), static_cast<u8>(0));
        mixedMap = currentMap;
    }
    const auto put = [](u8 &out, u8 in) { out = mixOf(in, out); };
    const auto *bytes = reinterpret_cast<const u8 *>(frame);
    if (mirrored) {
        drawframe::draw<true>(view.pixels(), x, y, bytes, put);
    } else {
        drawframe::draw<false>(view.pixels(), x, y, bytes, put);
    }
}
