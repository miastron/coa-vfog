#pragma once

#include <d3d9.h>

struct ImageSettings
{
    float sharpening = 0.0f;

    bool Any() const { return sharpening > 0.0f; }
};

struct ImageStats
{
    unsigned sharpened = 0;
    bool holdsSceneCopy = false;
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

    bool Skip(const char* reason);
    bool EnsureShaders(IDirect3DDevice9* dev);
    bool EnsureStateBlock(IDirect3DDevice9* dev);
    bool EnsureSceneCopy(IDirect3DDevice9* dev, D3DFORMAT format, UINT width, UINT height);
    bool CopyWorld(IDirect3DDevice9* dev, IDirect3DSurface9* target, const D3DSURFACE_DESC& desc,
                   const D3DVIEWPORT9& world);
    bool DrawTarget(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                    const ImageSettings& settings);
    void DrawPasses(IDirect3DDevice9* dev, const BoundTargets& bound, const D3DVIEWPORT9& world,
                    const ImageSettings& settings);
    void SetSharedState(IDirect3DDevice9* dev, const D3DVIEWPORT9& world);
    void Sharpen(IDirect3DDevice9* dev, const D3DVIEWPORT9& world, float strength);

    IDirect3DVertexShader9* m_vs = nullptr;
    IDirect3DPixelShader9* m_sharpen = nullptr;
    IDirect3DVertexDeclaration9* m_decl = nullptr;
    IDirect3DStateBlock9* m_state = nullptr;
    IDirect3DTexture9* m_sceneCopy = nullptr;
    UINT m_sceneCopyWidth = 0;
    UINT m_sceneCopyHeight = 0;
    D3DFORMAT m_sceneCopyFormat = D3DFMT_UNKNOWN;
    bool m_sceneCopyFailed = false;
    IDirect3DDevice9* m_unsupportedDevice = nullptr;
    unsigned m_sharpened = 0;
    const char* m_skip = "";
};
