#include "vp_bloom.hlsli"

sampler2D sLevel : register(s0);

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 uv = BloomUv(pixelIndex);
    float3 corners = BloomTap(sLevel, uv, float2(-1, -1)) + BloomTap(sLevel, uv, float2(1, -1)) +
                     BloomTap(sLevel, uv, float2(-1, 1)) + BloomTap(sLevel, uv, float2(1, 1));
    return float4((corners + BloomTap(sLevel, uv, float2(0, 0)) * 4.0) / 8.0, 1);
}
