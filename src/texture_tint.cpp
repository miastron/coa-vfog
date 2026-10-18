#include "texture_tint.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace
{
constexpr uint32_t kBlockTexels = 16;
constexpr uint32_t kBlockSide = 4;
constexpr size_t kDxt1BlockBytes = 8;
constexpr size_t kDxt35BlockBytes = 16;
constexpr float kFiveBits = 31.0f;
constexpr float kSixBits = 63.0f;
constexpr float kFourBits = 15.0f;
constexpr float kByte = 255.0f;

volatile uintptr_t g_textureInterface = 0;

struct TexelSum
{
    double color[3] = {};
    bool alphaWeighted = false;

    void Add(const float rgb[3], float alpha)
    {
        const double weight = alphaWeighted ? alpha : 1.0;
        for (int channel = 0; channel < 3; ++channel)
            color[channel] += rgb[channel] * weight;
    }
};

uint16_t Word(const unsigned char* bytes)
{
    return static_cast<uint16_t>(bytes[0] | bytes[1] << 8);
}

uint32_t Dword(const unsigned char* bytes)
{
    return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
           static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

void Rgb565(uint16_t packed, float rgb[3])
{
    rgb[0] = static_cast<float>((packed >> 11) & 31) / kFiveBits;
    rgb[1] = static_cast<float>((packed >> 5) & 63) / kSixBits;
    rgb[2] = static_cast<float>(packed & 31) / kFiveBits;
}

void ColorPalette(const unsigned char* block, bool punchThrough, float palette[4][3], float alpha[4])
{
    const uint16_t first = Word(block);
    const uint16_t second = Word(block + 2);
    Rgb565(first, palette[0]);
    Rgb565(second, palette[1]);
    const bool fourColors = !punchThrough || first > second;
    for (int channel = 0; channel < 3; ++channel)
    {
        if (fourColors)
        {
            palette[2][channel] = (2.0f * palette[0][channel] + palette[1][channel]) / 3.0f;
            palette[3][channel] = (palette[0][channel] + 2.0f * palette[1][channel]) / 3.0f;
        }
        else
        {
            palette[2][channel] = 0.5f * (palette[0][channel] + palette[1][channel]);
            palette[3][channel] = 0.0f;
        }
    }
    for (int i = 0; i < 3; ++i)
        alpha[i] = 1.0f;
    alpha[3] = fourColors ? 1.0f : 0.0f;
}

void ExplicitAlpha(const unsigned char* block, float alpha[kBlockTexels])
{
    for (uint32_t texel = 0; texel < kBlockTexels; ++texel)
        alpha[texel] = static_cast<float>((block[texel / 2] >> (4 * (texel % 2))) & 15) / kFourBits;
}

void InterpolatedAlpha(const unsigned char* block, float alpha[kBlockTexels])
{
    float palette[8];
    palette[0] = block[0] / kByte;
    palette[1] = block[1] / kByte;
    if (block[0] > block[1])
        for (int k = 2; k < 8; ++k)
            palette[k] = ((8 - k) * palette[0] + (k - 1) * palette[1]) / 7.0f;
    else
    {
        for (int k = 2; k < 6; ++k)
            palette[k] = ((6 - k) * palette[0] + (k - 1) * palette[1]) / 5.0f;
        palette[6] = 0.0f;
        palette[7] = 1.0f;
    }
    uint64_t indices = 0;
    for (int i = 0; i < 6; ++i)
        indices |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    for (uint32_t texel = 0; texel < kBlockTexels; ++texel)
        alpha[texel] = palette[(indices >> (3 * texel)) & 7];
}

void AddBlock(D3DFORMAT format, const unsigned char* block, TexelSum& sum)
{
    float alpha[kBlockTexels];
    const unsigned char* colors = block;
    if (format == D3DFMT_DXT3)
    {
        ExplicitAlpha(block, alpha);
        colors = block + kDxt1BlockBytes;
    }
    else if (format == D3DFMT_DXT5)
    {
        InterpolatedAlpha(block, alpha);
        colors = block + kDxt1BlockBytes;
    }
    float palette[4][3];
    float paletteAlpha[4];
    ColorPalette(colors, format == D3DFMT_DXT1, palette, paletteAlpha);
    const uint32_t indices = Dword(colors + 4);
    for (uint32_t texel = 0; texel < kBlockTexels; ++texel)
    {
        const uint32_t index = (indices >> (2 * texel)) & 3;
        sum.Add(palette[index], format == D3DFMT_DXT1 ? paletteAlpha[index] : alpha[texel]);
    }
}

void AddBlocks(D3DFORMAT format, const unsigned char* bits, int pitch, uint32_t width, uint32_t height,
               TexelSum& sum)
{
    const size_t blockBytes = format == D3DFMT_DXT1 ? kDxt1BlockBytes : kDxt35BlockBytes;
    const uint32_t columns = (width + kBlockSide - 1) / kBlockSide;
    const uint32_t rows = (height + kBlockSide - 1) / kBlockSide;
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t column = 0; column < columns; ++column)
            AddBlock(format, bits + static_cast<ptrdiff_t>(row) * pitch + column * blockBytes, sum);
}

bool DecodePacked(D3DFORMAT format, const unsigned char* texel, float rgb[3], float& alpha)
{
    switch (format)
    {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        rgb[0] = texel[2] / kByte;
        rgb[1] = texel[1] / kByte;
        rgb[2] = texel[0] / kByte;
        alpha = format == D3DFMT_A8R8G8B8 ? texel[3] / kByte : 1.0f;
        return true;
    case D3DFMT_R5G6B5:
        Rgb565(Word(texel), rgb);
        alpha = 1.0f;
        return true;
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
    {
        const uint16_t packed = Word(texel);
        rgb[0] = static_cast<float>((packed >> 10) & 31) / kFiveBits;
        rgb[1] = static_cast<float>((packed >> 5) & 31) / kFiveBits;
        rgb[2] = static_cast<float>(packed & 31) / kFiveBits;
        alpha = format == D3DFMT_A1R5G5B5 ? static_cast<float>(packed >> 15) : 1.0f;
        return true;
    }
    case D3DFMT_A4R4G4B4:
    {
        const uint16_t packed = Word(texel);
        rgb[0] = static_cast<float>((packed >> 8) & 15) / kFourBits;
        rgb[1] = static_cast<float>((packed >> 4) & 15) / kFourBits;
        rgb[2] = static_cast<float>(packed & 15) / kFourBits;
        alpha = static_cast<float>(packed >> 12) / kFourBits;
        return true;
    }
    default:
        return false;
    }
}

size_t PackedTexelBytes(D3DFORMAT format)
{
    switch (format)
    {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        return 4;
    case D3DFMT_R5G6B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A4R4G4B4:
        return 2;
    default:
        return 0;
    }
}

bool AddPackedTexels(D3DFORMAT format, const unsigned char* bits, int pitch, uint32_t width, uint32_t height,
                     TexelSum& sum)
{
    const size_t bytes = PackedTexelBytes(format);
    if (!bytes)
        return false;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            float rgb[3];
            float alpha = 1.0f;
            DecodePacked(format, bits + static_cast<ptrdiff_t>(y) * pitch + x * bytes, rgb, alpha);
            sum.Add(rgb, alpha);
        }
    return true;
}

bool Compressed(D3DFORMAT format)
{
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5;
}

UINT TintLevel(IDirect3DTexture9* texture)
{
    const UINT levels = texture->GetLevelCount();
    UINT chosen = 0;
    for (UINT level = 0; level < levels; ++level)
    {
        D3DSURFACE_DESC desc;
        if (FAILED(texture->GetLevelDesc(level, &desc)))
            break;
        if (std::min(desc.Width, desc.Height) < kTintLevelMinSize)
            break;
        chosen = level;
        if (std::max(desc.Width, desc.Height) <= kTintLevelMaxSize)
            break;
    }
    return chosen;
}

bool LockedTint(IDirect3DTexture9* texture, bool alphaWeighted, float tint[3])
{
    const UINT level = TintLevel(texture);
    D3DSURFACE_DESC desc;
    if (FAILED(texture->GetLevelDesc(level, &desc)) ||
        (desc.Pool != D3DPOOL_MANAGED && desc.Pool != D3DPOOL_SYSTEMMEM))
        return false;
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(level, &locked, nullptr, D3DLOCK_READONLY)))
        return false;
    const bool known = engine::AverageTexelTint(desc.Format, static_cast<const unsigned char*>(locked.pBits),
                                                locked.Pitch, desc.Width, desc.Height, alphaWeighted, tint);
    texture->UnlockRect(level);
    return known;
}

bool TextureTintUnsafe(uintptr_t texture, bool alphaWeighted, float tint[3])
{
    const uintptr_t known = g_textureInterface;
    if (!known || texture < 0x10000 || (texture & 3) || *reinterpret_cast<const uintptr_t*>(texture) != known)
        return false;
    auto* base = reinterpret_cast<IDirect3DBaseTexture9*>(texture);
    if (base->GetType() != D3DRTYPE_TEXTURE)
        return false;
    return LockedTint(static_cast<IDirect3DTexture9*>(base), alphaWeighted, tint);
}
}

namespace engine
{
bool AverageTexelTint(D3DFORMAT format, const unsigned char* bits, int pitch, uint32_t width, uint32_t height,
                      bool alphaWeighted, float tint[3])
{
    if (!bits || !width || !height)
        return false;
    TexelSum sum;
    sum.alphaWeighted = alphaWeighted;
    if (Compressed(format))
        AddBlocks(format, bits, pitch, width, height, sum);
    else if (!AddPackedTexels(format, bits, pitch, width, height, sum))
        return false;
    const double peak = std::max(sum.color[0], std::max(sum.color[1], sum.color[2]));
    const double texels = static_cast<double>(width) * height;
    if (!(peak / texels > kMinTintPeak))
        return false;
    for (int channel = 0; channel < 3; ++channel)
        tint[channel] = static_cast<float>(sum.color[channel] / peak);
    return true;
}

void RememberDeviceTexture(const void* texture)
{
    if (texture && !g_textureInterface)
        g_textureInterface = *static_cast<const uintptr_t*>(texture);
}

bool TextureTint(uintptr_t texture, bool alphaWeighted, float tint[3])
{
    __try
    {
        return TextureTintUnsafe(texture, alphaWeighted, tint);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}
