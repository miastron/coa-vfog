float4 cLocalLightControl : register(c53);
sampler2D sLocalLights : register(s8);

float LocalLightCount()
{
    return cLocalLightControl.x;
}

float LocalLightReachLimit()
{
    return cLocalLightControl.y;
}

float LocalLightPhaseG()
{
    return cLocalLightControl.z;
}

float4 LocalLightParameter(int index)
{
    float texel = (index + 0.5) / 32.0;
    return tex2Dlod(sLocalLights, float4(texel, texel, texel, -texel));
}

float3 LocalLightColour(int light)
{
    return LocalLightParameter(light * 3 + 1).rgb;
}

float3 LocalLightAttenuation(int light)
{
    return LocalLightParameter(light * 3 + 2).xyz;
}
