#include "vp_bloom.hlsli"

sampler2D sScene : register(s0);

static const float kThreshold = 0.7;
static const float kKnee = 0.2;
static const float3 kLumaWeights = float3(0.2126, 0.7152, 0.0722);

float3 BrightPart(float3 colour)
{
    float brightness = max(colour.r, max(colour.g, colour.b));
    float knee = clamp(brightness - kThreshold + kKnee, 0.0, 2.0 * kKnee);
    knee = knee * knee / (4.0 * kKnee);
    return colour * (max(knee, brightness - kThreshold) / max(brightness, kSmallestLevel));
}

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 uv = BloomUv(pixelIndex);
    float2 offsets[4] = {float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1)};
    float3 total = 0;
    float weights = 0;
    for (int i = 0; i < 4; ++i)
    {
        float3 bright = BrightPart(ToLinear(BloomTap(sScene, uv, offsets[i])));
        float weight = 1.0 / (1.0 + dot(bright, kLumaWeights));
        total += bright * weight;
        weights += weight;
    }
    return float4(total / weights, 1);
}
