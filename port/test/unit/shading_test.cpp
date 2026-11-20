/* The window's shading on the graphics card (port/system/shading.h), whose
 * oracle is the window's own colours (window.cpp's screenTexture): a
 * screen of every entry, in a palette of every value, the wide screen's
 * sides dimmed, looked up by the shader in a hidden window and read back,
 * each pixel the colour the window makes of it. Skipped (77) where there
 * is no display or the card takes none of the shaders. */
#include "shading.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int checks = 0, failures = 0;

void check(bool ok, const std::string &what)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("MISMATCH shading: %s\n", what.c_str());
    }
}

/* The colour the window makes of entry at column x (0xRRGGBB). */
Uint32 windowColour(const ScreenView &view, u8 entry, int x)
{
    const Uint32 colour = static_cast<Uint32>(view.palette[entry * 3]) << 16 | view.palette[(entry * 3) + 1] << 8 |
                          view.palette[(entry * 3) + 2];
    const bool dimmed = view.dimmed && (x < view.sides || x >= view.width - view.sides);
    return dimmed ? (colour >> 1) & 0x7f7f7f : colour;
}

/* The screen of view through the shader, read back as 0xRRGGBB; whether it
 * was. */
bool shaded(SDL_Renderer *renderer, const ScreenView &view, std::vector<Uint32> &out)
{
    SDL_Texture *screen = shadedScreen(view);
    if (screen == nullptr || !SDL_SetRenderTarget(renderer, screen)) {
        return false;
    }
    SDL_Surface *read = SDL_RenderReadPixels(renderer, nullptr);
    SDL_SetRenderTarget(renderer, nullptr);
    if (read == nullptr) {
        return false;
    }
    SDL_Surface *packed = SDL_ConvertSurface(read, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(read);
    if (packed == nullptr) {
        return false;
    }
    out.assign(static_cast<std::size_t>(view.width) * view.height, 0);
    for (int y = 0; y < view.height && y < packed->h; y++) {
        const auto *row =
            reinterpret_cast<const Uint32 *>(static_cast<const u8 *>(packed->pixels) + (y * packed->pitch));
        for (int x = 0; x < view.width && x < packed->w; x++) {
            out[(static_cast<std::size_t>(y) * view.width) + x] = row[x] & 0xffffff;
        }
    }
    SDL_DestroySurface(packed);
    return true;
}

/* Each pixel of view as shaded against the window's colour. */
void compare(SDL_Renderer *renderer, const ScreenView &view, const std::string &what)
{
    std::vector<Uint32> out;
    if (!shaded(renderer, view, out)) {
        check(false, what + ": shaded and read back (" + SDL_GetError() + ")");
        return;
    }
    int wrong = 0;
    std::string first;
    for (int y = 0; y < view.height; y++) {
        for (int x = 0; x < view.width; x++) {
            const Uint32 want = windowColour(view, view.pixels[(y * view.pitch) + x], x);
            const Uint32 got = out[(static_cast<std::size_t>(y) * view.width) + x];
            if (got != want && wrong++ == 0) {
                char text[96];
                std::snprintf(text, sizeof text, " (first at %d,%d: %06x for %06x)", x, y, got, want);
                first = text;
            }
        }
    }
    check(wrong == 0, what + ": " + std::to_string(wrong) + " pixels not the window's colour" + first);
}

}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("shading: skipped, no display (%s)\n", SDL_GetError());
        return 77;
    }
    SDL_Window *window = SDL_CreateWindow("shading", 64, 64, SDL_WINDOW_HIDDEN);
    SDL_Renderer *renderer = window != nullptr ? openShading(window) : nullptr;
    if (renderer == nullptr || !shading()) {
        std::printf("shading: skipped, the card takes none of the shaders (%s)\n",
                    renderer != nullptr ? SDL_GetRendererName(renderer) : SDL_GetError());
        closeShading();
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        SDL_Quit();
        return 77;
    }

    /* Every entry in rows, its pitch wider than the screen; every value of
     * a component somewhere in the palette. */
    const int width = 300;
    const int height = 40;
    const int pitch = 320;
    std::vector<u8> pixels(static_cast<std::size_t>(pitch) * height, 0);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            pixels[(y * pitch) + x] = static_cast<u8>((x * 7) + (y * 13));
        }
    }
    ScreenView view{};
    view.pixels = pixels.data();
    view.width = width;
    view.height = height;
    view.pitch = pitch;
    view.bits = 8;
    for (int i = 0; i < 256; i++) {
        view.palette[i * 3] = static_cast<u8>(i);
        view.palette[(i * 3) + 1] = static_cast<u8>(255 - i);
        view.palette[(i * 3) + 2] = static_cast<u8>(i * 37);
    }
    compare(renderer, view, "the screen in its palette");

    /* The wide screen's sides dimmed, and undimmed. */
    view.sides = 30;
    view.dimmed = true;
    compare(renderer, view, "the dimmed sides at half");
    view.dimmed = false;
    compare(renderer, view, "the sides undimmed");

    /* A new palette (the game's fades) and a new size. */
    for (int i = 0; i < 256; i++) {
        view.palette[i * 3] = static_cast<u8>(255 - i);
    }
    compare(renderer, view, "a changed palette");
    view.width = 200;
    view.height = 30;
    compare(renderer, view, "a smaller screen");

    /* The overlay's pictures through the card's uploads: an odd width (rows
     * padded on the way), its 0xRRGGBBAA words its colours. Two at once, a
     * second time into the same texture. */
    for (int round = 0; round < 2; round++) {
        const int pw = 37;
        const int ph = 9;
        std::vector<std::uint32_t> words(static_cast<std::size_t>(pw) * ph);
        for (int i = 0; i < pw * ph; i++) {
            words[i] = (static_cast<std::uint32_t>((i * 5) + round) & 0xffU) << 24 |
                       (static_cast<std::uint32_t>(i * 3) & 0xffU) << 16 | (static_cast<std::uint32_t>(255 - i) & 0xffU)
                                                                                << 8 |
                       0xffU;
        }
        static SDL_Texture *picture = SDL_CreateTexture(renderer, pictureFormat(), SDL_TEXTUREACCESS_STATIC, pw, ph);
        SDL_Texture *other = SDL_CreateTexture(renderer, pictureFormat(), SDL_TEXTUREACCESS_STATIC, 300, 3);
        std::vector<std::uint32_t> wide(300 * 3, 0x10203040U);
        putPicture(picture, words.data(), pw, ph);
        putPicture(other, wide.data(), 300, 3);
        sendPictures();
        SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pw, ph);
        SDL_SetRenderTarget(renderer, target);
        SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(renderer, picture, nullptr, nullptr);
        SDL_Surface *read = SDL_RenderReadPixels(renderer, nullptr);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_Surface *packed = read != nullptr ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA8888) : nullptr;
        int wrong = 0;
        for (int y = 0; packed != nullptr && y < ph; y++) {
            const auto *row = reinterpret_cast<const Uint32 *>(static_cast<const u8 *>(packed->pixels) +
                                                               (y * packed->pitch));
            for (int x = 0; x < pw; x++) {
                wrong += row[x] != words[(y * pw) + x] ? 1 : 0;
            }
        }
        check(packed != nullptr && wrong == 0,
              "a picture through the card's uploads, round " + std::to_string(round) + ": " + std::to_string(wrong) +
                  " pixels wrong");
        SDL_DestroySurface(read);
        SDL_DestroySurface(packed);
        SDL_DestroyTexture(target);
        SDL_DestroyTexture(other);
    }

    /* A picture in the top left of a larger texture (the window's frame
     * pictures, their sizes rounded up): its own pixels there, its last
     * column and row once more beside them. */
    {
        const int pw = 37;
        const int ph = 9;
        const int tw = 64;
        const int th = 32;
        std::vector<std::uint32_t> words(static_cast<std::size_t>(pw) * ph);
        for (int i = 0; i < pw * ph; i++) {
            words[i] = (static_cast<std::uint32_t>(i * 7) & 0xffU) << 24 | (static_cast<std::uint32_t>(i) & 0xffU) << 8 |
                       0xffU;
        }
        SDL_Texture *picture = SDL_CreateTexture(renderer, pictureFormat(), SDL_TEXTUREACCESS_STATIC, tw, th);
        putPicture(picture, words.data(), pw, ph);
        sendPictures();
        SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pw + 1,
                                                ph + 1);
        SDL_SetRenderTarget(renderer, target);
        SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
        const SDL_FRect part{0, 0, static_cast<float>(pw + 1), static_cast<float>(ph + 1)};
        SDL_RenderTexture(renderer, picture, &part, nullptr);
        SDL_Surface *read = SDL_RenderReadPixels(renderer, nullptr);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_Surface *packed = read != nullptr ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA8888) : nullptr;
        int wrong = 0;
        for (int y = 0; packed != nullptr && y <= ph; y++) {
            const auto *row = reinterpret_cast<const Uint32 *>(static_cast<const u8 *>(packed->pixels) +
                                                               (y * packed->pitch));
            for (int x = 0; x <= pw; x++) {
                wrong += row[x] != words[(std::min(y, ph - 1) * pw) + std::min(x, pw - 1)] ? 1 : 0;
            }
        }
        check(packed != nullptr && wrong == 0,
              "a picture in a larger texture: " + std::to_string(wrong) + " pixels wrong");
        SDL_DestroySurface(read);
        SDL_DestroySurface(packed);
        SDL_DestroyTexture(target);
        SDL_DestroyTexture(picture);
    }

    /* A 16-bit screen is the window's to draw. */
    view.bits = 16;
    check(shadedScreen(view) == nullptr, "no shaded 16-bit screen");

    std::printf("shading: %d checks, %d mismatches (%s)\n", checks, failures, SDL_GetRendererName(renderer));
    closeShading();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures ? 1 : 0;
}
