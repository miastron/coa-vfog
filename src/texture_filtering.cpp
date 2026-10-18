#include "texture_filtering.h"

#include <algorithm>

namespace
{
bool IsFilteredState(D3DSAMPLERSTATETYPE type)
{
    return type == D3DSAMP_MINFILTER || type == D3DSAMP_MAXANISOTROPY;
}

bool AcceptsAnisotropy(DWORD minFilter)
{
    return minFilter == D3DTEXF_LINEAR || minFilter == D3DTEXF_ANISOTROPIC;
}
}

unsigned AnisotropyLevel(int setting)
{
    unsigned level = 1;
    while (static_cast<int>(level * 2) <= std::min(setting, kMaxForcedAnisotropy))
        level *= 2;
    return level;
}

unsigned DeviceMaxAnisotropy(const D3DCAPS9& caps)
{
    if (!(caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC))
        return 1;
    return std::max<unsigned>(caps.MaxAnisotropy, 1);
}

bool IsMipmapped(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return false;
    const D3DRESOURCETYPE type = texture->GetType();
    return (type == D3DRTYPE_TEXTURE || type == D3DRTYPE_CUBETEXTURE) && texture->GetLevelCount() > 1;
}

SamplerFilter ForcedSamplerFilter(SamplerFilter requested, bool mipmapped, unsigned forcedAnisotropy,
                                  unsigned deviceMaxAnisotropy)
{
    const unsigned forced = std::min(forcedAnisotropy, deviceMaxAnisotropy);
    if (forced <= 1 || !mipmapped || !AcceptsAnisotropy(requested.minFilter))
        return requested;
    const DWORD requestedAnisotropy = requested.minFilter == D3DTEXF_ANISOTROPIC ? requested.maxAnisotropy : 1;
    return {D3DTEXF_ANISOTROPIC, std::max<DWORD>(requestedAnisotropy, forced)};
}

void TextureFilterOverride::Reset(unsigned deviceMaxAnisotropy)
{
    for (Stage& stage : m_stages)
        stage = Stage();
    m_deviceMaxAnisotropy = deviceMaxAnisotropy;
}

HRESULT TextureFilterOverride::SetTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* texture,
                                          unsigned forcedAnisotropy)
{
    const HRESULT hr = dev->SetTexture(stage, texture);
    if (FAILED(hr) || stage >= kFilteredSamplers)
        return hr;
    m_stages[stage].mipmapped = IsMipmapped(texture);
    Apply(dev, stage, forcedAnisotropy);
    return hr;
}

HRESULT TextureFilterOverride::SetSamplerState(IDirect3DDevice9* dev, DWORD stage, D3DSAMPLERSTATETYPE type,
                                               DWORD value, unsigned forcedAnisotropy)
{
    if (stage >= kFilteredSamplers || !IsFilteredState(type))
        return dev->SetSamplerState(stage, type, value);
    SamplerFilter& requested = m_stages[stage].requested;
    (type == D3DSAMP_MINFILTER ? requested.minFilter : requested.maxAnisotropy) = value;
    return Apply(dev, stage, forcedAnisotropy);
}

HRESULT TextureFilterOverride::Apply(IDirect3DDevice9* dev, DWORD stage, unsigned forcedAnisotropy)
{
    Stage& s = m_stages[stage];
    const SamplerFilter target = ForcedSamplerFilter(s.requested, s.mipmapped, forcedAnisotropy, m_deviceMaxAnisotropy);
    HRESULT hr = D3D_OK;
    if (target.maxAnisotropy != s.applied.maxAnisotropy)
    {
        hr = dev->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, target.maxAnisotropy);
        if (SUCCEEDED(hr))
            s.applied.maxAnisotropy = target.maxAnisotropy;
    }
    if (target.minFilter != s.applied.minFilter)
    {
        const HRESULT minHr = dev->SetSamplerState(stage, D3DSAMP_MINFILTER, target.minFilter);
        if (SUCCEEDED(minHr))
            s.applied.minFilter = target.minFilter;
        hr = FAILED(hr) ? hr : minHr;
    }
    return hr;
}
