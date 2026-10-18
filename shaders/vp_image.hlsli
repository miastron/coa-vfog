float4 cImageViewport : register(c0);
float4 cImageMetrics : register(c1);
float4 cImageSettings : register(c2);

float2 ImageOrigin()
{
    return cImageViewport.xy;
}

float2 ImagePixelSize()
{
    return cImageMetrics.xy;
}

float SharpeningStrength()
{
    return cImageSettings.x;
}

float2 ImageUv(float2 pixelIndex)
{
    return (pixelIndex + 0.5 - ImageOrigin()) * ImagePixelSize();
}
