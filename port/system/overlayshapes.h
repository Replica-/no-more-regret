/* The overlay's shapes (platform/overlay.h) as the system layer keeps them:
 * gathered from the game's function on the game thread, then drawn by the
 * window's renderer (window.cpp) or over a script's screen (video.cpp). */
#ifndef PORT_OVERLAYSHAPES_H
#define PORT_OVERLAYSHAPES_H
#include "common.h"

#include <cstdint>
#include <vector>

struct OverlayShape {
    enum Kind : u8 { LINE, BOX, RING, DISC, TRIANGLE, PICTURE } kind = LINE;
    /* LINE's ends, BOX's corner and size (x[1], y[1]), RING's and DISC's
     * centre, TRIANGLE's corners, PICTURE's place and size (x[1], y[1]). */
    float x[3] = {};
    float y[3] = {};
    /* LINE's and RING's width, RING's and DISC's radius in radius. */
    float width = 0;
    float radius = 0;
    std::uint32_t colour = 0;
    /* PICTURE's pixels: from first in Overlay's pixels, w x h. */
    std::size_t first = 0;
    int w = 0;
    int h = 0;
};

struct Overlay {
    std::vector<OverlayShape> shapes;
    std::vector<std::uint32_t> pixels;
    void clear()
    {
        shapes.clear();
        pixels.clear();
    }
};

/* The game's overlay now into overlay: its function called on the game's
 * side (nothing without one). The game thread's. */
void gatherOverlay(Overlay &overlay);
/* The overlay drawn over a picture of red, green and blue bytes, width x
 * height, the screen's x 0 at column left and its y 0 at row above: pixel
 * by pixel, without the window (a script's screen). */
void paintOverlay(const Overlay &overlay, u8 *rgb, int width, int height, int left, int above = 0);
#endif
