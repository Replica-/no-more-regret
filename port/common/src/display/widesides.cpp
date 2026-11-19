/* The wide screen's sides. See widesides.h. */
#include "widesides.h"
#include "gamestate.h"
#include "camera.h"
#include "colours.h"
#include "crusys.h"
#include "dlist.h"
#include "effects.h"
#include "gamepal.h"
#include "gray.h"
#include "gametime.h"
#include "host.h"
#include "item.h"
#include "itemcach.h"
#include "itemdata.h"
#include "itemutil.h"
#include "screen.h"
#include "shaphand.h"
#include "spmgr.h"
#include "status.h"
#include "type.h"
#include "viewport.h"
#include "world.h"

#include <vector>

extern SpriteEntry *&sprites;

namespace {

/* An item as the display list's node has it (DL_ItemNode::place): its map
 * point halved in x and y, its box, its point on the map's screen and its
 * frame's bounds there, before the scroll. */
struct SideItem {
    u16 item;
    /* The frame drawn: the item's, or the sides' own for a decoration. */
    u16 frame;
    i16 x, y, z;
    i16 width, depth, height;
    i16 screenX, screenY;
    Rect bounds;
};

/* The sides' pixels, the game's, and their width; whether they are covered
 * (coverWideSides) or greyed (greyWideSides). */
u8 *&pixels = gameGlobal<u8 *>(nullptr);
u16 &pixelsWidth = gameGlobal<u16>(0);
u16 &pixelsHeight = gameGlobal<u16>(0);
bool &covered = gameGlobal<bool>(false);
bool &greyed = gameGlobal<bool>(false);

/* The rows below the screen the status display's parts need to be moved
 * to the frame's bottom (screenShowFrame): their 40 and as many again. */
const int STATUS_MOVED_ROOM = 80;

/* A number of the sides' own from two (no random number of the game's). */
u32 mixed(u32 a, u32 b)
{
    u32 h = (a * 0x9e3779b1U) ^ (b + 0x7f4a7c15U);
    h ^= h >> 15;
    h *= 0x2c1b3c6dU;
    h ^= h >> 12;
    return h;
}

/* A frame of a group of size frames cycled on by steps from frame, as
 * animateItems' types 1 and 3 step it (animData 2 and up: the group of
 * frame; one past the frames, the group's first). */
u16 cycled(u16 frame, u16 frames, u16 group, u32 steps)
{
    if (group < 2) {
        return static_cast<u16>((frame + steps) % frames);
    }
    const u16 base = static_cast<u16>(frame - (frame % group));
    const auto within = static_cast<u16>((frame % group + steps) % group);
    const auto made = static_cast<u16>(base + within);
    return made < frames ? made : base;
}

/* The frame the sides draw a decoration of the sides alone at: animateItems'
 * steps of its type, one each of its shape's speed of the animation's time
 * (half of them where it steps at random one time in two), its random
 * frames of the sides' own numbers; the item's frame for the types that run
 * the game's code (5) and those that do not animate. */
u16 sidesFrame(u16 number, u16 frame, const ShapeInfo &info)
{
    u16 width;
    u16 height;
    u16 frames;
    shapeCache->shapeHeader(itemShape[number], &width, &height, &frames);
    if (frames < 2 || frame >= frames) {
        return frame;
    }
    const u32 speed = info.animSpeed < 2 ? 1 : info.animSpeed;
    const u32 tick = animationProcess->time / speed;
    const u32 steps = info.animData == 1 ? tick / 2 : tick;
    switch (info.animType) {
    case 1:
    case 3:
        return cycled(frame, frames, info.animData, steps);
    case 2:
        return static_cast<u16>(mixed(number, tick / 2) % frames);
    case 4: {
        /* At 0, a run through the frames one time in animData + 2. */
        const u32 period = frames + info.animData + 2U;
        const u32 phase = (tick + mixed(number, 0)) % period;
        return static_cast<u16>(phase < frames ? phase : 0);
    }
    case 6:
        /* Frame 0 is off; the others go round past it. */
        if (frame == 0) {
            return 0;
        }
        if (info.animData < 2) {
            return static_cast<u16>(1 + ((frame - 1 + steps) % (frames - 1)));
        }
        if (frame % info.animData == 0) {
            return frame;
        }
        {
            const u16 group = info.animData;
            const u16 base = static_cast<u16>(frame - (frame % group));
            const auto within = static_cast<u16>(1 + ((frame % group - 1 + steps) % (group - 1)));
            const auto made = static_cast<u16>(base + within);
            return made < frames ? made : base;
        }
    default:
        return frame;
    }
}

/* Whether two rectangles touch (Rect::touches on a copy). */
bool touching(Rect a, const Rect &b)
{
    return a.touches(b);
}

/* The frame's bounds on the map's screen at the item's point. */
Rect frameBounds(const SideItem &side, u16 shape, u16 frame)
{
    FrameRect bounds;
    shapeCache->frameBounds(shape, frame, &bounds, 0, 0);
    if ((itemFlags[side.item] & ITEM_FLIPPED) != 0) {
        return Rect(side.screenX - bounds.right, side.screenY + bounds.top, side.screenX - bounds.left,
                    side.screenY + bounds.bottom);
    }
    return Rect(side.screenX + bounds.left, side.screenY + bounds.top, side.screenX + bounds.right,
                side.screenY + bounds.bottom);
}

/* The item's point and box as DL_ItemNode::place puts them. */
void placeItem(u16 number, SideItem &side)
{
    const u16 shape = itemShape[number];
    side.item = number;
    side.frame = Item(number).frame();
    side.x = static_cast<i16>(itemX[number] >> 1);
    side.y = static_cast<i16>(itemY[number] >> 1);
    side.z = static_cast<i16>(itemZ[number]);
    side.screenX = static_cast<i16>(side.x - side.y);
    side.screenY = static_cast<i16>((side.x >> 1) + (side.y >> 1) - side.z);
    if ((itemFlags[number] & ITEM_FLIPPED) != 0) {
        halfBox(side.depth, side.width, side.height, &typeFlags.infos[shape]);
    } else {
        halfBox(side.width, side.depth, side.height, &typeFlags.infos[shape]);
    }
}

/* The box's bounds on the map's screen, as buildNodes takes them. */
Rect boxBounds(const SideItem &side)
{
    return Rect(static_cast<i16>(side.screenX - side.width),
                static_cast<i16>(side.screenY - (side.width >> 1) - (side.depth >> 1) - side.height),
                static_cast<i16>(side.screenX + side.depth), side.screenY);
}

/* The frame drawn and its bounds, false without a frame: the item's; a
 * decoration off the screen (whose frames the game steps only there) at
 * the sides' own. */
bool frameItem(SideItem &side, const Rect &screen)
{
    const u16 number = side.item;
    const u16 shape = itemShape[number];
    const u16 frame = side.frame;
    if (cachedShapeFrame(shape, frame) == nullptr) {
        return false;
    }
    side.bounds = frameBounds(side, shape, frame);
    const ShapeInfo &info = typeFlags.infos[shape];
    if (info.animType != 0U && animationsHeld == 0U && animationsStopped == 0U && !touching(side.bounds, screen)) {
        const u16 shown = sidesFrame(number, frame, info);
        if (shown != frame && cachedShapeFrame(shape, shown) != nullptr) {
            side.frame = shown;
            side.bounds = frameBounds(side, shape, shown);
        }
    }
    return true;
}

/* DL_ItemNode::order of two items whose bounds touch: 1 when a is drawn
 * over b, 2 when b is over a, 0 for neither. */
i16 orderItems(const SideItem &a, const SideItem &b)
{
    bool overlap = true;
    if (a.x - a.z - a.width - a.height >= b.x - b.z || a.x - a.z <= b.x - b.z - b.width - b.height) {
        overlap = false;
    } else if (a.y - a.z - a.depth - a.height >= b.y - b.z || a.y - a.z <= b.y - b.z - b.depth - b.height) {
        overlap = false;
    } else if (a.screenX - a.width >= b.x - b.y + b.depth || a.screenX + a.depth <= b.x - b.y - b.width) {
        overlap = false;
    }
    if (!overlap) {
        return 0;
    }
    if (a.x - a.width >= b.x) {
        return 1;
    }
    if (a.x <= b.x - b.width) {
        return 2;
    }
    if (a.y - a.depth >= b.y) {
        return 1;
    }
    if (a.y <= b.y - b.depth) {
        return 2;
    }
    if (a.z >= b.z + b.height && a.z + a.height > b.z) {
        return 1;
    }
    if (a.z + a.height <= b.z && a.z < b.z + b.height) {
        return 2;
    }
    const i16 sumA = static_cast<i16>(a.x - a.width + a.y - a.depth + a.z + a.z);
    const i16 sumB = static_cast<i16>(b.x - b.width + b.y - b.depth + b.z + b.z);
    if (sumA > sumB) {
        return 1;
    }
    if (sumA < sumB) {
        return 2;
    }
    return 0;
}

/* The fast area's items the camera would draw (buildNodes: not the
 * editor's shapes, the invisible ones only the controlled NPC's or with the
 * world inactive) whose frames lie in the sides of the map's screen. */
std::vector<SideItem> itemsIn(const std::vector<Rect> &areas, const Rect &screen)
{
    /* The boxes near the sides, a cell of the display list's grid around
     * them as the grid has around the screen, are those whose frames may
     * reach them. */
    std::vector<Rect> nearAreas;
    for (const Rect &area : areas) {
        nearAreas.emplace_back(static_cast<i16>(area.left - GRID_CELL), static_cast<i16>(area.top - GRID_CELL),
                               static_cast<i16>(area.right + GRID_CELL), static_cast<i16>(area.bottom + GRID_CELL));
    }
    const auto touchesAny = [](const Rect &box, const std::vector<Rect> &list) {
        for (const Rect &area : list) {
            if (touching(box, area)) {
                return true;
            }
        }
        return false;
    };
    std::vector<SideItem> found;
    for (i16 sum = fastArea.sumLow; sum <= fastArea.sumHigh; sum++) {
        for (i16 difference = fastArea.differenceLow; difference <= fastArea.differenceHigh; difference++) {
            if (((sum + difference) & 1) != 0) {
                continue;
            }
            const auto column = static_cast<i16>(static_cast<u16>(sum + difference) >> 1);
            const auto row = static_cast<i16>(static_cast<u16>(sum - difference) >> 1);
            if (!fastArea.contains(column, row)) {
                continue;
            }
            for (u16 number = mapChunks[column][row]; number != 0U; number = itemNext[number]) {
                const ShapeInfo &info = typeFlags.infos[itemShape[number]];
                if (info.editor) {
                    continue;
                }
                if ((itemFlags[number] & ITEM_INVISIBLE) != 0 && !Item(number).isSilencer() && worldActive != 0U) {
                    continue;
                }
                SideItem side{};
                placeItem(number, side);
                const Rect box = boxBounds(side);
                if (!touchesAny(box, nearAreas)) {
                    continue;
                }
                if (frameItem(side, screen) && touchesAny(side.bounds, areas)) {
                    found.push_back(side);
                }
            }
        }
    }
    return found;
}

/* The items in the order the display list draws them: each after those it
 * is over; where the order goes round, the first left in the fast area's. */
std::vector<std::size_t> drawingOrder(const std::vector<SideItem> &items)
{
    const std::size_t count = items.size();
    std::vector<std::vector<std::size_t>> over(count);
    std::vector<std::size_t> under(count, 0);
    for (std::size_t a = 0; a < count; a++) {
        for (std::size_t b = a + 1; b < count; b++) {
            if (!touching(items[a].bounds, items[b].bounds)) {
                continue;
            }
            const i16 order = orderItems(items[a], items[b]);
            if (order == 1) {
                over[b].push_back(a);
                under[a]++;
            } else if (order == 2) {
                over[a].push_back(b);
                under[b]++;
            }
        }
    }
    std::vector<std::size_t> drawn;
    std::vector<bool> done(count, false);
    while (drawn.size() < count) {
        std::size_t next = count;
        for (std::size_t k = 0; k < count && next == count; k++) {
            if (!done[k] && under[k] == 0) {
                next = k;
            }
        }
        for (std::size_t k = 0; k < count && next == count; k++) {
            if (!done[k]) {
                next = k;
            }
        }
        done[next] = true;
        drawn.push_back(next);
        for (const std::size_t above : over[next]) {
            if (under[above] > 0) {
                under[above]--;
            }
        }
    }
    return drawn;
}

/* The item's frame on view at its point less the scroll, moved by shift
 * across and down, as DL_ItemNode::draw draws it. */
void drawItem(Viewport &view, const SideItem &side, i16 shift, i16 down)
{
    const u16 shape = itemShape[side.item];
    const ShapeInfo &info = typeFlags.infos[shape];
    ShapeFrame *image = cachedShapeFrame(shape, side.frame);
    if (image == nullptr) {
        return;
    }
    const i16 x = static_cast<i16>(side.screenX - scrollX + shift);
    const i16 y = static_cast<i16>(side.screenY - scrollY + down);
    const u16 flags = itemFlags[side.item];
    /* The port's: a fire the hardware scene draws instead (effects.h). */
    const bool fire = effectsReplaces(shape);
    if (fire) {
        effectsItemDrawing(view, side.item, side.frame, true, true, x, y, static_cast<i16>(-shift),
                           static_cast<i16>(-down));
    }
    if ((flags & ITEM_FLIPPED) != 0) {
        if ((flags & ITEM_INVISIBLE) != 0) {
            view.drawShapeMirroredTranslucent(x, y, image, paletteTables->translucent.get());
        } else if (info.translucent) {
            view.drawShapeMirroredRemapped(x, y, image, paletteTables->remap);
        } else if (!Item(side.item).isSilencer() || !drawInColours(view, x, y, image, true)) {
            view.drawShapeMirrored(x, y, image);
        }
    } else if ((flags & ITEM_INVISIBLE) != 0) {
        view.drawShapeTranslucent(x, y, image, paletteTables->translucent.get());
    } else if (info.translucent) {
        view.drawShapeRemapped(x, y, image, paletteTables->remap);
    } else if (!Item(side.item).isSilencer() || !drawInColours(view, x, y, image, false)) {
        /* The port's: the Silencer in his colours (colours.h). */
        view.drawShape(x, y, image);
    }
    if (fire) {
        effectsItemDrawing(view, side.item, side.frame, false, true, x, y, static_cast<i16>(-shift),
                           static_cast<i16>(-down));
    }
}

/* The sprites on view, moved by shift across and down, as drawSprites
 * draws them (not those not drawn, spmgr.h's SPRITE_UNSEEN). */
void drawSidesSprites(Viewport &view, i16 shift, i16 down)
{
    for (u16 i = 0; i < spriteCount; i++) {
        const SpriteEntry &sprite = sprites[i];
        if (sprite.shape == 0U || (sprite.flags & SPRITE_UNSEEN) != 0) {
            continue;
        }
        ShapeFrame *image = cachedShapeFrame(sprite.shape, sprite.frame);
        if (image == nullptr) {
            continue;
        }
        i16 x;
        i16 y;
        mapToScreen(sprite.x, sprite.y, &x, &y);
        y = static_cast<i16>(y - sprite.z + down);
        if ((sprite.flags & SPRITE_REMAPPED) != 0) {
            view.drawShapeRemapped(static_cast<i16>(x + shift), y, image, paletteTables->remap);
        } else {
            view.drawShape(static_cast<i16>(x + shift), y, image);
        }
    }
}

}

const u8 *wideSidesPixels(int *width, int *height)
{
    *width = pixelsWidth;
    *height = pixelsHeight;
    return covered ? nullptr : pixels;
}

int wideStatusDown()
{
    const int height = screenWideHeight();
    if (statusShown == 0U || height - SCREEN_HEIGHT < STATUS_MOVED_ROOM || covered) {
        return 0;
    }
    return (height - SCREEN_HEIGHT) / 2;
}

void drawWideSides()
{
    /* Nothing while the host draws nothing (host.h). */
    if (!hostDraws()) {
        return;
    }
    const u16 width = screenWideWidth();
    const u16 height = screenWideHeight();
    if (width <= static_cast<u16>(SCREEN_WIDTH) && height <= static_cast<u16>(SCREEN_HEIGHT)) {
        return;
    }
    if (covered) {
        if (worldFrozen != 0U) {
            return;
        }
        covered = false;
    }
    if (greyed) {
        return;
    }
    if (width != pixelsWidth || height != pixelsHeight) {
        delete[] pixels;
        /* Its middle, under the 640x480 screen, is never drawn: zeros, so
         * that greyWideSides reads no undefined pixel there. */
        pixels = new u8[static_cast<std::size_t>(width) * height]();
        pixelsWidth = width;
        pixelsHeight = height;
    }
    const auto side = static_cast<i16>((width - SCREEN_WIDTH) / 2);
    const auto top = static_cast<i16>((height - SCREEN_HEIGHT) / 2);
    const auto last = static_cast<i16>(width - 1);
    const auto bottom = static_cast<i16>(height - 1);
    /* The parts of the frame round the screen on the pixels: the sides as
     * high as the frame, above and below the screen between them. */
    std::vector<Rect> parts;
    if (side > 0) {
        parts.emplace_back(0, 0, static_cast<i16>(side - 1), bottom);
        parts.emplace_back(static_cast<i16>(side + SCREEN_WIDTH), 0, last, bottom);
    }
    if (top > 0) {
        parts.emplace_back(side, 0, static_cast<i16>(side + SCREEN_WIDTH - 1), static_cast<i16>(top - 1));
        parts.emplace_back(side, static_cast<i16>(top + SCREEN_HEIGHT), static_cast<i16>(side + SCREEN_WIDTH - 1),
                           bottom);
    }
    /* With room enough below the screen, the status display's parts are
     * shown at the frame's bottom (screenShowFrame), the world drawn where
     * they were. */
    std::vector<ScreenBox> moved;
    if (wideStatusDown() != 0) {
        for (const StatusPart &part : statusParts) {
            moved.push_back({part.area.left, part.area.top, part.area.right, part.area.bottom});
            parts.emplace_back(static_cast<i16>(part.area.left + side), static_cast<i16>(part.area.top + top),
                               static_cast<i16>(part.area.right + side), static_cast<i16>(part.area.bottom + top));
        }
    }
    /* The parts on the map's screen, before the scroll. */
    std::vector<Rect> areas;
    for (const Rect &part : parts) {
        areas.emplace_back(static_cast<i16>(part.left - side + scrollX), static_cast<i16>(part.top - top + scrollY),
                           static_cast<i16>(part.right - side + scrollX),
                           static_cast<i16>(part.bottom - top + scrollY));
    }
    Viewport view(VIEWPORT_LARGE, 0, 0, last, bottom, pixels);
    const Rect screen(scrollX, scrollY, static_cast<i16>(scrollX + SCREEN_WIDTH - 1),
                      static_cast<i16>(scrollY + SCREEN_HEIGHT - 1));
    const std::vector<SideItem> items = itemsIn(areas, screen);
    const std::vector<std::size_t> order = drawingOrder(items);
    for (std::size_t k = 0; k < parts.size(); k++) {
        view.setBounds(parts[k]);
        view.clear(0, parts[k].left, parts[k].top, parts[k].right, parts[k].bottom);
        for (const std::size_t item : order) {
            if (touching(items[item].bounds, areas[k])) {
                drawItem(view, items[item], side, top);
            }
        }
        drawSidesSprites(view, side, top);
    }
    screenShowFrame(pixels, width, height, moved.data(), static_cast<int>(moved.size()));
}

void coverWideSides()
{
    covered = true;
    screenShowSides(nullptr, 0);
}

void greyWideSides(BasePalette *palette)
{
    const u16 width = screenWideWidth();
    const u16 height = screenWideHeight();
    if (covered || pixels == nullptr || width != pixelsWidth || height != pixelsHeight) {
        return;
    }
    Viewport view(VIEWPORT_LARGE, 0, 0, static_cast<i16>(width - 1), static_cast<i16>(height - 1), pixels);
    toFixedGray(&view, palette);
    greyed = true;
    screenShowFrame(pixels, width, height, nullptr, 0);
}

void ungreyWideSides()
{
    greyed = false;
}

void closeWideSides()
{
    delete[] pixels;
    pixels = nullptr;
    pixelsWidth = 0;
    pixelsHeight = 0;
}
