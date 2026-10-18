#pragma once

#include "client_code.h"
#include "engine_lights.h"

#include <cstddef>
#include <cstdint>

constexpr uint32_t kMaxSpellLightModels = 32;
constexpr uint32_t kMaxSpellGlowTrackKeys = 16;
constexpr uint32_t kSpellGlowFullParticles = 8;
constexpr float kSpellLightBaseReach = 3.0f;
constexpr float kSpellLightReachPerExtent = 3.0f;
constexpr float kSpellLightMaxReach = 20.0f;
constexpr float kSpellGlowMaxExtent = 50.0f;
constexpr uint32_t kSpellTintCacheEntries = 256;

namespace engine
{
constexpr uintptr_t kParticleEmitterRender = 0x0097EA60;
constexpr uintptr_t kSingleParticleRenderSite = 0x00821633;
constexpr uintptr_t kBatchedParticleRenderSite = 0x0082143F;
constexpr uintptr_t kM2BatchDraw = 0x008203B0;
constexpr uintptr_t kM2BatchDrawSite = 0x00823A83;

constexpr uintptr_t kSceneRenderCurrentElement = 0x50;
constexpr uintptr_t kSceneRenderData = 0x48;
constexpr uintptr_t kSceneRenderCurrentModel = 0x60;
constexpr uintptr_t kSceneRenderCurrentBatch = 0x88;
constexpr uintptr_t kSceneRenderCurrentSkinSection = 0x90;
constexpr uintptr_t kSceneRenderCurrentMaterial = 0x98;
constexpr uintptr_t kBatchedDrawElements = 0x0C;
constexpr uintptr_t kBatchedDrawIndexCursor = 0x10;
constexpr uintptr_t kM2ElementSize = 0x44;
constexpr uintptr_t kM2ElementModel = 0x04;
constexpr uintptr_t kM2ElementAlpha = 0x0C;
constexpr uintptr_t kM2ElementEmitter = 0x18;
constexpr uintptr_t kM2ModelScene = 0x28;
constexpr uintptr_t kM2ModelShared = 0x2C;
constexpr uintptr_t kM2SharedFileName = 0x3C;
constexpr uintptr_t kM2ModelBoneMatrices = 0x98;
constexpr uintptr_t kM2ModelColors = 0xA0;
constexpr uintptr_t kM2ModelTextures = 0xA4;
constexpr uintptr_t kM2ModelDiffuseColor = 0x1A0;
constexpr uintptr_t kM2SceneInverseView = 0xC4;
constexpr uintptr_t kM2DataBoneCount = 0x2C;
constexpr uintptr_t kM2DataColorCount = 0x48;
constexpr uintptr_t kM2DataTextureCount = 0x50;
constexpr uintptr_t kM2DataBoneCombos = 0x7C;
constexpr uintptr_t kM2DataTextureComboCount = 0x80;
constexpr uintptr_t kM2DataTextureCombos = 0x84;
constexpr uintptr_t kM2BatchColorIndex = 0x08;
constexpr uintptr_t kM2BatchTextureComboIndex = 0x10;
constexpr uintptr_t kM2MaterialBlend = 0x02;
constexpr uintptr_t kM2SkinSectionBoneComboIndex = 0x0E;
constexpr uintptr_t kM2SkinSectionBoundsCenter = 0x20;
constexpr uintptr_t kM2SkinSectionRadius = 0x2C;
constexpr uintptr_t kM2ModelColorSize = 0x20;
constexpr uintptr_t kM2ModelColorValue = 0x08;
constexpr uintptr_t kMatrixBytes = 0x40;
constexpr uintptr_t kTextureGxTexture = 0x44;
constexpr uintptr_t kGxTextureDeviceTexture = 0x38;
constexpr uint32_t kM2MeshBatchElement = 0;
constexpr uint32_t kM2OpaqueAdditiveBlend = 3;
constexpr uint32_t kM2AdditiveBlend = 4;
constexpr uintptr_t kEmitterRenderedParticles = 0x1C;
constexpr uintptr_t kEmitterMaterialBlend = 0xD0;
constexpr uintptr_t kEmitterColorTrack = 0xD8;
constexpr uintptr_t kEmitterAlphaTrack = 0xDC;
constexpr uintptr_t kEmitterReplacementColors = 0xF4;
constexpr uintptr_t kEmitterTexture = 0x128;
constexpr uintptr_t kEmitterAlphaScale = 0x130;
constexpr uintptr_t kEmitterFlags = 0x134;
constexpr uintptr_t kEmitterWorldBounds = 0x218;
constexpr uintptr_t kPartTrackValueCount = 0x08;
constexpr uintptr_t kPartTrackValues = 0x0C;
constexpr uint32_t kEmitterReplacesColors = 0x10;
constexpr uint32_t kReplacementColorCount = 3;
constexpr uint32_t kAdditiveParticleBlend = 3;
constexpr uint32_t kOpaqueAdditiveParticleBlend = 10;
constexpr float kParticleColorScale = 1.0f / 255.0f;
constexpr float kFixed16One = 32767.0f;

const ClientCodeGuard* SpellLightGuards(size_t& count);
bool SpellLightCodeMatches(const ClientCodeView& code);
}

struct SpellGlow
{
    uintptr_t model = 0;
    float boundsMin[3] = {};
    float boundsMax[3] = {};
    float color[3] = {};
    float strength = 0.0f;
    uintptr_t texture = 0;
    bool textureAlphaWeighted = false;
};

using TextureTintReader = bool (*)(uintptr_t texture, bool alphaWeighted, float tint[3]);

class SpellLightCollector
{
public:
    void UseTextureTints(TextureTintReader reader);
    void Add(const SpellGlow& glow);
    void Publish();
    void Clear();
    uint32_t Gathering() const { return m_gatheringCount; }
    uint32_t Count() const { return m_publishedCount; }
    bool Light(uint32_t index, float intensity, LocalPointLight& out) const;

private:
    struct CachedTint
    {
        uintptr_t texture = 0;
        bool alphaWeighted = false;
        bool known = false;
        float tint[3] = {};
    };

    const CachedTint& CachedTintFor(const SpellGlow& glow);
    void Tint(SpellGlow& glow);

    TextureTintReader m_tintReader = nullptr;
    CachedTint m_tints[kSpellTintCacheEntries] = {};
    uint32_t m_tintCount = 0;
    SpellGlow m_gathering[kMaxSpellLightModels] = {};
    uint32_t m_gatheringCount = 0;
    SpellGlow m_published[kMaxSpellLightModels] = {};
    uint32_t m_publishedCount = 0;
};

namespace engine
{
bool ReadSpellGlow(uintptr_t emitter, uintptr_t element, uintptr_t worldScene, SpellGlow& out);
bool ObserveSingleParticleRender(SpellLightCollector& lights, uintptr_t emitter, uintptr_t sceneRender,
                                 uintptr_t worldSceneSlot);
bool ObserveBatchedParticleRender(SpellLightCollector& lights, uintptr_t emitter, uintptr_t callerFrame,
                                  uintptr_t worldSceneSlot);
bool ReadSpellMeshGlow(uintptr_t sceneRender, uintptr_t worldScene, SpellGlow& out);
bool ObserveMeshBatchDraw(SpellLightCollector& lights, uintptr_t sceneRender, uintptr_t worldSceneSlot);
uint32_t AddSpellLights(LocalLightInputs& out, const SpellLightCollector& lights, const LocalLightCamera& camera,
                        const PointLightUpload& upload);
}

SpellLightCollector& GlobalSpellLights();
