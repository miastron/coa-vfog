#include "vp_bloom.hlsli"

sampler2D sBloom : register(s0);

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float3 bloom = Tent(sBloom, ImageUv(pixelIndex)) * BloomScale();
    return float4(saturate(ToDisplay(bloom)), 1);
}
