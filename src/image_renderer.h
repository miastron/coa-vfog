#pragma once

#include <d3d9.h>

struct ImageSettings
{
    bool antialiasing = false;
    float sharpening = 0.0f;

    bool Any() const { return antialiasing || sharpening > 0.0f; }
};

struct ImageStats
{
    unsigned smoothed = 0;
    unsigned sharpened = 0;
    bool holdsSceneCopy = false;
    bool holdsAntialiasingTargets = false;
    bool linearBlend = false;
};

class ImageRenderer
{
public:
    ~ImageRenderer();

    void ReleaseDefaultPool();
    void ReleaseAll();

    bool Draw(IDirect3DDevice9* dev, const D3DVIEWPORT9& world, const ImageSettings& settings);

    const char* LastSkipReason() const { return m_skip; }
    ImageStats Stats() const;

private:
    static constexpr DWORD kRenderTargets = 4;

    struct BoundTargets
    {
        IDirect3DSurface9* colour[kRenderTargets] = {};
        IDirect3DSurface9* depth = nullptr;
    };

    struct ViewTexture
    {
        IDirect3DTexture9* texture = nullptr;
        UINT width = 0;
        UINT height = 0;
        D3DFORMAT format = D3DFMT_UNKNOWN;
    };

    bool Skip(const char* reason);
    bool EnsureShaders(IDirect3DDevice9* dev);
    bool EnsureStateBlock(IDirect3DDevice9* dev);
    bool EnsureLookupTextures(IDirect3DDevice9* dev);
    bool EnsureViewTexture(IDirect3DDevice9* dev, ViewTexture& view, D3DFORMAT format, UINT width, UINT height);
    bool EnsureTargets(IDirect3DDevice9* dev, D3DFORMAT format, const D3DVIEWPORT9& world,
                       const ImageSettings& settings);
    bool CopyWorld(IDirect3DDevice9* dev, IDirect3DSurface9* target, const D3DVIEWPORT9& world);
    bool DrawTarget(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                    const ImageSettings& settings);
    void DrawPasses(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                    const ImageSettings& settings);
    void SetSharedState(IDirect3DDevice9* dev, const D3DVIEWPORT9& world);
    void Target(IDirect3DDevice9* dev, IDirect3DSurface9* surface, const D3DVIEWPORT9& viewport);
    void TargetTexture(IDirect3DDevice9* dev, const ViewTexture& view, bool cleared);
    IDirect3DBaseTexture9* Smooth(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                                  bool intoTexture);
    void Sharpen(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                 IDirect3DBaseTexture9* source, float strength);
    void ReleaseViewTexture(ViewTexture& view);

    IDirect3DVertexShader9* m_vs = nullptr;
    IDirect3DPixelShader9* m_sharpen = nullptr;
    IDirect3DPixelShader9* m_edgesPs = nullptr;
    IDirect3DPixelShader9* m_weightsPs = nullptr;
    IDirect3DPixelShader9* m_blendPs = nullptr;
    IDirect3DVertexDeclaration9* m_decl = nullptr;
    IDirect3DStateBlock9* m_state = nullptr;
    IDirect3DTexture9* m_area = nullptr;
    IDirect3DTexture9* m_search = nullptr;
    ViewTexture m_sceneCopy;
    ViewTexture m_edges;
    ViewTexture m_weights;
    ViewTexture m_smoothed;
    bool m_linearBlend = false;
    bool m_targetsFailed = false;
    bool m_lookupFailed = false;
    IDirect3DDevice9* m_unsupportedDevice = nullptr;
    unsigned m_smoothedCount = 0;
    unsigned m_sharpenedCount = 0;
    const char* m_skip = "";
};
