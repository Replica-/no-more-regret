/* The window's shading on the graphics card (shading.h). */
#include "shading.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

#include "shaders/palette.inc"

/* The renderer the shaders draw with and its device; the palette's shader,
 * its sampler (each texel its own) and its state with the palette's
 * texture bound after the screen's. */
SDL_Renderer *renderer = nullptr;
SDL_GPUDevice *device = nullptr;
SDL_GPUShader *paletteShader = nullptr;
SDL_GPUSampler *nearest = nullptr;
SDL_GPURenderState *paletteState = nullptr;
/* The screen's pixels (INDEX8: a red channel of the entries on the card;
 * SDL wants a palette of any texture of them, which the shader does not
 * read), the palette's colours (256 x 2: as they are, at half under), and
 * the screen in its colours. */
SDL_Texture *entries = nullptr;
SDL_Palette *unread = nullptr;
SDL_Texture *colours = nullptr;
SDL_Texture *screen = nullptr;
/* The palette as last put in colours (none yet: kept false). */
u8 coloursShown[256 * 3];
bool coloursKept = false;

/* The uploads gathered for the card (cardUpload): their pixels in the
 * upload buffer, mapped while they gather, each row at a multiple of 256
 * bytes and each picture at one of 512 (Direct3D 12's, which SDL makes
 * again of anything else at each upload, slowly); each picture's texture,
 * place in the buffer and size. One buffer for all, kept: SDL's own upload
 * makes one each time, which Direct3D 12 takes milliseconds over. */
const Uint32 ROW_ALIGN = 256;
const Uint32 PLACE_ALIGN = 512;
struct Upload {
    SDL_GPUTexture *texture;
    Uint32 offset;
    Uint32 pixelsPerRow;
    Uint32 width;
    Uint32 height;
};
SDL_GPUTransferBuffer *uploadBuffer = nullptr;
Uint32 uploadSize = 0;
Uint8 *uploadPixels = nullptr;
Uint32 uploadUsed = 0;
std::vector<Upload> uploads;

Uint32 aligned(Uint32 value, Uint32 to)
{
    return (value + to - 1) / to * to;
}

/* The uploads gathered sent to the card, in a copy pass of their own,
 * before what draws with them; the buffer unmapped. */
void sendUploads()
{
    if (uploadPixels != nullptr) {
        SDL_UnmapGPUTransferBuffer(device, uploadBuffer);
        uploadPixels = nullptr;
    }
    uploadUsed = 0;
    if (uploads.empty()) {
        return;
    }
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        uploads.clear();
        return;
    }
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(commands);
    for (const Upload &upload : uploads) {
        const SDL_GPUTextureTransferInfo from{uploadBuffer, upload.offset, upload.pixelsPerRow, upload.height};
        SDL_GPUTextureRegion to{};
        to.texture = upload.texture;
        to.w = upload.width;
        to.h = upload.height;
        to.d = 1;
        /* Cycled: a texture the frame before still draws with is not
         * waited for, the card gives it new memory. */
        SDL_UploadToGPUTexture(pass, &from, &to, true);
    }
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(commands);
    uploads.clear();
}

/* Room for bytes in the upload buffer, mapped: the uploads gathered sent
 * first when they fill it, a larger buffer when one alone is larger; its
 * place, or nullptr for none. */
Uint8 *uploadRoom(Uint32 bytes)
{
    const Uint32 at = aligned(uploadUsed, PLACE_ALIGN);
    if (uploadBuffer == nullptr || at + bytes > uploadSize) {
        if (uploadPixels != nullptr || !uploads.empty()) {
            sendUploads();
        }
        if (bytes > uploadSize) {
            if (uploadBuffer != nullptr) {
                SDL_ReleaseGPUTransferBuffer(device, uploadBuffer);
            }
            uploadSize = std::max<Uint32>(bytes, 16U << 20);
            const SDL_GPUTransferBufferCreateInfo info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uploadSize, 0};
            uploadBuffer = SDL_CreateGPUTransferBuffer(device, &info);
            if (uploadBuffer == nullptr) {
                uploadSize = 0;
                return nullptr;
            }
        }
        return uploadRoom(bytes);
    }
    if (uploadPixels == nullptr) {
        /* Cycled: a buffer the card still copies from is not waited for. */
        uploadPixels = static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(device, uploadBuffer, true));
        if (uploadPixels == nullptr) {
            return nullptr;
        }
    }
    uploadUsed = at + bytes;
    return uploadPixels + at;
}

/* Pixels of bytes each, rows of pitch bytes, into texture with the
 * uploads; words, 0xRRGGBBAA words to the card's bytes of RGBA32. Whether
 * they were gathered. */
bool gather(SDL_Texture *texture, const void *pixels, int pitch, int bytes, bool words)
{
    auto *card = static_cast<SDL_GPUTexture *>(
        SDL_GetPointerProperty(SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    float w = 0;
    float h = 0;
    if (card == nullptr || !SDL_GetTextureSize(texture, &w, &h)) {
        return false;
    }
    const auto width = static_cast<Uint32>(w);
    const auto height = static_cast<Uint32>(h);
    const Uint32 rowBytes = aligned(width * static_cast<Uint32>(bytes), ROW_ALIGN);
    Uint8 *room = uploadRoom(rowBytes * height);
    if (room == nullptr) {
        return false;
    }
    const auto *from = static_cast<const Uint8 *>(pixels);
    for (Uint32 y = 0; y < height; y++) {
        Uint8 *row = room + (static_cast<std::size_t>(y) * rowBytes);
        const Uint8 *source = from + (static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch));
        if (!words) {
            std::memcpy(row, source, static_cast<std::size_t>(width) * static_cast<std::size_t>(bytes));
            continue;
        }
        for (Uint32 x = 0; x < width; x++) {
            Uint32 word = 0;
            std::memcpy(&word, source + (x * 4), 4);
            word = SDL_Swap32BE(word);
            std::memcpy(row + (x * 4), &word, 4);
        }
    }
    uploads.push_back({card, static_cast<Uint32>(room - uploadPixels), rowBytes / static_cast<Uint32>(bytes), width,
                       height});
    return true;
}

/* The shader of code (bytes in format) with samplers and uniforms. */
SDL_GPUShader *makeShader(SDL_GPUShaderFormat format, const unsigned char *code, std::size_t size, Uint32 samplers,
                          Uint32 uniforms)
{
    SDL_GPUShaderCreateInfo info{};
    info.code_size = size;
    info.code = code;
    info.entrypoint = "main";
    info.format = format;
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(device, &info);
}

/* The shaders and their textures on the renderer's device; whether all
 * were made. */
bool makeShaders()
{
    device = SDL_GetGPURendererDevice(renderer);
    if (device == nullptr) {
        return false;
    }
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0U && palette_dxil_size != 0) {
        paletteShader = makeShader(SDL_GPU_SHADERFORMAT_DXIL, palette_dxil, palette_dxil_size, 2, 1);
    } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0U && palette_spirv_size != 0) {
        paletteShader = makeShader(SDL_GPU_SHADERFORMAT_SPIRV, palette_spirv, palette_spirv_size, 2, 1);
    }
    if (paletteShader == nullptr) {
        return false;
    }
    SDL_GPUSamplerCreateInfo sampling{};
    sampling.min_filter = SDL_GPU_FILTER_NEAREST;
    sampling.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampling.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampling.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampling.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampling.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearest = SDL_CreateGPUSampler(device, &sampling);
    colours = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 256, 2);
    unread = SDL_CreatePalette(256);
    if (nearest == nullptr || colours == nullptr || unread == nullptr) {
        return false;
    }
    auto *card = static_cast<SDL_GPUTexture *>(
        SDL_GetPointerProperty(SDL_GetTextureProperties(colours), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    if (card == nullptr) {
        return false;
    }
    const SDL_GPUTextureSamplerBinding binding{card, nearest};
    SDL_GPURenderStateCreateInfo state{};
    state.fragment_shader = paletteShader;
    state.num_sampler_bindings = 1;
    state.sampler_bindings = &binding;
    paletteState = SDL_CreateGPURenderState(renderer, &state);
    return paletteState != nullptr;
}

/* A texture of format and access kept at width x height: made again when
 * another size; whether there is one. */
bool sized(SDL_Texture *&texture, SDL_PixelFormat format, SDL_TextureAccess access, int width, int height)
{
    float w = 0;
    float h = 0;
    if (texture != nullptr && SDL_GetTextureSize(texture, &w, &h) && static_cast<int>(w) == width &&
        static_cast<int>(h) == height) {
        return true;
    }
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
    }
    texture = SDL_CreateTexture(renderer, format, access, width, height);
    return texture != nullptr;
}

/* The palette into colours, when it changed: each entry's colour, and at
 * half (as the window's own dimming has it) under it. */
void keepColours(const u8 *palette)
{
    if (coloursKept && std::memcmp(coloursShown, palette, sizeof coloursShown) == 0) {
        return;
    }
    u8 pixels[256 * 2 * 4];
    for (int i = 0; i < 256; i++) {
        for (int k = 0; k < 3; k++) {
            const u8 value = palette[(i * 3) + k];
            pixels[(i * 4) + k] = value;
            pixels[((256 + i) * 4) + k] = static_cast<u8>(value >> 1);
        }
        pixels[(i * 4) + 3] = 0xff;
        pixels[((256 + i) * 4) + 3] = 0xff;
    }
    if (gather(colours, pixels, 256 * 4, 4, false) || SDL_UpdateTexture(colours, nullptr, pixels, 256 * 4)) {
        std::memcpy(coloursShown, palette, sizeof coloursShown);
        coloursKept = true;
    }
}

}

SDL_Renderer *openShading(SDL_Window *window)
{
    /* A renderer named (SDL_HINT_RENDER_DRIVER): that one, unshaded. */
    if (SDL_GetHint(SDL_HINT_RENDER_DRIVER) == nullptr) {
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
        SDL_SetStringProperty(properties, SDL_PROP_RENDERER_CREATE_NAME_STRING, "gpu");
        SDL_SetBooleanProperty(properties, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_DXIL_BOOLEAN, palette_dxil_size != 0);
        SDL_SetBooleanProperty(properties, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN,
                               palette_spirv_size != 0);
        renderer = SDL_CreateRendererWithProperties(properties);
        SDL_DestroyProperties(properties);
    }
    if (renderer != nullptr && !makeShaders()) {
        SDL_Renderer *const unshaded = renderer;
        closeShading();
        SDL_DestroyRenderer(unshaded);
    }
    if (renderer != nullptr) {
        return renderer;
    }
    return SDL_CreateRenderer(window, nullptr);
}

bool shading()
{
    return paletteState != nullptr;
}

void closeShading()
{
    for (SDL_Texture **texture : {&entries, &colours, &screen}) {
        if (*texture != nullptr) {
            SDL_DestroyTexture(*texture);
            *texture = nullptr;
        }
    }
    if (unread != nullptr) {
        SDL_DestroyPalette(unread);
        unread = nullptr;
    }
    if (paletteState != nullptr) {
        SDL_DestroyGPURenderState(paletteState);
        paletteState = nullptr;
    }
    if (nearest != nullptr) {
        SDL_ReleaseGPUSampler(device, nearest);
        nearest = nullptr;
    }
    if (paletteShader != nullptr) {
        SDL_ReleaseGPUShader(device, paletteShader);
        paletteShader = nullptr;
    }
    if (uploadPixels != nullptr) {
        SDL_UnmapGPUTransferBuffer(device, uploadBuffer);
        uploadPixels = nullptr;
    }
    if (uploadBuffer != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, uploadBuffer);
        uploadBuffer = nullptr;
    }
    uploadSize = 0;
    uploadUsed = 0;
    uploads.clear();
    coloursKept = false;
    device = nullptr;
    renderer = nullptr;
}

SDL_PixelFormat pictureFormat()
{
    return shading() ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGBA8888;
}

void putPicture(SDL_Texture *texture, const std::uint32_t *pixels, int width)
{
    const int pitch = width * static_cast<int>(sizeof(std::uint32_t));
    if (!shading()) {
        SDL_UpdateTexture(texture, nullptr, pixels, pitch);
        return;
    }
    if (gather(texture, pixels, pitch, 4, true)) {
        return;
    }
    /* No room on the card: the words made bytes here, SDL's upload. */
    float w = 0;
    float h = 0;
    SDL_GetTextureSize(texture, &w, &h);
    std::vector<std::uint32_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(h));
    for (std::size_t i = 0; i < bytes.size(); i++) {
        bytes[i] = SDL_Swap32BE(pixels[i]);
    }
    SDL_UpdateTexture(texture, nullptr, bytes.data(), pitch);
}

void sendPictures()
{
    if (shading()) {
        sendUploads();
    }
}

SDL_Texture *shadedScreen(const ScreenView &view)
{
    if (!shading() || view.bits != 8) {
        return nullptr;
    }
    if (!sized(entries, SDL_PIXELFORMAT_INDEX8, SDL_TEXTUREACCESS_STATIC, view.width, view.height) ||
        (SDL_GetTexturePalette(entries) != unread && !SDL_SetTexturePalette(entries, unread)) ||
        !sized(screen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, view.width, view.height)) {
        return nullptr;
    }
    keepColours(view.palette);
    if (!gather(entries, view.pixels, view.pitch, 1, false) &&
        !SDL_UpdateTexture(entries, nullptr, view.pixels, view.pitch)) {
        return nullptr;
    }
    /* On the card before the draw that reads them. */
    sendUploads();
    const float left = view.dimmed ? view.sides : 0;
    const float right = view.dimmed ? static_cast<float>(view.width - view.sides) : view.width;
    const float uniforms[4] = {static_cast<float>(view.width), static_cast<float>(view.height), left, right};
    SDL_SetGPURenderStateFragmentUniforms(paletteState, 0, uniforms, sizeof uniforms);
    SDL_Texture *const shown = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, screen);
    SDL_SetTextureBlendMode(entries, SDL_BLENDMODE_NONE);
    SDL_SetGPURenderState(renderer, paletteState);
    const bool drawn = SDL_RenderTexture(renderer, entries, nullptr, nullptr);
    SDL_SetGPURenderState(renderer, nullptr);
    SDL_SetRenderTarget(renderer, shown);
    /* Drawn as it is, as the window's own texture of the colours. */
    SDL_SetTextureBlendMode(screen, SDL_BLENDMODE_NONE);
    return drawn ? screen : nullptr;
}
