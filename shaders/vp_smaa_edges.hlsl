#include "vp_smaa.hlsli"

sampler2D sSceneColour : register(s0);

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 uv = ImageUv(pixelIndex);
    float4 offset[3];
    SMAAEdgeDetectionVS(uv, offset);
    return float4(SMAALumaEdgeDetectionPS(uv, offset, sSceneColour), 0, 0);
}
