#include "vp_smaa.hlsli"

sampler2D sEdges : register(s0);
sampler2D sArea : register(s1);
sampler2D sSearch : register(s2);

static const float4 kNoSubsamples = float4(0, 0, 0, 0);

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 uv = ImageUv(pixelIndex);
    float2 pixcoord;
    float4 offset[3];
    SMAABlendingWeightCalculationVS(uv, pixcoord, offset);
    return SMAABlendingWeightCalculationPS(uv, pixcoord, offset, sEdges, sArea, sSearch, kNoSubsamples);
}
