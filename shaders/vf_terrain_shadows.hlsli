static const float kTerrainMapOriginYards = 17066.666;
static const float kTerrainVertexYards = 33.333332 / 8;
static const float kTerrainWindowVertices = 512;
static const float kTerrainPenumbraYards = 2;
static const float kTerrainSurfaceBiasYards = 1;

float4 cTerrainShadow : register(c67);

sampler2D sTerrainShadowHeight : register(s5);

bool TerrainShadowsEnabled()
{
    return cTerrainShadow.z > 0;
}

float2 TerrainWindowFirstVertex()
{
    return cTerrainShadow.xy;
}

float TerrainShadowHeightAt(float2 texel)
{
    return tex2Dlod(sTerrainShadowHeight, float4((texel + 0.5) / kTerrainWindowVertices, 0, 0)).r;
}

float TerrainShadowHeight(float2 windowVertex)
{
    float2 texel = windowVertex.yx;
    float2 cell = floor(texel);
    float2 fraction = texel - cell;
    float top = lerp(TerrainShadowHeightAt(cell), TerrainShadowHeightAt(cell + float2(1, 0)), fraction.x);
    float bottom = lerp(TerrainShadowHeightAt(cell + float2(0, 1)), TerrainShadowHeightAt(cell + 1), fraction.x);
    return lerp(top, bottom, fraction.y);
}

float TerrainSunVisibility(float3 worldPosition)
{
    [branch] if (!TerrainShadowsEnabled())
        return 1;
    float2 mapVertex = (kTerrainMapOriginYards - worldPosition.xy) / kTerrainVertexYards;
    float2 windowVertex = mapVertex - TerrainWindowFirstVertex();
    [branch] if (any(windowVertex < 0) || any(windowVertex > kTerrainWindowVertices - 1))
        return 1;
    float shadowHeight = TerrainShadowHeight(windowVertex);
    return saturate(0.5 + (worldPosition.z + kTerrainSurfaceBiasYards - shadowHeight) / kTerrainPenumbraYards);
}
