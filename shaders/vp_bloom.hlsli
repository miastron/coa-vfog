#include "vp_image.hlsli"

float4 cBloomTexels : register(c3);

static const float kDisplayGamma = 2.2;
static const float kSmallestLevel = 1.0 / 16777216.0;

float2 BloomSourceTexel()
{
    return cBloomTexels.xy;
}

float2 BloomUv(float2 pixelIndex)
{
    return (pixelIndex + 0.5) * cBloomTexels.zw;
}

float BloomScale()
{
    return cImageSettings.y;
}

float3 ToLinear(float3 colour)
{
    return pow(max(colour, kSmallestLevel), kDisplayGamma);
}

float3 ToDisplay(float3 colour)
{
    return colour > kSmallestLevel ? pow(max(colour, kSmallestLevel), 1.0 / kDisplayGamma) : 0.0;
}

float3 BloomTap(sampler2D source, float2 uv, float2 offset)
{
    return tex2Dlod(source, float4(uv + offset * BloomSourceTexel(), 0, 0)).rgb;
}

float3 Tent(sampler2D source, float2 uv)
{
    float3 corners = BloomTap(source, uv, float2(-1, -1)) + BloomTap(source, uv, float2(1, -1)) +
                     BloomTap(source, uv, float2(-1, 1)) + BloomTap(source, uv, float2(1, 1));
    float3 sides = BloomTap(source, uv, float2(0, -1)) + BloomTap(source, uv, float2(-1, 0)) +
                   BloomTap(source, uv, float2(1, 0)) + BloomTap(source, uv, float2(0, 1));
    return (corners + sides * 2.0 + BloomTap(source, uv, float2(0, 0)) * 4.0) / 16.0;
}
