/* The window's shading on the graphics card: SDL3's GPU renderer with the
 * port's own fragment shaders (system/shaders, made by tools/shaders.sh),
 * on the window thread. The screen's 8-bit pixels go to the card as they
 * are and its palette as a texture, the colours looked up there
 * (shaders/palette.hlsl), the wide screen's dimmed sides too, into a
 * texture of the screen's size, which the window scales as it did its own
 * of the colours. Where the card takes none of the shaders' formats (DXIL
 * for Direct3D 12, SPIR-V for Vulkan; a display without a GPU device, SDL's
 * dummy driver), or SDL_HINT_RENDER_DRIVER names a renderer, the window has
 * SDL's own renderer and makes the colours itself, as before. */
#ifndef PORT_SHADING_H
#define PORT_SHADING_H
#include "video.h"

#include <SDL3/SDL.h>

/* The window's renderer: the GPU renderer with the shaders when the card
 * takes them, else SDL's own choice; nullptr for none. */
SDL_Renderer *openShading(SDL_Window *window);
/* Whether the shaders are on (openShading). */
[[nodiscard]] bool shading();
/* The shaders and their textures released, before the renderer is. */
void closeShading();
/* The screen of view in its colours, view's size, in a texture the shading
 * keeps: nullptr without the shaders or for a 16-bit screen, which the
 * window draws itself. */
SDL_Texture *shadedScreen(const ScreenView &view);

#endif
