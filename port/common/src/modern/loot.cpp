/* What the dead drop. See loot.h. */
#include "loot.h"
#include "effects.h"
#include "pointer.h"

void noteLoot(u16 npc)
{
    if (npc == 1 || !pointerSetting()) {
        return;
    }
    effectsDropGun(npc);
}
