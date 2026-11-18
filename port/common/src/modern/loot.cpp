/* What the dead drop. See loot.h. */
#include "loot.h"
#include "gamestate.h"
#include "gravity.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "pointer.h"
#include "systimer.h"
#include "type.h"
#include "uprocess.h"

#include <cstdlib>
#include <cstring>

namespace {

/* The deaths waited for, the arena's: where each NPC died and when. */
const int WAITING = 16;
struct Death {
    u16 npc;
    u16 x;
    u16 y;
    u8 z;
    u32 at;
};
struct Deaths {
    Death list[WAITING];
};
Deaths &deaths = gameGlobal<Deaths>();

/* How long a body is waited for; how near the death it lies; the most
 * things thrown out of one, and the ways they go, round the compass in
 * turn. */
const u32 WAIT = 600;
const int NEAR = 64;
const int MOST = 16;
const i16 AWAY[8][2] = {{6, 0}, {4, 4}, {0, 6}, {-4, 4}, {-6, 0}, {-4, -4}, {0, -6}, {4, -4}};

/* A body: a container of a D_ class (D_GUARD, D_SOLDIER...). */
bool isBody(u16 item)
{
    if (typeFlags.infos[itemShape[item]].family != FAMILY_CONTAINER || classes == nullptr ||
        classes->unitFile == nullptr) {
        return false;
    }
    Item it(item);
    return std::strncmp(classes->info(classOf(&it)).name, "D_", 2) == 0;
}

/* The body near the death with something in it; 0 for none yet. */
u16 bodyNear(const Death &death)
{
    const int column = death.x >> CHUNK_SHIFT;
    const int row = death.y >> CHUNK_SHIFT;
    for (int c = std::max(0, column - 1); c <= std::min(CHUNKS - 1, column + 1); c++) {
        for (int r = std::max(0, row - 1); r <= std::min(CHUNKS - 1, row + 1); r++) {
            for (u16 item = mapChunks[c][r]; item != 0U; item = itemNext[item]) {
                if (item < FIRST_ITEM || (itemFlags[item] & (ITEM_CONTAINED | ITEM_ETHEREAL)) != 0 ||
                    std::abs(itemX[item] - death.x) > NEAR || std::abs(itemY[item] - death.y) > NEAR ||
                    std::abs(itemZ[item] - death.z) > 16 || Item(item).contents() == 0U || !isBody(item)) {
                    continue;
                }
                return item;
            }
        }
    }
    return 0;
}

void throwOut(u16 body)
{
    u16 loot[MOST];
    int count = 0;
    for (u16 thing = Item(body).contents(); thing != 0U && count < MOST; thing = itemNext[thing]) {
        loot[count++] = thing;
    }
    for (int i = 0; i < count; i++) {
        Item thing(loot[i]);
        thing.push();
        thing.popToCoords(itemX[body], itemY[body], static_cast<u8>(itemZ[body] + 10));
        hurl(&thing, AWAY[i % 8][0], AWAY[i % 8][1], 10, 2);
    }
}

}

void noteLoot(u16 npc)
{
    if (npc == 1 || !pointerSetting()) {
        return;
    }
    for (Death &death : deaths.list) {
        if (death.npc == 0U) {
            death = Death{npc, itemX[npc], itemY[npc], itemZ[npc], systemTimer->time()};
            return;
        }
    }
}

void lootTick()
{
    const u32 now = systemTimer->time();
    for (Death &death : deaths.list) {
        if (death.npc == 0U) {
            continue;
        }
        const u16 body = bodyNear(death);
        if (body != 0U) {
            throwOut(body);
            death.npc = 0;
        } else if (now - death.at > WAIT) {
            death.npc = 0;
        }
    }
}
