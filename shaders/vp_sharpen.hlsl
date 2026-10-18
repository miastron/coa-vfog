#include "vp_image.hlsli"

sampler2D sImage : register(s0);

static const float kSoftestPeak = 8.0;
static const float kSharpestPeak = 5.0;
static const float kSmallestRange = 1.0 / 256.0;

float3 Tap(float2 uv, float2 offset)
{
    return tex2Dlod(sImage, float4(uv + offset * ImagePixelSize(), 0, 0)).rgb;
}

float3 SharpeningAmount(float3 crossMin, float3 boxMin, float3 crossMax, float3 boxMax)
{
    float3 low = crossMin + boxMin;
    float3 high = crossMax + boxMax;
    return sqrt(saturate(min(low, 2.0 - high) / max(high, kSmallestRange)));
}

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 uv = ImageUv(pixelIndex);
    float3 a = Tap(uv, float2(-1, -1));
    float3 b = Tap(uv, float2(0, -1));
    float3 c = Tap(uv, float2(1, -1));
    float3 d = Tap(uv, float2(-1, 0));
    float3 e = Tap(uv, float2(0, 0));
    float3 f = Tap(uv, float2(1, 0));
    float3 g = Tap(uv, float2(-1, 1));
    float3 h = Tap(uv, float2(0, 1));
    float3 i = Tap(uv, float2(1, 1));
    float3 crossMin = min(min(min(d, e), min(f, b)), h);
    float3 crossMax = max(max(max(d, e), max(f, b)), h);
    float3 boxMin = min(crossMin, min(min(a, c), min(g, i)));
    float3 boxMax = max(crossMax, max(max(a, c), max(g, i)));
    float3 weight = SharpeningAmount(crossMin, boxMin, crossMax, boxMax) *
                    (-1.0 / lerp(kSoftestPeak, kSharpestPeak, SharpeningStrength()));
    float3 sharpened = ((b + d + f + h) * weight + e) / (1.0 + 4.0 * weight);
    return float4(saturate(sharpened), 1);
}
