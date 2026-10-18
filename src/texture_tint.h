#pragma once

#include <d3d9.h>

#include <cstddef>
#include <cstdint>

constexpr uint32_t kTintLevelMaxSize = 16;
constexpr uint32_t kTintLevelMinSize = 4;
constexpr double kMinTintPeak = 1.0 / 255.0;

namespace engine
{
bool AverageTexelTint(D3DFORMAT format, const unsigned char* bits, int pitch, uint32_t width, uint32_t height,
                      bool alphaWeighted, float tint[3]);
void RememberDeviceTexture(const void* texture);
bool TextureTint(uintptr_t texture, bool alphaWeighted, float tint[3]);
}
