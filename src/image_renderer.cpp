#include "image_renderer.h"

#include "fullscreen_triangle.h"
#include "log.h"

#include "AreaTex.h"
#include "SearchTex.h"
#include "ps_bloom_composite.h"
#include "ps_bloom_down.h"
#include "ps_bloom_prefilter.h"
#include "ps_bloom_up.h"
#include "ps_sharpen.h"
#include "ps_smaa_blend.h"
#include "ps_smaa_edges.h"
#include "ps_smaa_weights.h"
#include "vs_fullscreen.h"

#include <cstring>

namespace
{
constexpr UINT kImageConstants = 4;
constexpr DWORD kImageStages = 3;
constexpr DWORD kSceneStage = 0;
constexpr DWORD kEdgesStage = 0;
constexpr DWORD kAreaStage = 1;
constexpr DWORD kSearchStage = 2;
constexpr DWORD kWeightsStage = 1;
constexpr DWORD kBloomStage = 0;
constexpr UINT kOriginRegister = 0;
constexpr UINT kMetricsRegister = 1;
constexpr UINT kSettingsRegister = 2;
constexpr UINT kBloomTexelsRegister = 3;
constexpr D3DFORMAT kAntialiasingTargetFormat = D3DFMT_A8R8G8B8;
constexpr D3DFORMAT kFloatBloomFormat = D3DFMT_A16B16G16R16F;
constexpr D3DFORMAT kFixedBloomFormat = D3DFMT_A8R8G8B8;
constexpr DWORD kFloatBloomUsage =
    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING | D3DUSAGE_QUERY_FILTER;
constexpr D3DCOLOR kNoEdges = 0;
constexpr DWORD kColourChannels = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
constexpr DWORD kAllChannels = kColourChannels | D3DCOLORWRITEENABLE_ALPHA;
constexpr DWORD kEverySample = 0xFFFFFFFF;

struct RenderStateValue
{
    D3DRENDERSTATETYPE state;
    DWORD value;
};

const RenderStateValue kImageRenderStates[] = {
    {D3DRS_ZENABLE, D3DZB_FALSE},
    {D3DRS_ZWRITEENABLE, FALSE},
    {D3DRS_STENCILENABLE, FALSE},
    {D3DRS_TWOSIDEDSTENCILMODE, FALSE},
    {D3DRS_ALPHATESTENABLE, FALSE},
    {D3DRS_ALPHABLENDENABLE, FALSE},
    {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
    {D3DRS_SRCBLEND, D3DBLEND_ONE},
    {D3DRS_DESTBLEND, D3DBLEND_ZERO},
    {D3DRS_BLENDOP, D3DBLENDOP_ADD},
    {D3DRS_FOGENABLE, FALSE},
    {D3DRS_SRGBWRITEENABLE, FALSE},
    {D3DRS_CULLMODE, D3DCULL_NONE},
    {D3DRS_FILLMODE, D3DFILL_SOLID},
    {D3DRS_CLIPPLANEENABLE, 0},
    {D3DRS_SCISSORTESTENABLE, TRUE},
    {D3DRS_COLORWRITEENABLE, kColourChannels},
    {D3DRS_MULTISAMPLEMASK, kEverySample},
};

const D3DSAMPLERSTATETYPE kRecordedSamplerStates[] = {
    D3DSAMP_ADDRESSU,  D3DSAMP_ADDRESSV,    D3DSAMP_MAGFILTER,   D3DSAMP_MINFILTER,
    D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL,
};

enum class Filter
{
    Point,
    Linear,
};

template <typename T>
void SafeRelease(T*& p)
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}

RECT ViewportRect(const D3DVIEWPORT9& vp)
{
    return {static_cast<LONG>(vp.X), static_cast<LONG>(vp.Y), static_cast<LONG>(vp.X + vp.Width),
            static_cast<LONG>(vp.Y + vp.Height)};
}

bool InsideTarget(const D3DVIEWPORT9& world, const D3DSURFACE_DESC& desc)
{
    return world.Width > 0 && world.Height > 0 && world.X + world.Width <= desc.Width &&
           world.Y + world.Height <= desc.Height;
}

void Bind(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* texture, Filter filter, bool srgb = false)
{
    const DWORD textureFilter = filter == Filter::Linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    dev->SetTexture(stage, texture);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, textureFilter);
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, textureFilter);
    dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, srgb ? TRUE : FALSE);
    dev->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
}

void SetOrigin(IDirect3DDevice9* dev, const D3DVIEWPORT9& viewport)
{
    const float origin[4] = {static_cast<float>(viewport.X), static_cast<float>(viewport.Y), 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(kOriginRegister, origin, 1);
}

struct FormatUse
{
    DWORD usage;
    D3DRESOURCETYPE type;
};

template <size_t Count>
bool SupportsFormat(IDirect3DDevice9* dev, D3DFORMAT format, const FormatUse (&uses)[Count])
{
    IDirect3D9* d3d = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation = {};
    D3DDISPLAYMODE mode = {};
    if (FAILED(dev->GetDirect3D(&d3d)) || !d3d)
        return false;
    bool supported = SUCCEEDED(dev->GetCreationParameters(&creation)) && SUCCEEDED(dev->GetDisplayMode(0, &mode));
    for (const FormatUse& use : uses)
        supported = supported && SUCCEEDED(d3d->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
                                                                  mode.Format, use.usage, use.type, format));
    d3d->Release();
    return supported;
}

bool SupportsLinearBlend(IDirect3DDevice9* dev, D3DFORMAT format)
{
    const FormatUse uses[] = {
        {D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE},
        {D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBWRITE, D3DRTYPE_SURFACE},
    };
    return SupportsFormat(dev, format, uses);
}

D3DFORMAT BloomFormat(IDirect3DDevice9* dev)
{
    const FormatUse uses[] = {{kFloatBloomUsage, D3DRTYPE_TEXTURE}};
    return SupportsFormat(dev, kFloatBloomFormat, uses) ? kFloatBloomFormat : kFixedBloomFormat;
}

UINT BloomExtent(UINT worldExtent, int level)
{
    const UINT extent = worldExtent >> (level + 1);
    return extent ? extent : 1;
}

template <size_t Size>
bool Upload(IDirect3DTexture9* texture, const unsigned char (&bytes)[Size], UINT pitch, UINT height)
{
    D3DLOCKED_RECT locked = {};
    if (Size < static_cast<size_t>(pitch) * height || FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        return false;
    for (UINT row = 0; row < height; ++row)
        std::memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch, bytes + row * pitch, pitch);
    texture->UnlockRect(0);
    return true;
}
}

ImageRenderer::~ImageRenderer()
{
    ReleaseAll();
}

void ImageRenderer::ReleaseViewTexture(ViewTexture& view)
{
    SafeRelease(view.texture);
    view = ViewTexture();
}

void ImageRenderer::ReleaseDefaultPool()
{
    ReleaseViewTexture(m_sceneCopy);
    ReleaseViewTexture(m_edges);
    ReleaseViewTexture(m_weights);
    ReleaseViewTexture(m_smoothed);
    ReleaseBloomTargets();
    m_targetsFailed = false;
    SafeRelease(m_state);
}

void ImageRenderer::ReleaseAll()
{
    ReleaseDefaultPool();
    SafeRelease(m_area);
    SafeRelease(m_search);
    m_lookupFailed = false;
    SafeRelease(m_vs);
    SafeRelease(m_sharpen);
    SafeRelease(m_edgesPs);
    SafeRelease(m_weightsPs);
    SafeRelease(m_blendPs);
    SafeRelease(m_bloomPrefilterPs);
    SafeRelease(m_bloomDownPs);
    SafeRelease(m_bloomUpPs);
    SafeRelease(m_bloomCompositePs);
    SafeRelease(m_decl);
    m_bloomFormat = D3DFMT_UNKNOWN;
    m_unsupportedDevice = nullptr;
}

void ImageRenderer::ReleaseBloomTargets()
{
    for (ViewTexture& level : m_bloom)
        ReleaseViewTexture(level);
}

ImageStats ImageRenderer::Stats() const
{
    ImageStats stats;
    stats.smoothed = m_smoothedCount;
    stats.sharpened = m_sharpenedCount;
    stats.bloomed = m_bloomedCount;
    stats.holdsSceneCopy = m_sceneCopy.texture != nullptr;
    stats.holdsAntialiasingTargets = m_edges.texture || m_weights.texture || m_smoothed.texture;
    stats.holdsBloomTargets = m_bloom[0].texture != nullptr;
    stats.floatBloom = m_bloomFormat == kFloatBloomFormat;
    stats.linearBlend = m_linearBlend;
    return stats;
}

bool ImageRenderer::Skip(const char* reason)
{
    m_skip = reason;
    return false;
}

bool ImageRenderer::EnsureShaders(IDirect3DDevice9* dev)
{
    if (m_unsupportedDevice == dev)
        return Skip("image shaders unsupported");
    if (m_bloomCompositePs)
        return true;
    static const D3DVERTEXELEMENT9 kElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END(),
    };
    HRESULT result = dev->CreateVertexShader(reinterpret_cast<const DWORD*>(g_vs_fullscreen), &m_vs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_sharpen), &m_sharpen);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_smaa_edges), &m_edgesPs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_smaa_weights), &m_weightsPs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_smaa_blend), &m_blendPs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_bloom_prefilter), &m_bloomPrefilterPs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_bloom_down), &m_bloomDownPs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_bloom_up), &m_bloomUpPs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_bloom_composite), &m_bloomCompositePs);
    if (SUCCEEDED(result))
        result = dev->CreateVertexDeclaration(kElements, &m_decl);
    if (SUCCEEDED(result))
        return true;
    SafeRelease(m_vs);
    SafeRelease(m_sharpen);
    SafeRelease(m_edgesPs);
    SafeRelease(m_weightsPs);
    SafeRelease(m_blendPs);
    SafeRelease(m_bloomPrefilterPs);
    SafeRelease(m_bloomDownPs);
    SafeRelease(m_bloomUpPs);
    SafeRelease(m_bloomCompositePs);
    SafeRelease(m_decl);
    m_unsupportedDevice = dev;
    VF_LOG_ERROR("image shaders could not be created (HRESULT 0x%08lX); no antialiasing, sharpening or bloom on this "
                 "device",
                 static_cast<unsigned long>(result));
    return Skip("image shaders unsupported");
}

bool ImageRenderer::EnsureStateBlock(IDirect3DDevice9* dev)
{
    if (m_state)
        return true;
    if (FAILED(dev->BeginStateBlock()))
        return Skip("state block recording failed");
    for (const RenderStateValue& renderState : kImageRenderStates)
        dev->SetRenderState(renderState.state, 0);
    for (DWORD stage = 0; stage < kImageStages; ++stage)
    {
        dev->SetTexture(stage, nullptr);
        for (D3DSAMPLERSTATETYPE sampler : kRecordedSamplerStates)
            dev->SetSamplerState(stage, sampler, 0);
    }
    const float zeros[kImageConstants * 4] = {};
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetPixelShaderConstantF(0, zeros, kImageConstants);
    dev->SetVertexDeclaration(m_decl);
    dev->SetStreamSource(0, nullptr, 0, 0);
    D3DVIEWPORT9 viewport = {0, 0, 1, 1, 0.0f, 1.0f};
    dev->SetViewport(&viewport);
    RECT scissor = {0, 0, 1, 1};
    dev->SetScissorRect(&scissor);
    if (FAILED(dev->EndStateBlock(&m_state)) || !m_state)
    {
        m_state = nullptr;
        return Skip("state block recording failed");
    }
    return true;
}

bool ImageRenderer::EnsureLookupTextures(IDirect3DDevice9* dev)
{
    if (m_area && m_search)
        return true;
    if (m_lookupFailed)
        return Skip("antialiasing lookup textures unavailable");
    const bool created =
        SUCCEEDED(dev->CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 1, 0, D3DFMT_A8L8, D3DPOOL_MANAGED, &m_area,
                                     nullptr)) &&
        SUCCEEDED(dev->CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &m_search,
                                     nullptr)) &&
        Upload(m_area, areaTexBytes, AREATEX_PITCH, AREATEX_HEIGHT) &&
        Upload(m_search, searchTexBytes, SEARCHTEX_PITCH, SEARCHTEX_HEIGHT);
    if (created)
        return true;
    SafeRelease(m_area);
    SafeRelease(m_search);
    m_lookupFailed = true;
    VF_LOG_ERROR("SMAA area and search textures could not be created; no antialiasing on this device");
    return Skip("antialiasing lookup textures unavailable");
}

bool ImageRenderer::EnsureViewTexture(IDirect3DDevice9* dev, ViewTexture& view, D3DFORMAT format, UINT width,
                                      UINT height)
{
    if (view.texture && view.width == width && view.height == height && view.format == format)
        return true;
    ReleaseViewTexture(view);
    if (FAILED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &view.texture,
                                  nullptr)) ||
        !view.texture)
    {
        view.texture = nullptr;
        return false;
    }
    view.width = width;
    view.height = height;
    view.format = format;
    return true;
}

bool ImageRenderer::EnsureBloomTargets(IDirect3DDevice9* dev, const D3DVIEWPORT9& world, bool wanted)
{
    if (!wanted)
    {
        ReleaseBloomTargets();
        return true;
    }
    if (m_bloomFormat == D3DFMT_UNKNOWN)
        m_bloomFormat = BloomFormat(dev);
    bool created = true;
    for (int level = 0; level < kBloomLevels; ++level)
        created = created && EnsureViewTexture(dev, m_bloom[level], m_bloomFormat, BloomExtent(world.Width, level),
                                               BloomExtent(world.Height, level));
    return created;
}

bool ImageRenderer::EnsureTargets(IDirect3DDevice9* dev, D3DFORMAT format, const D3DVIEWPORT9& world,
                                  const ImageSettings& settings)
{
    if (m_targetsFailed)
        return Skip("image textures could not be created");
    const bool sceneFormatChanged = m_sceneCopy.format != format;
    bool created = EnsureViewTexture(dev, m_sceneCopy, format, world.Width, world.Height);
    if (settings.antialiasing)
    {
        created = created &&
                  EnsureViewTexture(dev, m_edges, kAntialiasingTargetFormat, world.Width, world.Height) &&
                  EnsureViewTexture(dev, m_weights, kAntialiasingTargetFormat, world.Width, world.Height);
        if (settings.sharpening > 0.0f)
            created = created && EnsureViewTexture(dev, m_smoothed, format, world.Width, world.Height);
        else
            ReleaseViewTexture(m_smoothed);
    }
    else
    {
        ReleaseViewTexture(m_edges);
        ReleaseViewTexture(m_weights);
        ReleaseViewTexture(m_smoothed);
    }
    created = created && EnsureBloomTargets(dev, world, settings.bloom > 0.0f);
    if (!created)
    {
        ReleaseDefaultPool();
        m_targetsFailed = true;
        VF_LOG_ERROR("image textures could not be created (%ux%u)", world.Width, world.Height);
        return Skip("image textures could not be created");
    }
    if (sceneFormatChanged)
        m_linearBlend = SupportsLinearBlend(dev, format);
    return true;
}

bool ImageRenderer::CopyWorld(IDirect3DDevice9* dev, IDirect3DSurface9* target, const D3DVIEWPORT9& world)
{
    const RECT rect = ViewportRect(world);
    IDirect3DSurface9* copy = nullptr;
    const bool copied = SUCCEEDED(m_sceneCopy.texture->GetSurfaceLevel(0, &copy)) &&
                        SUCCEEDED(dev->StretchRect(target, &rect, copy, nullptr, D3DTEXF_POINT));
    SafeRelease(copy);
    return copied || Skip("scene copy failed");
}

void ImageRenderer::SetSharedState(IDirect3DDevice9* dev, const D3DVIEWPORT9& world)
{
    for (const RenderStateValue& renderState : kImageRenderStates)
        dev->SetRenderState(renderState.state, renderState.value);
    dev->SetVertexShader(m_vs);
    dev->SetVertexDeclaration(m_decl);
    const float metrics[4] = {1.0f / world.Width, 1.0f / world.Height, static_cast<float>(world.Width),
                              static_cast<float>(world.Height)};
    dev->SetPixelShaderConstantF(kMetricsRegister, metrics, 1);
}

void ImageRenderer::Target(IDirect3DDevice9* dev, IDirect3DSurface9* surface, const D3DVIEWPORT9& viewport)
{
    const RECT scissor = ViewportRect(viewport);
    dev->SetRenderTarget(0, surface);
    dev->SetViewport(&viewport);
    dev->SetScissorRect(&scissor);
    SetOrigin(dev, viewport);
}

void ImageRenderer::TargetTexture(IDirect3DDevice9* dev, const ViewTexture& view, bool cleared)
{
    IDirect3DSurface9* surface = nullptr;
    view.texture->GetSurfaceLevel(0, &surface);
    const D3DVIEWPORT9 local = {0, 0, view.width, view.height, 0.0f, 1.0f};
    Target(dev, surface, local);
    SafeRelease(surface);
    if (!cleared)
        return;
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kAllChannels);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, kNoEdges, 1.0f, 0);
}

IDirect3DBaseTexture9* ImageRenderer::Smooth(IDirect3DDevice9* dev, const BoundTargets& bound,
                                             const D3DVIEWPORT9& world, bool intoTexture)
{
    TargetTexture(dev, m_edges, true);
    dev->SetPixelShader(m_edgesPs);
    Bind(dev, kSceneStage, m_sceneCopy.texture, Filter::Point);
    DrawFullscreenTriangle(dev);

    TargetTexture(dev, m_weights, true);
    dev->SetPixelShader(m_weightsPs);
    Bind(dev, kEdgesStage, m_edges.texture, Filter::Linear);
    Bind(dev, kAreaStage, m_area, Filter::Linear);
    Bind(dev, kSearchStage, m_search, Filter::Point);
    DrawFullscreenTriangle(dev);
    dev->SetTexture(kSearchStage, nullptr);

    if (intoTexture)
        TargetTexture(dev, m_smoothed, false);
    else
        Target(dev, bound.colour[0], world);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColourChannels);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, m_linearBlend ? TRUE : FALSE);
    dev->SetPixelShader(m_blendPs);
    Bind(dev, kSceneStage, m_sceneCopy.texture, Filter::Linear, m_linearBlend);
    Bind(dev, kWeightsStage, m_weights.texture, Filter::Linear);
    DrawFullscreenTriangle(dev);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetTexture(kWeightsStage, nullptr);
    ++m_smoothedCount;
    return intoTexture ? m_smoothed.texture : nullptr;
}

void ImageRenderer::Sharpen(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                            IDirect3DBaseTexture9* source, float strength)
{
    Target(dev, bound.colour[0], world);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColourChannels);
    dev->SetPixelShader(m_sharpen);
    Bind(dev, kSceneStage, source, Filter::Point);
    const float settings[4] = {strength, 0.0f, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(kSettingsRegister, settings, 1);
    DrawFullscreenTriangle(dev);
    ++m_sharpenedCount;
}

void ImageRenderer::DrawBloomPass(IDirect3DDevice9* dev, IDirect3DPixelShader9* shader, const ViewTexture& source,
                                  const ViewTexture& target)
{
    TargetTexture(dev, target, false);
    dev->SetPixelShader(shader);
    Bind(dev, kBloomStage, source.texture, Filter::Linear);
    const float texels[4] = {1.0f / source.width, 1.0f / source.height, 1.0f / target.width, 1.0f / target.height};
    dev->SetPixelShaderConstantF(kBloomTexelsRegister, texels, 1);
    DrawFullscreenTriangle(dev);
}

void ImageRenderer::BuildBloom(IDirect3DDevice9* dev)
{
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kAllChannels);
    DrawBloomPass(dev, m_bloomPrefilterPs, m_sceneCopy, m_bloom[0]);
    for (int level = 1; level < kBloomLevels; ++level)
        DrawBloomPass(dev, m_bloomDownPs, m_bloom[level - 1], m_bloom[level]);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    for (int level = kBloomLevels - 1; level > 0; --level)
        DrawBloomPass(dev, m_bloomUpPs, m_bloom[level], m_bloom[level - 1]);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
    dev->SetTexture(kBloomStage, nullptr);
}

void ImageRenderer::CompositeBloom(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                                   float strength)
{
    Target(dev, bound.colour[0], world);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColourChannels);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_INVDESTCOLOR);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetPixelShader(m_bloomCompositePs);
    Bind(dev, kBloomStage, m_bloom[0].texture, Filter::Linear);
    const float texels[4] = {1.0f / m_bloom[0].width, 1.0f / m_bloom[0].height, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(kBloomTexelsRegister, texels, 1);
    const float settings[4] = {0.0f, strength / kBloomLevels, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(kSettingsRegister, settings, 1);
    DrawFullscreenTriangle(dev);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    ++m_bloomedCount;
}

void ImageRenderer::DrawPasses(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                               const ImageSettings& settings)
{
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT streamOffset = 0;
    UINT streamStride = 0;
    dev->GetStreamSource(0, &stream, &streamOffset, &streamStride);
    m_state->Capture();
    for (DWORD i = 1; i < kRenderTargets; ++i)
        if (bound.colour[i])
            dev->SetRenderTarget(i, nullptr);
    dev->SetDepthStencilSurface(nullptr);
    SetSharedState(dev, world);
    const bool sharpen = settings.sharpening > 0.0f;
    const bool bloom = settings.bloom > 0.0f;
    if (bloom)
        BuildBloom(dev);
    IDirect3DBaseTexture9* sharpenSource = m_sceneCopy.texture;
    if (settings.antialiasing)
        sharpenSource = Smooth(dev, bound, world, sharpen);
    if (sharpen)
        Sharpen(dev, bound, world, sharpenSource, settings.sharpening);
    if (bloom)
        CompositeBloom(dev, bound, world, settings.bloom);
    dev->SetRenderTarget(0, bound.colour[0]);
    for (DWORD i = 1; i < kRenderTargets; ++i)
        if (bound.colour[i])
            dev->SetRenderTarget(i, bound.colour[i]);
    m_state->Apply();
    dev->SetDepthStencilSurface(bound.depth);
    dev->SetStreamSource(0, stream, streamOffset, streamStride);
    SafeRelease(stream);
}

bool ImageRenderer::DrawTarget(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                               const ImageSettings& settings)
{
    D3DSURFACE_DESC desc = {};
    if (!bound.colour[0])
        return Skip("no render target");
    if (FAILED(bound.colour[0]->GetDesc(&desc)))
        return Skip("surface description failed");
    if (!InsideTarget(world, desc))
        return Skip("world viewport outside the render target");
    if (!EnsureTargets(dev, desc.Format, world, settings) || !CopyWorld(dev, bound.colour[0], world))
        return false;
    DrawPasses(dev, bound, world, settings);
    return true;
}

bool ImageRenderer::Draw(IDirect3DDevice9* dev, const D3DVIEWPORT9& world, const ImageSettings& settings)
{
    m_skip = "";
    if (!settings.Any())
        return Skip("off");
    if (dev->TestCooperativeLevel() != D3D_OK)
        return Skip("device not ready");
    if (!EnsureShaders(dev) || !EnsureStateBlock(dev))
        return false;
    if (settings.antialiasing && !EnsureLookupTextures(dev))
        return false;
    BoundTargets bound;
    for (DWORD i = 0; i < kRenderTargets; ++i)
        dev->GetRenderTarget(i, &bound.colour[i]);
    dev->GetDepthStencilSurface(&bound.depth);
    const bool drawn = DrawTarget(dev, bound, world, settings);
    for (IDirect3DSurface9*& colour : bound.colour)
        SafeRelease(colour);
    SafeRelease(bound.depth);
    return drawn;
}
