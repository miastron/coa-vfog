#pragma once

#include <d3d9.h>

constexpr int kAnisotropyOff = 0;
constexpr int kMaxForcedAnisotropy = 16;
constexpr DWORD kFilteredSamplers = 16;

struct SamplerFilter
{
    DWORD minFilter;
    DWORD maxAnisotropy;
};

constexpr SamplerFilter kDefaultSamplerFilter = {D3DTEXF_POINT, 1};

unsigned AnisotropyLevel(int setting);
unsigned DeviceMaxAnisotropy(const D3DCAPS9& caps);
bool IsMipmapped(IDirect3DBaseTexture9* texture);
SamplerFilter ForcedSamplerFilter(SamplerFilter requested, bool mipmapped, unsigned forcedAnisotropy,
                                  unsigned deviceMaxAnisotropy);

class TextureFilterOverride
{
public:
    void Reset(unsigned deviceMaxAnisotropy);
    HRESULT SetTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* texture, unsigned forcedAnisotropy);
    HRESULT SetSamplerState(IDirect3DDevice9* dev, DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value,
                            unsigned forcedAnisotropy);

private:
    struct Stage
    {
        SamplerFilter requested = kDefaultSamplerFilter;
        SamplerFilter applied = kDefaultSamplerFilter;
        bool mipmapped = false;
    };

    HRESULT Apply(IDirect3DDevice9* dev, DWORD stage, unsigned forcedAnisotropy);

    Stage m_stages[kFilteredSamplers];
    unsigned m_deviceMaxAnisotropy = 1;
};
