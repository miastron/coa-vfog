#include "vf_terrain_shadows.hlsli"

#ifndef SHADOW_TRACE_TAPS
#define SHADOW_TRACE_TAPS 12
#endif

static const int kShadowTraceTaps = SHADOW_TRACE_TAPS;
static const float kShadowMinViewZ = 0.3;
static const float kShadowDepthBias = 0.3;
static const float kShadowMinStep = 1.5;
static const float kShadowStepPerYard = 0.035;
static const float kShadowThicknessInSteps = 4;
static const float kWorldShadowFullCoverageEdge = 0.7;
static const float kWorldShadowEdge = 0.99;
static const int kCharacterShadowMap = 0;
static const int kNearShadowMap = 1;
static const int kMiddleShadowMap = 2;
static const int kFarShadowMap = 3;

float4 cSunShadow : register(c10);
float4 cSunShadowLight : register(c54);
float4 cViewToShadow[12] : register(c55);

sampler2D sCharacterShadow : register(s11);
sampler2D sNearShadow : register(s12);
sampler2D sMiddleShadow : register(s13);
sampler2D sFarShadow : register(s14);

bool SunShadowsEnabled()
{
    return cSunShadow.x > 0;
}

bool HardwareShadowComparison()
{
    return cSunShadow.y > 0;
}

bool EnvironmentShadowMapsPresent()
{
    return cSunShadow.z > 0;
}

bool CharacterShadowMapPresent()
{
    return cSunShadow.w > 0;
}

float3 DirectionToShadowLightView()
{
    return cSunShadowLight.xyz;
}

float3 ShadowPosition(float3 viewPosition, int mapIndex)
{
    float4 position = float4(viewPosition, 1);
    int firstRow = mapIndex * 3;
    return float3(dot(position, cViewToShadow[firstRow]), dot(position, cViewToShadow[firstRow + 1]),
                  dot(position, cViewToShadow[firstRow + 2]));
}

float WorldShadowCoverage(float3 shadowPosition)
{
    float edge = max(abs(shadowPosition.x), abs(shadowPosition.y));
    float inDepthRange = shadowPosition.z > 0 && shadowPosition.z < 1 ? 1 : 0;
    return saturate((kWorldShadowEdge - edge) / (kWorldShadowEdge - kWorldShadowFullCoverageEdge)) * inDepthRange;
}

float WorldShadowVisibility(sampler2D shadowMap, float3 shadowPosition)
{
    float2 uv = shadowPosition.xy * 0.5 + 0.5;
    float stored = tex2Dlod(shadowMap, float4(uv, shadowPosition.z, 0)).r;
    return HardwareShadowComparison() ? stored : step(shadowPosition.z, stored);
}

void BlendWorldShadowMap(sampler2D shadowMap, int mapIndex, float3 viewPosition, inout float lit,
                         inout float uncovered)
{
    [branch] if (uncovered <= 0)
        return;
    float3 shadowPosition = ShadowPosition(viewPosition, mapIndex);
    float coverage = WorldShadowCoverage(shadowPosition);
    [branch] if (coverage <= 0)
        return;
    lit += uncovered * coverage * WorldShadowVisibility(shadowMap, shadowPosition);
    uncovered *= 1 - coverage;
}

bool SceneOccludes(float sceneDepth, float2 segmentViewZ, float occluderThickness)
{
    float sceneViewZ = LinearDepth(sceneDepth);
    return sceneDepth < 1 && !BeyondFarClip(sceneDepth) &&
           sceneViewZ < max(segmentViewZ.x, segmentViewZ.y) - kShadowDepthBias &&
           sceneViewZ > min(segmentViewZ.x, segmentViewZ.y) - occluderThickness;
}

float ScreenSpaceSunVisibility(float3 viewPosition, float sampleDistance, float jitter, float surfaceMask)
{
    [branch] if (viewPosition.z < kShadowMinViewZ)
        return 1;
    float3 toLight = DirectionToShadowLightView();
    float traceLength = min(FarClip(), (viewPosition.z - kShadowMinViewZ) / max(-toLight.z, 1e-6));
    float3 endPosition = viewPosition + toLight * traceLength;
    float2 startPixel = ViewToPixel(viewPosition);
    float2 pixelDelta = ViewToPixel(endPosition) - startPixel;
    float2 toEdge = min((ViewportOrigin() + ViewportSize() - startPixel) / max(pixelDelta, 1e-6),
                        (ViewportOrigin() - startPixel) / min(pixelDelta, -1e-6));
    float endFraction = saturate(min(toEdge.x, toEdge.y));
    float2 inverseZ = float2(1 / viewPosition.z, 1 / endPosition.z);
    inverseZ.y = lerp(inverseZ.x, inverseZ.y, endFraction);
    float3 segmentStep = float3(pixelDelta * endFraction, inverseZ.y - inverseZ.x) / kShadowTraceTaps;
    float3 sample = float3(startPixel, inverseZ.x);
    float occluderThickness = max(kShadowMinStep, sampleDistance * kShadowStepPerYard) * kShadowThicknessInSteps;
    occluderThickness = max(occluderThickness, surfaceMask * step(1e-6, toLight.z) * (MaxFogDistance() + FarClip()));
    [loop] for (int k = 0; k < kShadowTraceTaps; k++)
    {
        float2 segmentViewZ = 1 / float2(sample.z, sample.z + segmentStep.z);
        float2 pixel = sample.xy + segmentStep.xy * jitter;
        [branch] if (SceneOccludes(SampleDepth(sDepth, pixel), segmentViewZ, occluderThickness))
            return 0;
        sample += segmentStep;
    }
    return 1;
}

float SunVisibility(float3 viewPosition, float sampleDistance, float jitter, float surfaceMask)
{
    float terrainLit = TerrainSunVisibility(CameraPositionWorld() + ViewToWorldDirection(viewPosition));
    [branch] if (terrainLit <= 0)
        return 0;
    float lit = 0;
    float uncovered = 1;
    [branch] if (EnvironmentShadowMapsPresent())
    {
        BlendWorldShadowMap(sNearShadow, kNearShadowMap, viewPosition, lit, uncovered);
        BlendWorldShadowMap(sMiddleShadow, kMiddleShadowMap, viewPosition, lit, uncovered);
        BlendWorldShadowMap(sFarShadow, kFarShadowMap, viewPosition, lit, uncovered);
    }
    [branch] if (uncovered > 0)
        lit += uncovered * ScreenSpaceSunVisibility(viewPosition, sampleDistance, jitter, surfaceMask);
    [branch] if (CharacterShadowMapPresent() && lit > 0)
    {
        float characterLit = 0;
        float characterUncovered = 1;
        BlendWorldShadowMap(sCharacterShadow, kCharacterShadowMap, viewPosition, characterLit, characterUncovered);
        lit = min(lit, characterLit + characterUncovered);
    }
    return lit * terrainLit;
}
