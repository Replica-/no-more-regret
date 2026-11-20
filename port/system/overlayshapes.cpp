/* The overlay (platform/overlay.h, overlayshapes.h): the game's shapes gathered at
 * a frame, and drawn pixel by pixel over a script's screen. */
#include "overlayshapes.h"
#include "gamememory.h"
#include "overlay.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
void (*drawAction)() = nullptr;
/* The overlay being gathered, while the game's function runs. */
Overlay *gathering = nullptr;
/* The kept pictures (overlayPictureKept): each key's version, pixels and
 * the gathering it was last drawn in; forgotten after FORGET gatherings
 * without it. The game thread's. */
struct KeptPicture {
    std::uint32_t version;
    std::shared_ptr<const std::vector<std::uint32_t>> pixels;
    std::uint64_t used;
};
std::unordered_map<std::uint32_t, KeptPicture> keptPictures;
std::uint64_t gatherings = 0;
const std::uint64_t FORGET = 120;

void add(const OverlayShape &shape)
{
    if (gathering != nullptr) {
        gathering->shapes.push_back(shape);
    }
}
}

void overlayAction(void (*draw)())
{
    const SystemSide systemSide;
    drawAction = draw;
}

void overlayLine(float x0, float y0, float x1, float y1, float width, std::uint32_t colour)
{
    const SystemSide systemSide;
    add({.kind = OverlayShape::LINE, .x = {x0, x1, 0}, .y = {y0, y1, 0}, .width = width, .colour = colour});
}

void overlayBox(float x, float y, float w, float h, std::uint32_t colour)
{
    const SystemSide systemSide;
    add({.kind = OverlayShape::BOX, .x = {x, w, 0}, .y = {y, h, 0}, .colour = colour});
}

void overlayRing(float x, float y, float radius, float width, std::uint32_t colour)
{
    const SystemSide systemSide;
    add({.kind = OverlayShape::RING, .x = {x, 0, 0}, .y = {y, 0, 0}, .width = width, .radius = radius, .colour = colour});
}

void overlayDisc(float x, float y, float radius, std::uint32_t colour)
{
    const SystemSide systemSide;
    add({.kind = OverlayShape::DISC, .x = {x, 0, 0}, .y = {y, 0, 0}, .radius = radius, .colour = colour});
}

void overlayTriangle(float x0, float y0, float x1, float y1, float x2, float y2, std::uint32_t colour)
{
    const SystemSide systemSide;
    add({.kind = OverlayShape::TRIANGLE, .x = {x0, x1, x2}, .y = {y0, y1, y2}, .colour = colour});
}

void overlayPicture(const std::uint32_t *pixels, int width, int height, float x, float y, float w, float h)
{
    overlayPictureAs(pixels, width, height, x, y, w, h, OverlayBlend::BLEND, false);
}

void overlayPictureAs(const std::uint32_t *pixels, int width, int height, float x, float y, float w, float h,
                      OverlayBlend blend, bool sharp)
{
    const SystemSide systemSide;
    if (gathering == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const std::size_t first = gathering->pixels.size();
    gathering->pixels.insert(gathering->pixels.end(), pixels,
                             pixels + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height)));
    add({.kind = OverlayShape::PICTURE,
         .x = {x, w, 0},
         .y = {y, h, 0},
         .colour = 0xffffffff,
         .first = first,
         .w = width,
         .h = height,
         .blend = static_cast<u8>(blend),
         .sharp = sharp});
}

void overlayPictureKept(std::uint32_t key, std::uint32_t version, const std::uint32_t *pixels, int width, int height,
                        float x, float y, float w, float h, OverlayBlend blend, bool sharp)
{
    const SystemSide systemSide;
    if (gathering == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    auto found = keptPictures.find(key);
    if (found == keptPictures.end() || found->second.version != version || found->second.pixels->size() != count) {
        if (pixels == nullptr) {
            return;
        }
        auto copy = std::make_shared<const std::vector<std::uint32_t>>(pixels, pixels + count);
        found = keptPictures.insert_or_assign(key, KeptPicture{version, std::move(copy), gatherings}).first;
    }
    found->second.used = gatherings;
    add({.kind = OverlayShape::PICTURE,
         .x = {x, w, 0},
         .y = {y, h, 0},
         .colour = 0xffffffff,
         .w = width,
         .h = height,
         .blend = static_cast<u8>(blend),
         .sharp = sharp,
         .kept = found->second.pixels,
         .key = key,
         .version = version});
}

void gatherOverlay(Overlay &overlay)
{
    overlay.clear();
    if (drawAction == nullptr) {
        return;
    }
    gatherings++;
    if ((gatherings % FORGET) == 0) {
        std::erase_if(keptPictures, [](const auto &entry) { return gatherings - entry.second.used > FORGET; });
    }
    gathering = &overlay;
    {
        const GameSide gameSide;
        drawAction();
    }
    gathering = nullptr;
}

namespace {
/* A pixel of colour (0xRRGGBBAA) over rgb, coverage of it. */
void blend(u8 *rgb, std::uint32_t colour, float coverage)
{
    const float alpha = static_cast<float>(colour & 0xff) / 255.0F * std::clamp(coverage, 0.0F, 1.0F);
    if (alpha <= 0) {
        return;
    }
    const float channel[3] = {static_cast<float>(colour >> 24 & 0xff), static_cast<float>(colour >> 16 & 0xff),
                              static_cast<float>(colour >> 8 & 0xff)};
    for (int i = 0; i < 3; i++) {
        rgb[i] = static_cast<u8>(std::lround((rgb[i] * (1 - alpha)) + (channel[i] * alpha)));
    }
}

/* The distance from p to the segment a-b. */
float segmentDistance(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length = (dx * dx) + (dy * dy);
    float t = length > 0 ? (((px - ax) * dx) + ((py - ay) * dy)) / length : 0;
    t = std::clamp(t, 0.0F, 1.0F);
    return std::hypot(px - (ax + (t * dx)), py - (ay + (t * dy)));
}

float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return ((bx - ax) * (py - ay)) - ((by - ay) * (px - ax));
}
}

void paintOverlay(const Overlay &overlay, u8 *rgb, int width, int height, int left, int above)
{
    for (const OverlayShape &shape : overlay.shapes) {
        float x0;
        float y0;
        float x1;
        float y1;
        switch (shape.kind) {
        case OverlayShape::LINE:
            x0 = std::min(shape.x[0], shape.x[1]) - shape.width;
            x1 = std::max(shape.x[0], shape.x[1]) + shape.width;
            y0 = std::min(shape.y[0], shape.y[1]) - shape.width;
            y1 = std::max(shape.y[0], shape.y[1]) + shape.width;
            break;
        case OverlayShape::BOX:
        case OverlayShape::PICTURE:
            x0 = shape.x[0];
            y0 = shape.y[0];
            x1 = shape.x[0] + shape.x[1];
            y1 = shape.y[0] + shape.y[1];
            break;
        case OverlayShape::RING:
        case OverlayShape::DISC:
            x0 = shape.x[0] - shape.radius - shape.width - 1;
            x1 = shape.x[0] + shape.radius + shape.width + 1;
            y0 = shape.y[0] - shape.radius - shape.width - 1;
            y1 = shape.y[0] + shape.radius + shape.width + 1;
            break;
        case OverlayShape::TRIANGLE:
        default:
            x0 = std::min({shape.x[0], shape.x[1], shape.x[2]});
            x1 = std::max({shape.x[0], shape.x[1], shape.x[2]});
            y0 = std::min({shape.y[0], shape.y[1], shape.y[2]});
            y1 = std::max({shape.y[0], shape.y[1], shape.y[2]});
            break;
        }
        const int top = std::max(0, static_cast<int>(std::floor(y0)) + above);
        const int bottom = std::min(height - 1, static_cast<int>(std::ceil(y1)) + above);
        const int first = std::max(0, static_cast<int>(std::floor(x0)) + left);
        const int last = std::min(width - 1, static_cast<int>(std::ceil(x1)) + left);
        for (int row = top; row <= bottom; row++) {
            for (int column = first; column <= last; column++) {
                const float px = static_cast<float>(column - left) + 0.5F;
                const float py = static_cast<float>(row - above) + 0.5F;
                u8 *pixel = rgb + ((static_cast<std::size_t>(row) * width + column) * 3);
                float coverage = 0;
                std::uint32_t colour = shape.colour;
                switch (shape.kind) {
                case OverlayShape::LINE:
                    coverage = (shape.width / 2) + 0.5F -
                               segmentDistance(px, py, shape.x[0], shape.y[0], shape.x[1], shape.y[1]);
                    break;
                case OverlayShape::BOX:
                    coverage = px >= x0 && px < x1 && py >= y0 && py < y1 ? 1 : 0;
                    break;
                case OverlayShape::RING:
                    coverage = (shape.width / 2) + 0.5F -
                               std::abs(std::hypot(px - shape.x[0], py - shape.y[0]) - shape.radius);
                    break;
                case OverlayShape::DISC:
                    coverage = shape.radius + 0.5F - std::hypot(px - shape.x[0], py - shape.y[0]);
                    break;
                case OverlayShape::TRIANGLE: {
                    const float a = edge(shape.x[0], shape.y[0], shape.x[1], shape.y[1], px, py);
                    const float b = edge(shape.x[1], shape.y[1], shape.x[2], shape.y[2], px, py);
                    const float c = edge(shape.x[2], shape.y[2], shape.x[0], shape.y[0], px, py);
                    coverage = (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0) ? 1 : 0;
                    break;
                }
                case OverlayShape::PICTURE: {
                    if (px < x0 || px >= x1 || py < y0 || py >= y1) {
                        break;
                    }
                    const int u = std::min(shape.w - 1, static_cast<int>((px - x0) * shape.w / shape.x[1]));
                    const int v = std::min(shape.h - 1, static_cast<int>((py - y0) * shape.h / shape.y[1]));
                    const std::size_t at = (static_cast<std::size_t>(v) * shape.w) + u;
                    colour = shape.kept ? (*shape.kept)[at] : overlay.pixels[shape.first + at];
                    coverage = 1;
                    if (shape.blend == static_cast<u8>(OverlayBlend::MULTIPLY)) {
                        for (int k = 0; k < 3; k++) {
                            pixel[k] = static_cast<u8>((pixel[k] * ((colour >> (24 - (8 * k))) & 0xffU)) / 255);
                        }
                        coverage = 0;
                    } else if (shape.blend == static_cast<u8>(OverlayBlend::ADD)) {
                        const std::uint32_t alpha = colour & 0xffU;
                        for (int k = 0; k < 3; k++) {
                            const std::uint32_t add = (((colour >> (24 - (8 * k))) & 0xffU) * alpha) / 255;
                            pixel[k] = static_cast<u8>(std::min<std::uint32_t>(255, pixel[k] + add));
                        }
                        coverage = 0;
                    }
                    break;
                }
                }
                blend(pixel, colour, coverage);
            }
        }
    }
}
