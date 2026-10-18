#include "vp_smaa.hlsli"

sampler2D sSceneColour : register(s0);
sampler2D sBlendWeights : register(s1);

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 uv = ImageUv(pixelIndex);
    float4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    return float4(SMAANeighborhoodBlendingPS(uv, offset, sSceneColour, sBlendWeights).rgb, 1);
}
