#include "spell_lights.h"

#include "client_memory.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr char kSpellModelFolder[] = "spells\\";
constexpr size_t kSpellModelFolderLength = sizeof(kSpellModelFolder) - 1;
constexpr size_t kTrackColorBytes = 3 * sizeof(float);
constexpr size_t kBoundsFloats = 6;
constexpr size_t kMatrixFloats = 16;
constexpr float kAxisShareOfRadius = 0.57735027f;

const ClientCodeGuard kSpellLightCode[] = {
    {"particle render bounds reset", 0x0097EA81, 6, {0x89, 0x81, 0x18, 0x02, 0x00, 0x00}},
    {"particle render live count", 0x0097EABA, 16,
     {0x83, 0x79, 0x50, 0x00, 0x75, 0x14, 0x83, 0xA1, 0x38, 0x01, 0x00, 0x00, 0xFE, 0xC7, 0x41, 0x1C}},
    {"particle render return", 0x0097EAFF, 3, {0xC2, 0x0C, 0x00}},
    {"particle colour track", 0x009795DA, 15,
     {0x8B, 0xBE, 0xD8, 0x00, 0x00, 0x00, 0x83, 0x7F, 0x08, 0x01, 0x75, 0x43, 0x8B, 0x47, 0x0C}},
    {"particle replacement colour flag", 0x00979642, 7, {0xF6, 0x86, 0x34, 0x01, 0x00, 0x00, 0x10}},
    {"particle replacement colours", 0x00979652, 7, {0xD9, 0x84, 0x8E, 0xF4, 0x00, 0x00, 0x00}},
    {"particle alpha track", 0x00979F01, 6, {0x8B, 0x8E, 0xDC, 0x00, 0x00, 0x00}},
    {"particle alpha scale", 0x00979F0F, 6, {0xD8, 0x8E, 0x30, 0x01, 0x00, 0x00}},
    {"fixed16 track values", 0x009794F7, 12,
     {0x83, 0x7F, 0x08, 0x01, 0x75, 0x17, 0x8B, 0x47, 0x0C, 0x0F, 0xBF, 0x08}},
    {"single particle draw element", 0x008214E7, 15,
     {0x8B, 0xF1, 0x8B, 0x46, 0x50, 0x57, 0x8B, 0x78, 0x18, 0x8B, 0x8F, 0xD4, 0x00, 0x00, 0x00}},
    {"single particle draw blend", 0x008214F9, 6, {0x8B, 0x87, 0xD0, 0x00, 0x00, 0x00}},
    {"single particle render call", 0x00821623, 16,
     {0x8B, 0x56, 0x60, 0x8B, 0x82, 0x74, 0x01, 0x00, 0x00, 0x6A, 0x01, 0x6A, 0x00, 0x50, 0x8B, 0xCF}},
    {"batched particle element", 0x00821411, 15,
     {0x8B, 0x55, 0x10, 0x8B, 0x02, 0x8B, 0x55, 0x0C, 0x8B, 0xC8, 0xC1, 0xE1, 0x04, 0x03, 0xC8}},
    {"batched particle emitter", 0x00821420, 4, {0x8B, 0x74, 0x8A, 0x18}},
    {"batched particle render call", 0x0082143C, 3, {0x51, 0x8B, 0xCE}},
    {"batched particle element model", 0x00821169, 3, {0x8B, 0x4F, 0x04}},
    {"model scene", 0x00824141, 6, {0x8B, 0x56, 0x28, 0x8B, 0x52, 0x0C}},
    {"model file name", 0x008245BE, 8, {0x8B, 0x46, 0x2C, 0x53, 0x57, 0x83, 0xC0, 0x3C}},
    {"particle texture", 0x00821563, 6, {0x8B, 0x87, 0x28, 0x01, 0x00, 0x00}},
    {"texture gx texture", 0x004B6D4B, 4, {0x83, 0x7E, 0x44, 0x00}},
    {"gx texture device texture", 0x006A492E, 3, {0x8B, 0x53, 0x38}},
    {"mesh batch dispatch", 0x00823A6C, 14,
     {0x8B, 0x56, 0x50, 0x8B, 0x02, 0x83, 0xF8, 0x05, 0x0F, 0x87, 0xAD, 0x00, 0x00, 0x00}},
    {"mesh batch draw call", 0x00823A7A, 9, {0xFF, 0x24, 0x85, 0x90, 0x3C, 0x82, 0x00, 0x8B, 0xCE}},
    {"mesh batch return", 0x00820615, 1, {0xC3}},
    {"mesh batch and skin section", 0x008203D8, 16,
     {0x8B, 0x46, 0x50, 0x8B, 0x50, 0x28, 0x89, 0x96, 0x88, 0x00, 0x00, 0x00, 0x8B, 0x48, 0x2C, 0x89}},
    {"mesh batch material", 0x008203ED, 16,
     {0x0F, 0xB7, 0x4A, 0x0A, 0x8B, 0x56, 0x48, 0x8B, 0x52, 0x74, 0x8D, 0x0C, 0x8A, 0x89, 0x8E, 0x98}},
    {"mesh batch bone combos", 0x00820486, 10, {0x0F, 0xB7, 0x49, 0x0E, 0x8B, 0x7E, 0x48, 0x8B, 0x7F, 0x7C}},
    {"mesh batch colour", 0x0082023F, 16,
     {0x0F, 0xB7, 0x49, 0x08, 0x8B, 0x7E, 0x48, 0x3B, 0x4F, 0x48, 0x73, 0x29, 0x8B, 0x80, 0xA0, 0x00}},
    {"mesh batch colour value", 0x00820251, 7, {0xC1, 0xE1, 0x05, 0xD9, 0x44, 0x01, 0x08}},
    {"model diffuse colour", 0x008201E9, 6, {0x8B, 0x88, 0xA0, 0x01, 0x00, 0x00}},
    {"mesh batch texture combo", 0x0081F4E6, 13,
     {0x0F, 0xB7, 0x51, 0x10, 0x8B, 0x43, 0x48, 0x8B, 0x88, 0x84, 0x00, 0x00, 0x00}},
    {"mesh batch texture count", 0x0081F4FC, 3, {0x3B, 0x70, 0x50}},
    {"model textures", 0x0081F508, 6, {0x8B, 0x82, 0xA4, 0x00, 0x00, 0x00}},
    {"model bones and inverse view", 0x00828AC2, 15,
     {0x8B, 0x53, 0x28, 0x03, 0x8B, 0x98, 0x00, 0x00, 0x00, 0x81, 0xC2, 0xC4, 0x00, 0x00, 0x00}},
};

float UnitOrZero(float value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

bool ClampFiniteColor(float color[3])
{
    for (int channel = 0; channel < 3; ++channel)
    {
        if (!std::isfinite(color[channel]))
            return false;
        color[channel] = std::clamp(color[channel], 0.0f, 1.0f);
    }
    return true;
}

bool ReadIndexedWord(uintptr_t words, uint32_t index, uint32_t& value)
{
    const uintptr_t address = words + index * sizeof(uint16_t);
    if (!ValidRange(address, sizeof(uint16_t)))
        return false;
    value = Read<uint16_t>(address);
    return true;
}

char LowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool InSpellFolder(uintptr_t fileName)
{
    for (size_t i = 0; i < kSpellModelFolderLength; ++i)
    {
        const char c = LowerAscii(Read<char>(fileName + i));
        if (c != kSpellModelFolder[i] && !(kSpellModelFolder[i] == '\\' && c == '/'))
            return false;
    }
    return true;
}

bool ReadBounds(uintptr_t emitter, SpellGlow& glow)
{
    float bounds[kBoundsFloats];
    ReadFloats(emitter + engine::kEmitterWorldBounds, bounds, kBoundsFloats);
    for (int axis = 0; axis < 3; ++axis)
    {
        const float low = bounds[axis];
        const float high = bounds[axis + 3];
        if (!std::isfinite(low) || !std::isfinite(high) || low > high || high - low > 2.0f * kSpellGlowMaxExtent)
            return false;
        glow.boundsMin[axis] = low;
        glow.boundsMax[axis] = high;
    }
    return true;
}

bool MeanTrackColor(uintptr_t emitter, float color[3])
{
    const uintptr_t track = Read<uintptr_t>(emitter + engine::kEmitterColorTrack);
    if (!ValidPointer(track, engine::kPartTrackValues + sizeof(uintptr_t)))
        return false;
    const uint32_t count = std::min(Read<uint32_t>(track + engine::kPartTrackValueCount), kMaxSpellGlowTrackKeys);
    const uintptr_t values = Read<uintptr_t>(track + engine::kPartTrackValues);
    if (!count || !ValidPointer(values, count * kTrackColorBytes))
        return false;
    const bool replaced = (Read<uint32_t>(emitter + engine::kEmitterFlags) & engine::kEmitterReplacesColors) != 0;
    float sum[3] = {};
    for (uint32_t key = 0; key < count; ++key)
    {
        const uintptr_t source =
            replaced ? emitter + engine::kEmitterReplacementColors +
                           std::min(key, engine::kReplacementColorCount - 1) * kTrackColorBytes
                     : values + key * kTrackColorBytes;
        for (int channel = 0; channel < 3; ++channel)
            sum[channel] += Read<float>(source + channel * sizeof(float));
    }
    for (int channel = 0; channel < 3; ++channel)
        color[channel] = sum[channel] / static_cast<float>(count) * engine::kParticleColorScale;
    return ClampFiniteColor(color);
}

float MeanTrackAlpha(uintptr_t emitter)
{
    const uintptr_t track = Read<uintptr_t>(emitter + engine::kEmitterAlphaTrack);
    if (!ValidPointer(track, engine::kPartTrackValues + sizeof(uintptr_t)))
        return 0.0f;
    const uint32_t count = std::min(Read<uint32_t>(track + engine::kPartTrackValueCount), kMaxSpellGlowTrackKeys);
    const uintptr_t values = Read<uintptr_t>(track + engine::kPartTrackValues);
    if (!count)
        return 1.0f;
    if (!ValidRange(values, count * sizeof(int16_t)))
        return 0.0f;
    float sum = 0.0f;
    for (uint32_t key = 0; key < count; ++key)
        sum += static_cast<float>(Read<int16_t>(values + key * sizeof(int16_t)));
    return std::clamp(sum / static_cast<float>(count) / engine::kFixed16One, 0.0f, 1.0f);
}

bool AdditiveBlend(uint32_t blend)
{
    return blend == engine::kAdditiveParticleBlend || blend == engine::kOpaqueAdditiveParticleBlend;
}

bool AdditiveMeshBlend(uint32_t blend)
{
    return blend == engine::kM2AdditiveBlend || blend == engine::kM2OpaqueAdditiveBlend;
}

uintptr_t DeviceTexture(uintptr_t texture)
{
    if (!ValidPointer(texture, engine::kTextureGxTexture + sizeof(uintptr_t)))
        return 0;
    const uintptr_t gxTexture = Read<uintptr_t>(texture + engine::kTextureGxTexture);
    if (!ValidPointer(gxTexture, engine::kGxTextureDeviceTexture + sizeof(uintptr_t)))
        return 0;
    return Read<uintptr_t>(gxTexture + engine::kGxTextureDeviceTexture);
}

uintptr_t SpellModel(uintptr_t element, uintptr_t worldScene)
{
    const uintptr_t model = Read<uintptr_t>(element + engine::kM2ElementModel);
    if (!ValidPointer(model, engine::kM2ModelShared + sizeof(uintptr_t)) ||
        Read<uintptr_t>(model + engine::kM2ModelScene) != worldScene)
        return 0;
    const uintptr_t shared = Read<uintptr_t>(model + engine::kM2ModelShared);
    if (!ValidPointer(shared, engine::kM2SharedFileName + kSpellModelFolderLength) ||
        !InSpellFolder(shared + engine::kM2SharedFileName))
        return 0;
    return model;
}

bool ReadSpellGlowUnsafe(uintptr_t emitter, uintptr_t element, uintptr_t worldScene, SpellGlow& out)
{
    if (!worldScene || !ValidPointer(element, engine::kM2ElementEmitter + sizeof(uintptr_t)) ||
        Read<uintptr_t>(element + engine::kM2ElementEmitter) != emitter ||
        !ValidPointer(emitter, engine::kEmitterWorldBounds + kBoundsFloats * sizeof(float)))
        return false;
    const uint32_t rendered = Read<uint32_t>(emitter + engine::kEmitterRenderedParticles);
    if (!rendered || !AdditiveBlend(Read<uint32_t>(emitter + engine::kEmitterMaterialBlend)))
        return false;
    SpellGlow glow;
    glow.model = SpellModel(element, worldScene);
    if (!glow.model || !ReadBounds(emitter, glow) || !MeanTrackColor(emitter, glow.color))
        return false;
    const float particles = std::min(static_cast<float>(rendered) / kSpellGlowFullParticles, 1.0f);
    glow.strength = MeanTrackAlpha(emitter) * UnitOrZero(Read<float>(emitter + engine::kEmitterAlphaScale)) * particles;
    if (!(glow.strength > 0.0f))
        return false;
    glow.texture = DeviceTexture(Read<uintptr_t>(emitter + engine::kEmitterTexture));
    glow.textureAlphaWeighted =
        Read<uint32_t>(emitter + engine::kEmitterMaterialBlend) == engine::kAdditiveParticleBlend;
    out = glow;
    return true;
}

void TransformPoint(const float point[3], const float matrix[kMatrixFloats], float out[3])
{
    for (int j = 0; j < 3; ++j)
        out[j] = point[0] * matrix[j] + point[1] * matrix[4 + j] + point[2] * matrix[8 + j] + matrix[12 + j];
}

bool MeshColor(uintptr_t model, uintptr_t data, uintptr_t batch, float color[3])
{
    ReadFloats(model + engine::kM2ModelDiffuseColor, color, 3);
    const uint32_t colorIndex = Read<uint16_t>(batch + engine::kM2BatchColorIndex);
    if (colorIndex < Read<uint32_t>(data + engine::kM2DataColorCount))
    {
        const uintptr_t colors = Read<uintptr_t>(model + engine::kM2ModelColors);
        const uintptr_t value = colors + colorIndex * engine::kM2ModelColorSize + engine::kM2ModelColorValue;
        if (!ValidPointer(value, kTrackColorBytes))
            return false;
        float track[3];
        ReadFloats(value, track, 3);
        for (int channel = 0; channel < 3; ++channel)
            color[channel] *= track[channel];
    }
    return ClampFiniteColor(color);
}

bool MeshBounds(uintptr_t model, uintptr_t data, uintptr_t skin, uintptr_t worldScene, SpellGlow& glow)
{
    const uint32_t comboIndex = Read<uint16_t>(skin + engine::kM2SkinSectionBoneComboIndex);
    uint32_t bone = 0;
    if (!ReadIndexedWord(Read<uintptr_t>(data + engine::kM2DataBoneCombos), comboIndex, bone))
        return false;
    const uintptr_t bones = Read<uintptr_t>(model + engine::kM2ModelBoneMatrices);
    if (bone >= Read<uint32_t>(data + engine::kM2DataBoneCount) ||
        !ValidPointer(bones + bone * engine::kMatrixBytes, engine::kMatrixBytes) ||
        !ValidPointer(worldScene, engine::kM2SceneInverseView + engine::kMatrixBytes))
        return false;
    float boneMatrix[kMatrixFloats];
    float inverseView[kMatrixFloats];
    float centre[3];
    ReadFloats(bones + bone * engine::kMatrixBytes, boneMatrix, kMatrixFloats);
    ReadFloats(worldScene + engine::kM2SceneInverseView, inverseView, kMatrixFloats);
    ReadFloats(skin + engine::kM2SkinSectionBoundsCenter, centre, 3);
    float inView[3];
    float world[3];
    TransformPoint(centre, boneMatrix, inView);
    TransformPoint(inView, inverseView, world);
    const float scale = std::sqrt(boneMatrix[0] * boneMatrix[0] + boneMatrix[1] * boneMatrix[1] +
                                  boneMatrix[2] * boneMatrix[2]);
    const float radius = Read<float>(skin + engine::kM2SkinSectionRadius) * scale;
    if (!std::isfinite(radius) || radius < 0.0f || radius > kSpellGlowMaxExtent)
        return false;
    const float half = radius * kAxisShareOfRadius;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(world[axis]))
            return false;
        glow.boundsMin[axis] = world[axis] - half;
        glow.boundsMax[axis] = world[axis] + half;
    }
    return true;
}

uintptr_t MeshTexture(uintptr_t model, uintptr_t data, uintptr_t batch)
{
    const uint32_t comboIndex = Read<uint16_t>(batch + engine::kM2BatchTextureComboIndex);
    if (comboIndex >= Read<uint32_t>(data + engine::kM2DataTextureComboCount))
        return 0;
    uint32_t combo = 0;
    if (!ReadIndexedWord(Read<uintptr_t>(data + engine::kM2DataTextureCombos), comboIndex, combo))
        return 0;
    const uintptr_t textures = Read<uintptr_t>(model + engine::kM2ModelTextures);
    if (combo >= Read<uint32_t>(data + engine::kM2DataTextureCount) ||
        !ValidPointer(textures + combo * sizeof(uintptr_t), sizeof(uintptr_t)))
        return 0;
    return DeviceTexture(Read<uintptr_t>(textures + combo * sizeof(uintptr_t)));
}

bool ReadSpellMeshGlowUnsafe(uintptr_t sceneRender, uintptr_t worldScene, SpellGlow& out)
{
    if (!worldScene || !ValidPointer(sceneRender, engine::kSceneRenderCurrentMaterial + sizeof(uintptr_t)))
        return false;
    const uintptr_t element = Read<uintptr_t>(sceneRender + engine::kSceneRenderCurrentElement);
    if (!ValidPointer(element, engine::kM2ElementEmitter + sizeof(uintptr_t)) ||
        Read<uint32_t>(element) != engine::kM2MeshBatchElement)
        return false;
    const uintptr_t material = Read<uintptr_t>(sceneRender + engine::kSceneRenderCurrentMaterial);
    if (!ValidPointer(material, sizeof(uint32_t)))
        return false;
    const uint32_t blend = Read<uint16_t>(material + engine::kM2MaterialBlend);
    if (!AdditiveMeshBlend(blend))
        return false;
    SpellGlow glow;
    glow.model = SpellModel(element, worldScene);
    const uintptr_t data = Read<uintptr_t>(sceneRender + engine::kSceneRenderData);
    const uintptr_t batch = Read<uintptr_t>(sceneRender + engine::kSceneRenderCurrentBatch);
    const uintptr_t skin = Read<uintptr_t>(sceneRender + engine::kSceneRenderCurrentSkinSection);
    if (!glow.model || Read<uintptr_t>(sceneRender + engine::kSceneRenderCurrentModel) != glow.model ||
        !ValidPointer(data, engine::kM2DataTextureCombos + sizeof(uintptr_t)) ||
        !ValidRange(batch, engine::kM2BatchTextureComboIndex + sizeof(uint16_t)) ||
        !ValidRange(skin, engine::kM2SkinSectionRadius + sizeof(float)) ||
        !ValidPointer(glow.model, engine::kM2ModelDiffuseColor + kTrackColorBytes) ||
        !MeshColor(glow.model, data, batch, glow.color) || !MeshBounds(glow.model, data, skin, worldScene, glow))
        return false;
    glow.strength = UnitOrZero(Read<float>(element + engine::kM2ElementAlpha));
    if (!(glow.strength > 0.0f))
        return false;
    glow.texture = MeshTexture(glow.model, data, batch);
    glow.textureAlphaWeighted = blend == engine::kM2AdditiveBlend;
    out = glow;
    return true;
}

uintptr_t WorldScene(uintptr_t worldSceneSlot)
{
    return ValidPointer(worldSceneSlot, sizeof(uintptr_t)) ? Read<uintptr_t>(worldSceneSlot) : 0;
}

bool ObserveUnsafe(SpellLightCollector& lights, uintptr_t emitter, uintptr_t element, uintptr_t worldSceneSlot)
{
    SpellGlow glow;
    if (!ReadSpellGlowUnsafe(emitter, element, WorldScene(worldSceneSlot), glow))
        return false;
    lights.Add(glow);
    return true;
}

bool ObserveMeshUnsafe(SpellLightCollector& lights, uintptr_t sceneRender, uintptr_t worldSceneSlot)
{
    SpellGlow glow;
    if (!ReadSpellMeshGlowUnsafe(sceneRender, WorldScene(worldSceneSlot), glow))
        return false;
    lights.Add(glow);
    return true;
}

uintptr_t BatchedElement(uintptr_t callerFrame)
{
    if (!ValidPointer(callerFrame, engine::kBatchedDrawIndexCursor + sizeof(uintptr_t)))
        return 0;
    const uintptr_t elements = Read<uintptr_t>(callerFrame + engine::kBatchedDrawElements);
    const uintptr_t cursor = Read<uintptr_t>(callerFrame + engine::kBatchedDrawIndexCursor);
    if (!ValidPointer(elements, engine::kM2ElementSize) || !ValidPointer(cursor, sizeof(uint32_t)))
        return 0;
    return elements + Read<uint32_t>(cursor) * engine::kM2ElementSize;
}

uintptr_t SingleElement(uintptr_t sceneRender)
{
    if (!ValidPointer(sceneRender, engine::kSceneRenderCurrentElement + sizeof(uintptr_t)))
        return 0;
    return Read<uintptr_t>(sceneRender + engine::kSceneRenderCurrentElement);
}

void Widen(SpellGlow& merged, const SpellGlow& glow)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        merged.boundsMin[axis] = std::min(merged.boundsMin[axis], glow.boundsMin[axis]);
        merged.boundsMax[axis] = std::max(merged.boundsMax[axis], glow.boundsMax[axis]);
        merged.color[axis] += glow.color[axis] * glow.strength;
    }
    merged.strength += glow.strength;
}
}

namespace engine
{
const ClientCodeGuard* SpellLightGuards(size_t& count)
{
    count = sizeof(kSpellLightCode) / sizeof(kSpellLightCode[0]);
    return kSpellLightCode;
}

bool SpellLightCodeMatches(const ClientCodeView& code)
{
    for (const ClientCodeGuard& guard : kSpellLightCode)
        if (!ClientGuardMatches(code, guard))
        {
            VF_LOG_ERROR("spell lights: the client's %s at 0x%08X differs from the 12340 client; spells do not light "
                         "the fog",
                         guard.name, static_cast<unsigned>(guard.address));
            return false;
        }
    return true;
}

bool ReadSpellGlow(uintptr_t emitter, uintptr_t element, uintptr_t worldScene, SpellGlow& out)
{
    __try
    {
        return ReadSpellGlowUnsafe(emitter, element, worldScene, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ObserveSingleParticleRender(SpellLightCollector& lights, uintptr_t emitter, uintptr_t sceneRender,
                                 uintptr_t worldSceneSlot)
{
    __try
    {
        return ObserveUnsafe(lights, emitter, SingleElement(sceneRender), worldSceneSlot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ObserveBatchedParticleRender(SpellLightCollector& lights, uintptr_t emitter, uintptr_t callerFrame,
                                  uintptr_t worldSceneSlot)
{
    __try
    {
        return ObserveUnsafe(lights, emitter, BatchedElement(callerFrame), worldSceneSlot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ReadSpellMeshGlow(uintptr_t sceneRender, uintptr_t worldScene, SpellGlow& out)
{
    __try
    {
        return ReadSpellMeshGlowUnsafe(sceneRender, worldScene, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ObserveMeshBatchDraw(SpellLightCollector& lights, uintptr_t sceneRender, uintptr_t worldSceneSlot)
{
    __try
    {
        return ObserveMeshUnsafe(lights, sceneRender, worldSceneSlot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

uint32_t AddSpellLights(LocalLightInputs& out, const SpellLightCollector& lights, const LocalLightCamera& camera,
                        const PointLightUpload& upload)
{
    if (!(upload.spellIntensity > 0.0f) || !CapturedLightsUsable(out.capture))
        return 0;
    uint32_t added = 0;
    for (uint32_t i = 0; i < lights.Count(); ++i)
    {
        LocalPointLight light;
        if (lights.Light(i, upload.spellIntensity, light) && SelectLocalPointLightInView(out, light, camera, upload))
            ++added;
    }
    return added;
}
}

void SpellLightCollector::UseTextureTints(TextureTintReader reader)
{
    m_tintReader = reader;
    m_tintCount = 0;
}

const SpellLightCollector::CachedTint& SpellLightCollector::CachedTintFor(const SpellGlow& glow)
{
    for (uint32_t i = 0; i < m_tintCount; ++i)
        if (m_tints[i].texture == glow.texture && m_tints[i].alphaWeighted == glow.textureAlphaWeighted)
            return m_tints[i];
    if (m_tintCount == kSpellTintCacheEntries)
        m_tintCount = 0;
    CachedTint& cached = m_tints[m_tintCount++];
    cached = CachedTint{};
    cached.texture = glow.texture;
    cached.alphaWeighted = glow.textureAlphaWeighted;
    cached.known = m_tintReader(glow.texture, glow.textureAlphaWeighted, cached.tint);
    return cached;
}

void SpellLightCollector::Tint(SpellGlow& glow)
{
    if (!m_tintReader || !glow.texture)
        return;
    const CachedTint& cached = CachedTintFor(glow);
    if (!cached.known)
        return;
    for (int channel = 0; channel < 3; ++channel)
        glow.color[channel] *= cached.tint[channel];
}

void SpellLightCollector::Add(const SpellGlow& observed)
{
    SpellGlow glow = observed;
    Tint(glow);
    for (uint32_t i = 0; i < m_gatheringCount; ++i)
        if (m_gathering[i].model == glow.model)
        {
            Widen(m_gathering[i], glow);
            return;
        }
    if (m_gatheringCount == kMaxSpellLightModels)
        return;
    SpellGlow& merged = m_gathering[m_gatheringCount++];
    merged = glow;
    for (int channel = 0; channel < 3; ++channel)
        merged.color[channel] = glow.color[channel] * glow.strength;
}

void SpellLightCollector::Publish()
{
    std::memcpy(m_published, m_gathering, sizeof(m_published));
    m_publishedCount = m_gatheringCount;
    m_gatheringCount = 0;
}

void SpellLightCollector::Clear()
{
    m_gatheringCount = 0;
    m_publishedCount = 0;
}

bool SpellLightCollector::Light(uint32_t index, float intensity, LocalPointLight& out) const
{
    if (index >= m_publishedCount || !(intensity > 0.0f))
        return false;
    const SpellGlow& glow = m_published[index];
    if (!(glow.strength > 0.0f))
        return false;
    LocalPointLight light;
    float extentSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float half = 0.5f * (glow.boundsMax[axis] - glow.boundsMin[axis]);
        light.position[axis] = glow.boundsMin[axis] + half;
        extentSquared += half * half;
    }
    const float brightness = std::min(glow.strength, 1.0f) * intensity / glow.strength;
    for (int channel = 0; channel < 3; ++channel)
        light.color[channel] = glow.color[channel] * brightness;
    const float reach =
        std::min(kSpellLightBaseReach + kSpellLightReachPerExtent * std::sqrt(extentSquared), kSpellLightMaxReach);
    light.attenuation[0] = 1.0f;
    light.attenuation[1] = 0.0f;
    light.attenuation[2] = (1.0f / kLocalPointLightContributionCutoff - 1.0f) / (reach * reach);
    light.nativeId = glow.model;
    light.source = LocalLightSource::Spell;
    out = light;
    return true;
}

SpellLightCollector& GlobalSpellLights()
{
    static SpellLightCollector lights;
    return lights;
}
