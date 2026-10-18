#pragma once

#include "texture_filtering.h"

namespace texture_filtering_checks
{
constexpr UINT kMipmappedEdge = 256;
constexpr UINT kAllLevels = 0;
constexpr int kLevelGreyStep = 28;
constexpr int kTopLevelGrey = 255;
constexpr DWORD kGroundFvf = D3DFVF_XYZ | D3DFVF_TEX1;
constexpr float kGroundHalfWidth = 50.0f;
constexpr float kGroundNear = 1.0f;
constexpr float kGroundFar = 400.0f;
constexpr float kGroundRepeatsAcross = 50.0f;
constexpr float kGroundRepeatsAlong = 400.0f;
constexpr float kEyeHeight = 2.0f;
constexpr float kGroundFovY = 0.9f;
constexpr float kGroundZNear = 0.5f;
constexpr float kGroundZFar = 1000.0f;
constexpr UINT kFarBandTop = 364;
constexpr UINT kFarBandBottom = 420;
constexpr UINT kFarBandLeft = 400;
constexpr UINT kFarBandRight = 880;
constexpr float kSharperFarGroundAtLeast = 0.05f;
constexpr int kForcedSixteen = 16;
constexpr unsigned kDeviceSixteen = 16;
constexpr unsigned kDeviceEight = 8;
constexpr DWORD kRequestedEight = 8;

struct GroundVertex
{
    float x, y, z;
    float u, v;
};

bool SameFilter(SamplerFilter a, SamplerFilter b)
{
    return a.minFilter == b.minFilter && a.maxAnisotropy == b.maxAnisotropy;
}

void CheckForcedSamplerFilter()
{
    Check(AnisotropyLevel(kAnisotropyOff) == 1 && AnisotropyLevel(1) == 1 && AnisotropyLevel(5) == 4 &&
              AnisotropyLevel(kForcedSixteen) == 16 && AnisotropyLevel(64) == 16,
          "AnisotropicFiltering is taken as the power of two at or below it, 16 at most, and 0 or 1 is off");

    const SamplerFilter linear = {D3DTEXF_LINEAR, 1};
    const SamplerFilter point = {D3DTEXF_POINT, 1};
    const SamplerFilter clientEight = {D3DTEXF_ANISOTROPIC, kRequestedEight};
    Check(SameFilter(ForcedSamplerFilter(linear, true, 16, kDeviceSixteen), {D3DTEXF_ANISOTROPIC, 16}),
          "a linear sampler on a mipmapped texture is filtered anisotropically at the forced level");
    Check(SameFilter(ForcedSamplerFilter(linear, false, 16, kDeviceSixteen), linear),
          "a texture with one level (render targets, shadow maps, the interface's copies) keeps the game's filter");
    Check(SameFilter(ForcedSamplerFilter(point, true, 16, kDeviceSixteen), point),
          "a point-filtered sampler stays point-filtered");
    Check(SameFilter(ForcedSamplerFilter(clientEight, true, 4, kDeviceSixteen), clientEight),
          "the game's own higher anisotropy is kept when the forced level is lower");
    Check(SameFilter(ForcedSamplerFilter(linear, true, 16, kDeviceEight), {D3DTEXF_ANISOTROPIC, 8}),
          "the forced level is limited to what the device supports");
    Check(SameFilter(ForcedSamplerFilter(linear, true, 1, kDeviceSixteen), linear) &&
              SameFilter(ForcedSamplerFilter(linear, true, 16, 1), linear),
          "with AnisotropicFiltering off, or a device without anisotropic filtering, the game's filter stays");
}

IDirect3DTexture9* CreateLevelCodedTexture(IDirect3DDevice9* dev, UINT levels)
{
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(dev->CreateTexture(kMipmappedEdge, kMipmappedEdge, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                  &texture, nullptr)))
        return nullptr;
    for (DWORD level = 0; level < texture->GetLevelCount(); ++level)
    {
        D3DSURFACE_DESC desc = {};
        D3DLOCKED_RECT locked = {};
        texture->GetLevelDesc(level, &desc);
        if (FAILED(texture->LockRect(level, &locked, nullptr, 0)))
            continue;
        const int grey = std::max(kTopLevelGrey - kLevelGreyStep * static_cast<int>(level), 0);
        const uint32_t texel = 0xFF000000u | (grey << 16) | (grey << 8) | grey;
        for (UINT y = 0; y < desc.Height; ++y)
        {
            auto* row = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
            std::fill(row, row + desc.Width, texel);
        }
        texture->UnlockRect(level);
    }
    return texture;
}

void GroundLookAt(float* m)
{
    const Vec3 eye = {0, 0, kEyeHeight};
    const Vec3 z = {0, 1, 0};
    const Vec3 x = Norm(Cross(kWorldUp, z));
    const Vec3 y = Cross(z, x);
    const float view[16] = {x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0,
                            -Dot(x, eye), -Dot(y, eye), -Dot(z, eye), 1};
    std::memcpy(m, view, sizeof(view));
}

void GroundProjection(float aspect, float* m)
{
    const float yScale = 1.0f / std::tan(kGroundFovY * 0.5f);
    const float depthScale = kGroundZFar / (kGroundZFar - kGroundZNear);
    const float proj[16] = {yScale / aspect, 0, 0, 0, 0, yScale, 0, 0, 0, 0, depthScale, 1,
                            0, 0, -kGroundZNear * depthScale, 0};
    std::memcpy(m, proj, sizeof(proj));
}

void DrawGround(IDirect3DDevice9* dev, IDirect3DTexture9* texture)
{
    D3DSURFACE_DESC back = {};
    IDirect3DSurface9* target = nullptr;
    dev->GetRenderTarget(0, &target);
    target->GetDesc(&back);
    target->Release();
    const D3DVIEWPORT9 viewport = {0, 0, back.Width, back.Height, 0.0f, 1.0f};
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float view[16];
    float proj[16];
    GroundLookAt(view);
    GroundProjection(static_cast<float>(back.Width) / back.Height, proj);
    const GroundVertex ground[6] = {
        {-kGroundHalfWidth, kGroundNear, 0, 0, 0},
        {kGroundHalfWidth, kGroundNear, 0, kGroundRepeatsAcross, 0},
        {kGroundHalfWidth, kGroundFar, 0, kGroundRepeatsAcross, kGroundRepeatsAlong},
        {-kGroundHalfWidth, kGroundNear, 0, 0, 0},
        {kGroundHalfWidth, kGroundFar, 0, kGroundRepeatsAcross, kGroundRepeatsAlong},
        {-kGroundHalfWidth, kGroundFar, 0, 0, kGroundRepeatsAlong},
    };
    dev->SetViewport(&viewport);
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(kGroundFvf);
    dev->SetTransform(D3DTS_WORLD, reinterpret_cast<D3DMATRIX*>(identity));
    dev->SetTransform(D3DTS_VIEW, reinterpret_cast<D3DMATRIX*>(view));
    dev->SetTransform(D3DTS_PROJECTION, reinterpret_cast<D3DMATRIX*>(proj));
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 1);
    dev->SetTexture(0, texture);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
    dev->BeginScene();
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, ground, sizeof(GroundVertex));
    dev->EndScene();
}

float FarGroundLuma(const Image& image)
{
    float sum = 0.0f;
    int n = 0;
    for (UINT y = kFarBandTop; y < kFarBandBottom; ++y)
        for (UINT x = kFarBandLeft; x < kFarBandRight; ++x)
        {
            sum += image.Luma(x, y);
            ++n;
        }
    return n ? sum / n : 0.0f;
}

struct GroundDraw
{
    float farLuma = 0.0f;
    DWORD minFilter = 0;
    DWORD maxAnisotropy = 0;
};

GroundDraw DrawGroundWith(IDirect3DDevice9* dev, IDirect3DTexture9* texture, int anisotropicFiltering)
{
    Config cfg = {};
    vf_test_get_config(&cfg);
    Config forced = cfg;
    forced.anisotropicFiltering = anisotropicFiltering;
    vf_test_set_config(&forced);
    DrawGround(dev, texture);
    GroundDraw draw;
    draw.farLuma = FarGroundLuma(Capture(dev));
    dev->GetSamplerState(0, D3DSAMP_MINFILTER, &draw.minFilter);
    dev->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &draw.maxAnisotropy);
    vf_test_set_config(&cfg);
    return draw;
}

void CheckForcedAnisotropyOnTheDevice(IDirect3DDevice9* dev)
{
    D3DCAPS9 caps = {};
    dev->GetDeviceCaps(&caps);
    const unsigned deviceMax = DeviceMaxAnisotropy(caps);
    if (deviceMax <= 1)
    {
        std::printf("     the device has no anisotropic filtering; the forced filtering checks were not run\n");
        return;
    }
    IDirect3DStateBlock9* saved = nullptr;
    dev->CreateStateBlock(D3DSBT_ALL, &saved);
    IDirect3DTexture9* mipmapped = CreateLevelCodedTexture(dev, kAllLevels);
    IDirect3DTexture9* single = CreateLevelCodedTexture(dev, 1);
    if (!mipmapped || !single)
    {
        Check(false, "the level-coded ground textures could not be created");
        return;
    }
    const GroundDraw gameFilter = DrawGroundWith(dev, mipmapped, kAnisotropyOff);
    const GroundDraw forced = DrawGroundWith(dev, mipmapped, kForcedSixteen);
    const GroundDraw singleLevel = DrawGroundWith(dev, single, kForcedSixteen);
    std::printf("     far ground through the wrapper: game's filter %.3f (min filter %lu), forced 16x %.3f (min filter "
                "%lu, %lux of %ux), one-level texture min filter %lu\n",
                gameFilter.farLuma, gameFilter.minFilter, forced.farLuma, forced.minFilter, forced.maxAnisotropy,
                deviceMax, singleLevel.minFilter);
    Check(gameFilter.minFilter == D3DTEXF_LINEAR && forced.minFilter == D3DTEXF_ANISOTROPIC &&
              forced.maxAnisotropy == std::min<unsigned>(kForcedSixteen, deviceMax),
          "with AnisotropicFiltering=16 the game's linear filter on a mipmapped texture reaches the device as "
          "anisotropic at the device's limit; with 0 it stays linear");
    Check(forced.farLuma > gameFilter.farLuma + kSharperFarGroundAtLeast,
          "forced anisotropic filtering samples finer levels of a mipmapped ground texture at a grazing angle");
    Check(singleLevel.minFilter == D3DTEXF_LINEAR,
          "a texture with one level keeps the game's linear filter while anisotropic filtering is forced");

    dev->SetTexture(0, nullptr);
    mipmapped->Release();
    single->Release();
    if (saved)
    {
        saved->Apply();
        saved->Release();
    }
    DWORD restoredMin = 0;
    DWORD restoredAnisotropy = 0;
    IDirect3DBaseTexture9* restoredTexture = nullptr;
    dev->GetSamplerState(0, D3DSAMP_MINFILTER, &restoredMin);
    dev->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &restoredAnisotropy);
    dev->GetTexture(0, &restoredTexture);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, restoredMin);
    dev->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, restoredAnisotropy);
    dev->SetTexture(0, restoredTexture);
    if (restoredTexture)
        restoredTexture->Release();
}

void CheckShippedSetting(const std::wstring& shippedIni)
{
    Config defaults = {};
    Check(defaults.anisotropicFiltering == kForcedSixteen &&
              GetPrivateProfileIntW(L"CoAVolFog", L"AnisotropicFiltering", -1, shippedIni.c_str()) == kForcedSixteen,
          "anisotropic filtering is forced at 16x by default and in the shipped INI");
}
}
