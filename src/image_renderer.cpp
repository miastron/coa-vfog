#include "image_renderer.h"

#include "fullscreen_triangle.h"
#include "log.h"

#include "ps_sharpen.h"
#include "vs_fullscreen.h"

namespace
{
constexpr UINT kImageConstants = 3;
constexpr DWORD kImageStages = 1;
constexpr DWORD kImageStage = 0;
constexpr DWORD kColourChannels = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
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
    {D3DRS_FOGENABLE, FALSE},
    {D3DRS_SRGBWRITEENABLE, FALSE},
    {D3DRS_CULLMODE, D3DCULL_NONE},
    {D3DRS_FILLMODE, D3DFILL_SOLID},
    {D3DRS_CLIPPLANEENABLE, 0},
    {D3DRS_SCISSORTESTENABLE, TRUE},
    {D3DRS_COLORWRITEENABLE, kColourChannels},
    {D3DRS_MULTISAMPLEMASK, kEverySample},
};

struct SamplerStateValue
{
    D3DSAMPLERSTATETYPE state;
    DWORD value;
};

const SamplerStateValue kPointSampledClamp[] = {
    {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
    {D3DSAMP_MAGFILTER, D3DTEXF_POINT},    {D3DSAMP_MINFILTER, D3DTEXF_POINT},
    {D3DSAMP_MIPFILTER, D3DTEXF_NONE},     {D3DSAMP_SRGBTEXTURE, FALSE},
    {D3DSAMP_MAXMIPLEVEL, 0},
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

void BindPointSampled(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* texture)
{
    dev->SetTexture(stage, texture);
    for (const SamplerStateValue& sampler : kPointSampledClamp)
        dev->SetSamplerState(stage, sampler.state, sampler.value);
}
}

ImageRenderer::~ImageRenderer()
{
    ReleaseAll();
}

void ImageRenderer::ReleaseDefaultPool()
{
    SafeRelease(m_sceneCopy);
    m_sceneCopyWidth = m_sceneCopyHeight = 0;
    m_sceneCopyFormat = D3DFMT_UNKNOWN;
    m_sceneCopyFailed = false;
    SafeRelease(m_state);
}

void ImageRenderer::ReleaseAll()
{
    ReleaseDefaultPool();
    SafeRelease(m_vs);
    SafeRelease(m_sharpen);
    SafeRelease(m_decl);
    m_unsupportedDevice = nullptr;
}

ImageStats ImageRenderer::Stats() const
{
    ImageStats stats;
    stats.sharpened = m_sharpened;
    stats.holdsSceneCopy = m_sceneCopy != nullptr;
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
    if (m_sharpen)
        return true;
    static const D3DVERTEXELEMENT9 kElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END(),
    };
    HRESULT result = dev->CreateVertexShader(reinterpret_cast<const DWORD*>(g_vs_fullscreen), &m_vs);
    if (SUCCEEDED(result))
        result = dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_sharpen), &m_sharpen);
    if (SUCCEEDED(result))
        result = dev->CreateVertexDeclaration(kElements, &m_decl);
    if (SUCCEEDED(result))
        return true;
    SafeRelease(m_vs);
    SafeRelease(m_sharpen);
    SafeRelease(m_decl);
    m_unsupportedDevice = dev;
    VF_LOG_ERROR("image shaders could not be created (HRESULT 0x%08lX); no sharpening on this device",
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
        for (const SamplerStateValue& sampler : kPointSampledClamp)
            dev->SetSamplerState(stage, sampler.state, 0);
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

bool ImageRenderer::EnsureSceneCopy(IDirect3DDevice9* dev, D3DFORMAT format, UINT width, UINT height)
{
    if (m_sceneCopy && m_sceneCopyWidth == width && m_sceneCopyHeight == height && m_sceneCopyFormat == format)
        return true;
    SafeRelease(m_sceneCopy);
    m_sceneCopyWidth = m_sceneCopyHeight = 0;
    if (m_sceneCopyFailed)
        return Skip("scene copy creation failed");
    const D3DFORMAT candidates[] = {format, D3DFMT_A8R8G8B8};
    for (D3DFORMAT candidate : candidates)
        if (SUCCEEDED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, candidate, D3DPOOL_DEFAULT,
                                         &m_sceneCopy, nullptr)) &&
            m_sceneCopy)
        {
            m_sceneCopyWidth = width;
            m_sceneCopyHeight = height;
            m_sceneCopyFormat = format;
            return true;
        }
    m_sceneCopy = nullptr;
    m_sceneCopyFailed = true;
    VF_LOG_ERROR("image scene copy creation failed (%ux%u)", width, height);
    return Skip("scene copy creation failed");
}

bool ImageRenderer::CopyWorld(IDirect3DDevice9* dev, IDirect3DSurface9* target, const D3DSURFACE_DESC& desc,
                              const D3DVIEWPORT9& world)
{
    if (!EnsureSceneCopy(dev, desc.Format, world.Width, world.Height))
        return false;
    const RECT rect = ViewportRect(world);
    IDirect3DSurface9* copy = nullptr;
    const bool copied = SUCCEEDED(m_sceneCopy->GetSurfaceLevel(0, &copy)) &&
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
    dev->SetPixelShaderConstantF(1, metrics, 1);
}

void ImageRenderer::Sharpen(IDirect3DDevice9* dev, const D3DVIEWPORT9& world, float strength)
{
    const RECT scissor = ViewportRect(world);
    dev->SetViewport(&world);
    dev->SetScissorRect(&scissor);
    dev->SetPixelShader(m_sharpen);
    BindPointSampled(dev, kImageStage, m_sceneCopy);
    const float origin[4] = {static_cast<float>(world.X), static_cast<float>(world.Y), 0.0f, 0.0f};
    const float settings[4] = {strength, 0.0f, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(0, origin, 1);
    dev->SetPixelShaderConstantF(2, settings, 1);
    DrawFullscreenTriangle(dev);
    ++m_sharpened;
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
    Sharpen(dev, world, settings.sharpening);
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
    if (!CopyWorld(dev, bound.colour[0], desc, world))
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
