#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr uintptr_t kLowestClientAddress = 0x10000;
constexpr uintptr_t kClientAddressLimit = 0xFFF00000;

template <typename T>
T Read(uintptr_t address)
{
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

inline void ReadFloats(uintptr_t address, float* out, size_t count)
{
    std::memcpy(out, reinterpret_cast<const void*>(address), count * sizeof(float));
}

inline bool ValidRange(uintptr_t address, size_t size)
{
    return address >= kLowestClientAddress && address < kClientAddressLimit && size <= kClientAddressLimit - address;
}

inline bool ValidPointer(uintptr_t address, size_t size)
{
    return !(address & 3) && ValidRange(address, size);
}

template <size_t Size>
bool Matches(uintptr_t address, const unsigned char (&bytes)[Size])
{
    return std::memcmp(reinterpret_cast<const void*>(address), bytes, Size) == 0;
}
