#pragma once

#include "ps_shadowed_march_high.h"
#include "ps_shadowed_march_low.h"
#include "ps_shadowed_march_mid.h"

namespace sun_shadow_checks
{
constexpr int kFixtureConstants = 99;
constexpr int kSunShadowRegister = 10;
constexpr int kSunShadowLightRegister = 54;
constexpr int kViewToShadowRegister = 55;
constexpr DWORD kFirstWorldShadowStage = 11;
constexpr int kWorldShadowMaps = 4;
constexpr int kCharacterMap = 0;
constexpr int kNearMap = 1;
constexpr int kMiddleMap = 2;
constexpr int kFarMap = 3;
constexpr float kOffMap = 2.0f;
constexpr float kCasterSide = -0.5f;
constexpr float kOpenSide = 0.5f;
constexpr float kHalfCoveredEdge = -0.845f;
constexpr float kNearCasterDepth = 0.25f;
constexpr float kFarCasterDepth = 0.75f;
constexpr float kReceiverDepth = 0.5f;
constexpr float kReceiverBeforeCaster = 0.1f;
constexpr float kReceiverOutsideMap = 1.1f;
constexpr float kLayerEmissive = 0.1f;
constexpr float kLayerDiffuse = 0.75f;
constexpr float kLayerDensity = 0.003f;
constexpr float kWallYards = 120.0f;
constexpr float kForegroundYards = 2.0f;
constexpr int kDepthSize = 64;
constexpr int kForegroundColumns = 35;
constexpr int kTerrainShadowRegister = 67;
constexpr DWORD kTerrainShadowStage = 5;
constexpr float kTerrainAboveEveryRay = 100000.0f;
constexpr float kCameraMapVertex = kTerrainMapOriginYards / kTerrainVertexYards;
constexpr float kNearWindowStart = 100.0f;
constexpr float kFarWindowStart = 400.0f;

using FixtureConstants = float[kFixtureConstants][4];

void FillSunShadowFixture(FixtureConstants& constants, bool shafts)
{
    std::memset(constants, 0, sizeof(FixtureConstants));
    constants[0][2] = constants[0][3] = kDepthSize;
    constants[1][0] = 8;
    constants[1][2] = constants[1][3] = 1.0f / kDepthSize;
    constants[2][0] = constants[2][1] = 1;
    constants[3][0] = 0.940376f;
    constants[3][1] = -0.3761504f;
    constants[3][2] = 1000;
    constants[3][3] = 0.94f;
    for (int row = 0; row < 4; ++row)
        constants[4 + row][row] = 1;
    constants[8][0] = constants[8][1] = 8;
    constants[8][2] = constants[8][3] = 0.125f;
    constants[9][2] = constants[9][3] = 1;
    constants[kSunShadowRegister][0] = shafts ? 1.0f : 0.0f;
    constants[11][1] = constants[11][3] = 1000;
    constants[11][2] = 850;
    constants[12][1] = kLayerDensity;
    constants[12][3] = 1;
    constants[13][0] = constants[13][1] = constants[13][2] = kLayerEmissive;
    constants[14][0] = constants[14][1] = constants[14][2] = kLayerDiffuse;
    constants[14][3] = 1;
    constants[16][0] = constants[16][1] = constants[16][2] = kLayerEmissive;
    constants[16][3] = 1;
    constants[17][2] = 1000;
    constants[kSunShadowLightRegister][2] = 1;
}

float WallRadiance(float source)
{
    return source * (1 - std::exp(-kLayerDensity * kWallYards * std::sqrt(1 + 2 * 0.125f * 0.125f)));
}

constexpr float kSkyDepth = 1.0f;

float FixtureDepth(bool sky, bool foreground, int column)
{
    if (sky)
        return kSkyDepth;
    return 0.940376f - 0.3761504f / (foreground && column < kForegroundColumns ? kForegroundYards : kWallYards);
}

bool FillViewDepth(IDirect3DTexture9* depth, bool sky, bool foreground)
{
    D3DLOCKED_RECT locked = {};
    if (FAILED(depth->LockRect(0, &locked, nullptr, 0)))
        return false;
    for (int y = 0; y < kDepthSize; ++y)
    {
        float* row = reinterpret_cast<float*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < kDepthSize; ++x)
            row[x] = FixtureDepth(sky, foreground, x);
    }
    return SUCCEEDED(depth->UnlockRect(0));
}

float DrawFixture(IDirect3DDevice9* device, const FogIntegrationResources& resources, const FixtureConstants& constants,
                  HRESULT& draw)
{
    const float quad[4][4] = {{-0.5f, -0.5f, 0, 1}, {7.5f, -0.5f, 0, 1}, {-0.5f, 7.5f, 0, 1}, {7.5f, 7.5f, 0, 1}};
    device->SetPixelShaderConstantF(0, &constants[0][0], kFixtureConstants);
    const HRESULT begin = device->BeginScene();
    draw = SUCCEEDED(begin) ? device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])) : begin;
    if (SUCCEEDED(begin))
        device->EndScene();
    return ReadFogIntegrationOpacity(device, resources);
}

void CheckSunBlockedBehindWall(IDirect3DDevice9* device, const FogIntegrationResources& resources, int quality,
                               IDirect3DTexture9* depth)
{
    for (int foreground = 0; foreground < 2; ++foreground)
        for (int shafts = 0; shafts < 2; ++shafts)
        {
            FixtureConstants constants;
            FillSunShadowFixture(constants, shafts != 0);
            HRESULT draw = E_FAIL;
            const bool filled = FillViewDepth(depth, false, foreground != 0);
            const float actual = filled ? DrawFixture(device, resources, constants, draw) : -1.0f;
            const float expected = WallRadiance(shafts ? kLayerEmissive : kLayerEmissive + kLayerDiffuse);
            char label[200];
            std::snprintf(label, sizeof(label),
                          "shadowed march quality %d: fog in front of a wall with the sun behind it, foreground "
                          "silhouette %d, LightShafts %d keeps %s (error %.3f)",
                          quality, foreground, shafts, shafts ? "only its ambient light" : "its sunlight",
                          std::fabs(actual - expected));
            Check(SUCCEEDED(draw) && std::fabs(actual - expected) <= 2.0f / 255, label);
        }
}

struct WorldShadowCase
{
    const char* name;
    bool environment;
    float mapX[kWorldShadowMaps];
    float receiverDepth;
    float visibility;
};

const WorldShadowCase kWorldShadowCases[] = {
    {"an off-screen character occluder blocks the light", false, {kCasterSide, kOffMap, kOffMap, kOffMap},
     kReceiverDepth, 0},
    {"a receiver before the caster stays lit", false, {kCasterSide, kOffMap, kOffMap, kOffMap},
     kReceiverBeforeCaster, 1},
    {"outside the map's depth range the image trace decides", false, {kCasterSide, kOffMap, kOffMap, kOffMap},
     kReceiverOutsideMap, 1},
    {"the character map fades out at its border", false, {kHalfCoveredEdge, kOffMap, kOffMap, kOffMap},
     kReceiverDepth, 0.5f},
    {"an off-screen environment caster blocks the light", true, {kOffMap, kCasterSide, kOffMap, kOffMap},
     kReceiverDepth, 0},
    {"the middle cascade covers a missing near cascade", true, {kOffMap, kOffMap, kCasterSide, kOffMap},
     kReceiverDepth, 0},
    {"the far cascade covers missing nearer cascades", true, {kOffMap, kOffMap, kOffMap, kCasterSide},
     kReceiverDepth, 0},
    {"the near cascade takes precedence over coarser ones", true, {kOffMap, kOpenSide, kCasterSide, kCasterSide},
     kReceiverDepth, 1},
    {"a cascade border blends into the next map", true, {kOffMap, kHalfCoveredEdge, kOpenSide, kOpenSide},
     kReceiverDepth, 0.5f},
};

bool CreateWorldShadowMap(IDirect3DDevice9* device, IDirect3DTexture9** map)
{
    if (FAILED(device->CreateTexture(4, 4, 1, 0, D3DFMT_R32F, D3DPOOL_MANAGED, map, nullptr)))
        return false;
    D3DLOCKED_RECT locked = {};
    if (FAILED((*map)->LockRect(0, &locked, nullptr, 0)))
        return false;
    for (int y = 0; y < 4; ++y)
    {
        float* row = reinterpret_cast<float*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < 4; ++x)
            row[x] = x < 2 ? kNearCasterDepth : kFarCasterDepth;
    }
    return SUCCEEDED((*map)->UnlockRect(0));
}

void BindWorldShadowMap(IDirect3DDevice9* device, IDirect3DTexture9* map)
{
    for (DWORD stage = kFirstWorldShadowStage; stage < kFirstWorldShadowStage + kWorldShadowMaps; ++stage)
    {
        device->SetTexture(stage, map);
        device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
        device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
}

void CheckWorldShadowMaps(IDirect3DDevice9* device, const FogIntegrationResources& resources, int quality,
                          IDirect3DTexture9* depth)
{
    IDirect3DTexture9* map = nullptr;
    const bool ready = FillViewDepth(depth, true, false) && CreateWorldShadowMap(device, &map);
    Check(ready, "world shadow map fixture created");
    if (!ready)
    {
        if (map)
            map->Release();
        return;
    }
    BindWorldShadowMap(device, map);
    for (const WorldShadowCase& sample : kWorldShadowCases)
    {
        FixtureConstants constants;
        FillSunShadowFixture(constants, true);
        constants[12][1] = 0.001f;
        constants[13][0] = constants[13][1] = constants[13][2] = 0;
        constants[14][0] = constants[14][1] = constants[14][2] = 1;
        constants[kSunShadowRegister][2] = sample.environment ? 1.0f : 0.0f;
        constants[kSunShadowRegister][3] = 1;
        for (int map = 0; map < kWorldShadowMaps; ++map)
        {
            constants[kViewToShadowRegister + map * 3][3] = sample.mapX[map];
            constants[kViewToShadowRegister + map * 3 + 2][3] = sample.receiverDepth;
        }
        HRESULT draw = E_FAIL;
        const float actual = DrawFixture(device, resources, constants, draw);
        const float expected = sample.visibility * (1.0f - std::exp(-1.0f));
        char label[200];
        std::snprintf(label, sizeof(label), "world shadow maps quality %d: %s (error %.3f)", quality, sample.name,
                      std::fabs(actual - expected));
        Check(SUCCEEDED(draw) && std::fabs(actual - expected) <= 2.0f / 255, label);
    }
    for (DWORD stage = kFirstWorldShadowStage; stage < kFirstWorldShadowStage + kWorldShadowMaps; ++stage)
        device->SetTexture(stage, nullptr);
    map->Release();
}

bool FillTerrainShadowHeights(IDirect3DTexture9* map, bool rowsAboveRays, bool colsAboveRays)
{
    D3DLOCKED_RECT locked = {};
    if (FAILED(map->LockRect(0, &locked, nullptr, 0)))
        return false;
    const int half = kTerrainWindowVertices / 2;
    for (int row = 0; row < kTerrainWindowVertices; ++row)
    {
        float* line = reinterpret_cast<float*>(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch);
        for (int col = 0; col < kTerrainWindowVertices; ++col)
            line[col] = (rowsAboveRays && row < half) || (colsAboveRays && col < half) ? kTerrainAboveEveryRay
                                                                                     : kNoTerrainHeight;
    }
    return SUCCEEDED(map->UnlockRect(0));
}

struct TerrainShadowCase
{
    const char* name;
    bool enabled;
    bool rowsAboveRays;
    bool colsAboveRays;
    float firstRowFromCamera;
    float firstColFromCamera;
    float visibility;
};

const TerrainShadowCase kTerrainShadowCases[] = {
    {"terrain whose shadow rises above the rays blocks the light", true, true, true, kNearWindowStart,
     kNearWindowStart, 0},
    {"terrain below the rays leaves the light", true, false, false, kNearWindowStart, kNearWindowStart, 1},
    {"disabled terrain shadows leave the light", false, true, true, kNearWindowStart, kNearWindowStart, 1},
    {"the window's rows follow world -X", true, true, false, kNearWindowStart, kNearWindowStart, 0},
    {"a row beyond the shadowed half is lit", true, true, false, kFarWindowStart, kNearWindowStart, 1},
    {"the window's columns follow world -Y", true, false, true, kNearWindowStart, kNearWindowStart, 0},
    {"a column beyond the shadowed half is lit", true, false, true, kNearWindowStart, kFarWindowStart, 1},
    {"rays outside the window keep the light", true, true, true, -kFarWindowStart, kNearWindowStart, 1},
};

void CheckTerrainShadows(IDirect3DDevice9* device, const FogIntegrationResources& resources, int quality,
                         IDirect3DTexture9* depth)
{
    IDirect3DTexture9* map = nullptr;
    const bool ready = FillViewDepth(depth, true, false) &&
                       SUCCEEDED(device->CreateTexture(kTerrainWindowVertices, kTerrainWindowVertices, 1, 0,
                                                       D3DFMT_R32F, D3DPOOL_MANAGED, &map, nullptr));
    Check(ready, "terrain shadow height fixture created");
    if (!ready)
    {
        if (map)
            map->Release();
        return;
    }
    device->SetTexture(kTerrainShadowStage, map);
    device->SetSamplerState(kTerrainShadowStage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    device->SetSamplerState(kTerrainShadowStage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    device->SetSamplerState(kTerrainShadowStage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(kTerrainShadowStage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(kTerrainShadowStage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    for (const TerrainShadowCase& sample : kTerrainShadowCases)
    {
        FixtureConstants constants;
        FillSunShadowFixture(constants, true);
        constants[12][1] = 0.001f;
        constants[13][0] = constants[13][1] = constants[13][2] = 0;
        constants[14][0] = constants[14][1] = constants[14][2] = 1;
        constants[kTerrainShadowRegister][0] = kCameraMapVertex - sample.firstRowFromCamera;
        constants[kTerrainShadowRegister][1] = kCameraMapVertex - sample.firstColFromCamera;
        constants[kTerrainShadowRegister][2] = sample.enabled ? 1.0f : 0.0f;
        HRESULT draw = E_FAIL;
        const bool filled = FillTerrainShadowHeights(map, sample.rowsAboveRays, sample.colsAboveRays);
        const float actual = filled ? DrawFixture(device, resources, constants, draw) : -1.0f;
        const float expected = sample.visibility * (1.0f - std::exp(-1.0f));
        char label[200];
        std::snprintf(label, sizeof(label), "terrain shadows quality %d: %s (error %.3f)", quality, sample.name,
                      std::fabs(actual - expected));
        Check(SUCCEEDED(draw) && std::fabs(actual - expected) <= 2.0f / 255, label);
    }
    device->SetTexture(kTerrainShadowStage, nullptr);
    map->Release();
}

void CheckShadowedMarch(IDirect3DDevice9* device, const FogIntegrationResources& resources, int quality,
                        IDirect3DPixelShader9* shader)
{
    IDirect3DTexture9* depth = nullptr;
    const bool ready = SUCCEEDED(device->CreateTexture(kDepthSize, kDepthSize, 1, 0, D3DFMT_R32F, D3DPOOL_MANAGED,
                                                       &depth, nullptr));
    Check(ready, "sun shadow view depth fixture created");
    if (!ready)
        return;
    device->SetPixelShader(shader);
    device->SetTexture(0, depth);
    CheckSunBlockedBehindWall(device, resources, quality, depth);
    CheckWorldShadowMaps(device, resources, quality, depth);
    CheckTerrainShadows(device, resources, quality, depth);
    device->SetTexture(0, resources.depth);
    depth->Release();
}
}
