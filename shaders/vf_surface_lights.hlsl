#include "vf_common.hlsli"
#include "vf_local_light_data.hlsli"

sampler2D sDepth : register(s0);
sampler2D sSceneBeforeLights : register(s1);

float4 cSurfaceLight : register(c100);
float4 cSurfaceAmbient : register(c101);
float4 cSurfaceDirect : register(c102);
float4 cSurfaceToLight : register(c103);

static const float kDisplayGamma = 2.2;
static const float kDarkestIlluminant = 0.02;
static const float kFacingWrap = 0.25;
static const float kFadeSharpness = 4.0;
static const float kFaintestLight = 1.0 / 1024.0;
static const float kSmallestScene = 1e-6;

float SurfaceLightStrength()
{
    return cSurfaceLight.x;
}

float SurfaceLightReach()
{
    return cSurfaceLight.y;
}

float3 ViewPositionAt(float2 pixel)
{
    return ViewRayAtUnitDepth(pixel) * LinearDepth(SampleDepth(sDepth, pixel));
}

float3 NearerDifference(float3 centre, float3 before, float3 after)
{
    float3 backward = centre - before;
    float3 forward = after - centre;
    return abs(backward.z) < abs(forward.z) ? backward : forward;
}

float3 SurfaceNormal(float2 pixel, float3 centre)
{
    float3 across =
        NearerDifference(centre, ViewPositionAt(pixel - float2(1, 0)), ViewPositionAt(pixel + float2(1, 0)));
    float3 down = NearerDifference(centre, ViewPositionAt(pixel - float2(0, 1)), ViewPositionAt(pixel + float2(0, 1)));
    float3 normal = normalize(cross(across, down));
    return dot(normal, centre) > 0 ? -normal : normal;
}

float Facing(float3 normal, float3 direction)
{
    return saturate((dot(normal, direction) + kFacingWrap) / (1 + kFacingWrap));
}

float3 PointLightOnSurface(int index, float3 position, float3 normal)
{
    float4 positionRadius = LocalLightParameter(index * 3);
    float3 attenuation = LocalLightAttenuation(index);
    float3 toLight = positionRadius.xyz - position;
    float distanceSquared = max(dot(toLight, toLight), 1e-6);
    float inverseDistance = rsqrt(distanceSquared);
    float lightDistance = distanceSquared * inverseDistance;
    float falloff = rcp(max(attenuation.x + attenuation.y * lightDistance + attenuation.z * distanceSquared, 1));
    float boundaryFade = saturate((positionRadius.w - lightDistance) * kFadeSharpness / max(positionRadius.w, 1e-3));
    return LocalLightColour(index) * falloff * boundaryFade * Facing(normal, toLight * inverseDistance);
}

float3 EstimatedAlbedo(float3 sceneLinear, float3 normal)
{
    float3 illuminant = cSurfaceAmbient.rgb + cSurfaceDirect.rgb * saturate(dot(normal, cSurfaceToLight.xyz));
    return saturate(sceneLinear / max(illuminant, kDarkestIlluminant));
}

float4 main(float2 pixelIndex : VPOS) : COLOR0
{
    float2 pixel = pixelIndex + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    clip(BeyondFarClip(depth) ? -1 : 1);
    float3 position = ViewRayAtUnitDepth(pixel) * LinearDepth(depth);
    clip(SurfaceLightReach() - length(position));
    float3 normal = SurfaceNormal(pixel, position);
    float3 light = 0;
    [loop] for (int index = 0; index < LocalLightCount(); ++index)
        light += PointLightOnSurface(index, position, normal);
    clip(max(light.r, max(light.g, light.b)) - kFaintestLight);
    float2 viewportUv = (pixel - ViewportOrigin()) / ViewportSize();
    float3 sceneLinear = pow(max(tex2Dlod(sSceneBeforeLights, float4(viewportUv, 0, 0)).rgb, kSmallestScene),
                             kDisplayGamma);
    float3 lit = sceneLinear + EstimatedAlbedo(sceneLinear, normal) * light * SurfaceLightStrength();
    return float4(pow(saturate(lit), 1 / kDisplayGamma), 1);
}
