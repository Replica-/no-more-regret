/* The port's drawing over the game's screen: lines, boxes, rings, discs,
 * triangles and pictures that the window draws at its own size over the
 * picture of the screen, sharp and blended, outside the game's 256 colours
 * (the pointer, the reticle, the wheels, the map). The places are the 640x480
 * screen's pixels, fractions too; the wide screen's sides are left of 0 and
 * right of 639. The colours are 0xRRGGBBAA.
 *
 * The game gives a function that draws the overlay (overlayAction); the
 * system layer calls it on the game thread at each frame it hands the window,
 * and at each screen a script writes, after which the shapes are drawn over
 * the screen. The shapes are the frame's only: the next frame draws them all
 * again. The system layer gives it (port/system/overlay.cpp, window.cpp). */
#ifndef PORT_PLATFORM_OVERLAY_H
#define PORT_PLATFORM_OVERLAY_H
#include <cstdint>

/* The game's function that draws the overlay, or 0 for none. */
void overlayAction(void (*draw)());

/* A line from x0,y0 to x1,y1, width pixels of the screen wide. */
void overlayLine(float x0, float y0, float x1, float y1, float width, std::uint32_t colour);
/* A box filled, w x h from x,y. */
void overlayBox(float x, float y, float w, float h, std::uint32_t colour);
/* A circle's ring of radius around x,y, width wide. */
void overlayRing(float x, float y, float radius, float width, std::uint32_t colour);
/* A circle filled. */
void overlayDisc(float x, float y, float radius, std::uint32_t colour);
/* A triangle filled. */
void overlayTriangle(float x0, float y0, float x1, float y1, float x2, float y2, std::uint32_t colour);
/* A picture of width x height pixels of 0xRRGGBBAA, drawn w x h from x,y;
 * taken at once. */
void overlayPicture(const std::uint32_t *pixels, int width, int height, float x, float y, float w, float h);
/* How a picture is laid over the screen (the port's hardware scene): blended
 * by its alpha, multiplying the screen's colours by its own (its alpha not
 * used), or added to them by its alpha; scaled smooth, or sharp (each pixel
 * a block). */
enum class OverlayBlend : std::uint8_t { BLEND, MULTIPLY, ADD };
void overlayPictureAs(const std::uint32_t *pixels, int width, int height, float x, float y, float w, float h,
                      OverlayBlend blend, bool sharp);
#endif
