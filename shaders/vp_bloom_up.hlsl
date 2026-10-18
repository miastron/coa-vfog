#include "vp_bloom.hlsli"

sampler2D sLevel : register(s0);

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    return float4(Tent(sLevel, BloomUv(pixelIndex)), 0);
}
