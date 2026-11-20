/* The game's 8-bit screen in its palette's colours (shading.h), drawn one
 * pixel to a pixel of the target: each pixel's entry (the screen, SDL's
 * texture at slot 0, its red the entry of 255) looked up in the palette
 * (slot 1, 256 x 2: the colours, and at half under them for the wide
 * screen's dimmed sides). SDL's GPU renderer's own fragment interface
 * (its tri_texture vertex shader's outputs, uniforms in space 3, the
 * samplers in space 2); its shaders are made through SPIR-V, which names
 * the outputs by their places: TEXCOORD0 the colour, TEXCOORD1 the
 * texture's place. tools/shaders.sh makes palette.inc of it. */

cbuffer Screen : register(b0, space3) {
    /* The screen's width and height, and the dimmed sides': the columns
     * left of the first not dimmed and from the first dimmed on the right
     * (0 and the width without). */
    float4 screen;
};

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> entries : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState entriesSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> palette : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState paletteSampler : register(s1, space2);

struct PSInput {
    [[vk::location(0)]] float4 v_color : TEXCOORD0;
    [[vk::location(1)]] float2 v_uv : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target {
    const float entry = round(entries.Sample(entriesSampler, input.v_uv).r * 255.0);
    const float column = floor(input.v_uv.x * screen.x);
    const float dimmed = column < screen.z || column >= screen.w ? 1.0 : 0.0;
    const float2 at = float2((entry + 0.5) / 256.0, (dimmed + 0.5) / 2.0);
    return float4(palette.Sample(paletteSampler, at).rgb, 1.0) * input.v_color;
}
