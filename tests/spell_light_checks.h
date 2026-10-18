#pragma once

namespace spell_light_checks
{
using local_light_stabiliser_checks::CaptureInView;
using local_light_stabiliser_checks::ForwardCamera;
using local_light_stabiliser_checks::kStabiliserFrameSeconds;
using local_light_stabiliser_checks::StabiliserLight;
using local_light_stabiliser_checks::StepFrame;
using local_light_stabiliser_checks::Uploads;

constexpr uint32_t kEmitterWords = 0x240 / 4;
constexpr uint32_t kElementCount = 3;
constexpr uint32_t kSpellElement = 2;
constexpr uint32_t kModelWords = 0x30 / 4;
constexpr uint32_t kSharedWords = 0x150 / 4;
constexpr uint32_t kTrackWords = 4;
constexpr uint32_t kSceneRenderWords = 0x60 / 4;
constexpr uint32_t kBatchedFrameWords = 0x14 / 4;
constexpr uint32_t kFullParticles = 8;
constexpr float kGlowTolerance = 1e-4f;
constexpr float kFrontDepth = 10.0f;
constexpr float kBehindDepth = -50.0f;
constexpr double kSpellFadeCheckSeconds = 0.2;
constexpr uintptr_t kThunkFrameArgument = 0x1111;
constexpr uintptr_t kThunkBufferArgument = 0x2222;
constexpr uintptr_t kThunkFirstArgument = 1;
constexpr int16_t kFullAlpha = 32767;
constexpr int16_t kHalfAlpha = 16384;
constexpr char kSpellModelName[] = "Spells\\Fireball_Missile_High.m2";
constexpr char kCreatureModelName[] = "Creature\\Wisp\\Wisp.m2";
constexpr uint16_t kDxt1Blue = 0x001F;
constexpr uint16_t kDxt1DarkBlue = 0x0010;
constexpr uint16_t kDxt1Red = 0xF800;
constexpr uint32_t kAllThirdIndex = 0xFFFFFFFFu;
constexpr uintptr_t kBlueTexture = 0x5550;
constexpr float kMeshAlpha = 0.8f;
constexpr float kBoneScale = 2.0f;
constexpr float kSkinRadius = 1.5f;
constexpr float kViewOrigin[3] = {100.0f, 200.0f, 300.0f};
constexpr float kBoneOrigin[3] = {1.0f, 2.0f, 3.0f};
constexpr uint32_t kOpaqueTealTexel = 0xFF0080FFu;

struct FakeSpellScene
{
    std::vector<uint32_t> emitter = std::vector<uint32_t>(kEmitterWords);
    std::vector<uint32_t> elements = std::vector<uint32_t>(kElementCount * engine::kM2ElementSize / 4);
    std::vector<uint32_t> model = std::vector<uint32_t>(kModelWords);
    std::vector<uint32_t> shared = std::vector<uint32_t>(kSharedWords);
    std::vector<uint32_t> colorTrack = std::vector<uint32_t>(kTrackWords);
    std::vector<uint32_t> alphaTrack = std::vector<uint32_t>(kTrackWords);
    std::vector<float> colors = {255.0f, 128.0f, 0.0f, 255.0f, 64.0f, 0.0f};
    std::vector<int16_t> alphas = {kFullAlpha, kHalfAlpha};
    std::vector<uint32_t> sceneRender = std::vector<uint32_t>(kSceneRenderWords);
    std::vector<uint32_t> batchedFrame = std::vector<uint32_t>(kBatchedFrameWords);
    uint32_t indices[kElementCount] = {0, kSpellElement, 1};
    uint32_t worldSceneObject = 0;
    uintptr_t worldSceneSlot = 0;

    FakeSpellScene()
    {
        worldSceneSlot = WorldScene();
        Put(emitter, engine::kEmitterRenderedParticles, kFullParticles);
        Put(emitter, engine::kEmitterMaterialBlend, engine::kAdditiveParticleBlend);
        Put(emitter, engine::kEmitterColorTrack, Address(colorTrack));
        Put(emitter, engine::kEmitterAlphaTrack, Address(alphaTrack));
        Put(emitter, engine::kEmitterAlphaScale, 1.0f);
        const float bounds[6] = {10.0f, 20.0f, 30.0f, 12.0f, 24.0f, 32.0f};
        for (int i = 0; i < 6; ++i)
            Put(emitter, engine::kEmitterWorldBounds + i * sizeof(float), bounds[i]);
        Put(colorTrack, engine::kPartTrackValueCount, 2u);
        Put(colorTrack, engine::kPartTrackValues, reinterpret_cast<uintptr_t>(colors.data()));
        Put(alphaTrack, engine::kPartTrackValueCount, 2u);
        Put(alphaTrack, engine::kPartTrackValues, reinterpret_cast<uintptr_t>(alphas.data()));
        Put(model, engine::kM2ModelScene, WorldScene());
        Put(model, engine::kM2ModelShared, Address(shared));
        Name(kSpellModelName);
        Put(elements, kSpellElement * engine::kM2ElementSize + engine::kM2ElementModel, Address(model));
        Put(elements, kSpellElement * engine::kM2ElementSize + engine::kM2ElementEmitter, Emitter());
        Put(sceneRender, engine::kSceneRenderCurrentElement, Element());
        Put(batchedFrame, engine::kBatchedDrawElements, Address(elements));
        Put(batchedFrame, engine::kBatchedDrawIndexCursor, reinterpret_cast<uintptr_t>(&indices[1]));
    }

    template <typename T>
    static void Put(std::vector<uint32_t>& words, uintptr_t offset, T value)
    {
        std::memcpy(reinterpret_cast<unsigned char*>(words.data()) + offset, &value, sizeof(value));
    }
    static uintptr_t Address(const std::vector<uint32_t>& words) { return reinterpret_cast<uintptr_t>(words.data()); }
    void Name(const char* name)
    {
        std::memset(reinterpret_cast<unsigned char*>(shared.data()) + engine::kM2SharedFileName, 0,
                    shared.size() * 4 - engine::kM2SharedFileName);
        std::memcpy(reinterpret_cast<unsigned char*>(shared.data()) + engine::kM2SharedFileName, name,
                    std::strlen(name));
    }
    uintptr_t WorldScene() const { return reinterpret_cast<uintptr_t>(&worldSceneObject); }
    uintptr_t WorldSceneSlot() const { return reinterpret_cast<uintptr_t>(&worldSceneSlot); }
    uintptr_t Emitter() const { return Address(emitter); }
    uintptr_t Element() const { return Address(elements) + kSpellElement * engine::kM2ElementSize; }
    uintptr_t Model() const { return Address(model); }
};

bool GlowNear(float a, float b)
{
    return std::fabs(a - b) <= kGlowTolerance;
}

bool Reads(const FakeSpellScene& scene, SpellGlow& glow)
{
    return engine::ReadSpellGlow(scene.Emitter(), scene.Element(), scene.WorldScene(), glow);
}

bool Refused(const FakeSpellScene& scene)
{
    SpellGlow glow;
    return !Reads(scene, glow);
}

void CheckSpellGlowReading()
{
    FakeSpellScene scene;
    SpellGlow glow;
    const bool read = Reads(scene, glow);
    std::printf("     spell glow: colour %.3f %.3f %.3f, strength %.3f\n", glow.color[0], glow.color[1],
                glow.color[2], glow.strength);
    Check(read && glow.model == scene.Model() && GlowNear(glow.color[0], 1.0f) &&
              GlowNear(glow.color[1], 96.0f / 255.0f) && GlowNear(glow.color[2], 0.0f) &&
              GlowNear(glow.strength, (1.0f + kHalfAlpha / 32767.0f) / 2.0f) && glow.boundsMin[0] == 10.0f &&
              glow.boundsMax[1] == 24.0f && glow.boundsMax[2] == 32.0f,
          "an additive emitter of a Spells\\ model reads its mean track colour, its mean alpha and the world bounds "
          "the client gave its rendered particles");

    FakeSpellScene few;
    FakeSpellScene::Put(few.emitter, engine::kEmitterRenderedParticles, 2u);
    FakeSpellScene::Put(few.emitter, engine::kEmitterAlphaScale, 0.5f);
    SpellGlow faint;
    Check(Reads(few, faint) && GlowNear(faint.strength, glow.strength * 0.5f * 2.0f / kFullParticles),
          "a spell glow weakens with fewer rendered particles and with the model's alpha scale");

    FakeSpellScene tinted;
    FakeSpellScene::Put(tinted.emitter, engine::kEmitterFlags, engine::kEmitterReplacesColors);
    for (uint32_t key = 0; key < engine::kReplacementColorCount; ++key)
        FakeSpellScene::Put(tinted.emitter, engine::kEmitterReplacementColors + key * 12 + 8, 255.0f);
    SpellGlow blue;
    Check(Reads(tinted, blue) && GlowNear(blue.color[0], 0.0f) && GlowNear(blue.color[2], 1.0f),
          "an emitter with replacement colours glows in them instead of its track colours");

    FakeSpellScene creature;
    creature.Name(kCreatureModelName);
    FakeSpellScene alphaBlended;
    FakeSpellScene::Put(alphaBlended.emitter, engine::kEmitterMaterialBlend, 2u);
    FakeSpellScene opaqueAdd;
    FakeSpellScene::Put(opaqueAdd.emitter, engine::kEmitterMaterialBlend, engine::kOpaqueAdditiveParticleBlend);
    FakeSpellScene elsewhere;
    FakeSpellScene::Put(elsewhere.model, engine::kM2ModelScene, elsewhere.WorldScene() + 4);
    FakeSpellScene idle;
    FakeSpellScene::Put(idle.emitter, engine::kEmitterRenderedParticles, 0u);
    FakeSpellScene stranger;
    FakeSpellScene::Put(stranger.elements, kSpellElement * engine::kM2ElementSize + engine::kM2ElementEmitter,
                        stranger.Emitter() + 4);
    FakeSpellScene unbounded;
    FakeSpellScene::Put(unbounded.emitter, engine::kEmitterWorldBounds, std::numeric_limits<float>::max());
    SpellGlow added;
    Check(Refused(creature) && Refused(alphaBlended) && Reads(opaqueAdd, added) && Refused(elsewhere) &&
              Refused(idle) && Refused(stranger) && Refused(unbounded),
          "only additive emitters with rendered particles of a Spells\\ model in the world scene glow; the element "
          "must hold the emitter and the bounds must be set");

    void* reserved = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    SpellGlow unread;
    SpellLightCollector lights;
    const uintptr_t unmapped = reinterpret_cast<uintptr_t>(reserved);
    const bool survives = !engine::ReadSpellGlow(unmapped, unmapped, scene.WorldScene(), unread) &&
                          !engine::ReadSpellGlow(scene.Emitter(), 0, scene.WorldScene(), unread) &&
                          !engine::ObserveSingleParticleRender(lights, scene.Emitter(), unmapped,
                                                               scene.WorldSceneSlot()) &&
                          !engine::ObserveBatchedParticleRender(lights, scene.Emitter(), unmapped,
                                                                scene.WorldSceneSlot());
    if (reserved)
        VirtualFree(reserved, 0, MEM_RELEASE);
    Check(reserved && survives && lights.Gathering() == 0,
          "unreadable emitters, elements and draw frames are refused without a fault");

    const bool single =
        engine::ObserveSingleParticleRender(lights, scene.Emitter(), FakeSpellScene::Address(scene.sceneRender),
                                            scene.WorldSceneSlot());
    const bool batched =
        engine::ObserveBatchedParticleRender(lights, scene.Emitter(), FakeSpellScene::Address(scene.batchedFrame),
                                             scene.WorldSceneSlot());
    Check(single && batched && lights.Gathering() == 1,
          "the single draw finds the emitter's element in the scene render, the batched draw through its element "
          "array and index cursor, and both glows of one model gather into one light");
}

SpellGlow GlowAt(uintptr_t model, float x, float extent, float red, float blue, float strength)
{
    SpellGlow glow;
    glow.model = model;
    glow.boundsMin[0] = x - extent;
    glow.boundsMax[0] = x + extent;
    glow.boundsMin[2] = kFrontDepth - extent;
    glow.boundsMax[2] = kFrontDepth + extent;
    glow.color[0] = red;
    glow.color[2] = blue;
    glow.strength = strength;
    return glow;
}

void CheckSpellLightCollector()
{
    SpellLightCollector lights;
    lights.Add(GlowAt(1, 0.0f, 1.0f, 1.0f, 0.0f, 0.5f));
    lights.Add(GlowAt(1, 2.0f, 1.0f, 0.0f, 1.0f, 0.5f));
    const uint32_t beforePublish = lights.Count();
    lights.Publish();
    LocalPointLight light;
    const bool lit = lights.Light(0, 1.0f, light);
    Check(beforePublish == 0 && lights.Count() == 1 && lit && GlowNear(light.position[0], 1.0f) &&
              GlowNear(light.position[2], kFrontDepth) && GlowNear(light.color[0], 0.5f) &&
              GlowNear(light.color[2], 0.5f) && light.nativeId == 1 && light.source == LocalLightSource::Spell,
          "the emitters of one spell model become one light at the centre of their bounds, in their "
          "strength-weighted colour, published at the next frame");

    LocalPointLight brighter;
    LocalPointLight off;
    Check(lights.Light(0, 2.0f, brighter) && GlowNear(brighter.color[0], 1.0f) && !lights.Light(0, 0.0f, off),
          "SpellLightIntensity scales the light, and 0 makes none");

    SpellLightCollector point;
    point.Add(GlowAt(2, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f));
    point.Add(GlowAt(3, 0.0f, 10.0f, 1.0f, 0.0f, 4.0f));
    point.Publish();
    LocalPointLight pinpoint;
    LocalPointLight wide;
    point.Light(0, 1.0f, pinpoint);
    point.Light(1, 1.0f, wide);
    const float smallReach = engine::PointLightCutoff(pinpoint.color, pinpoint.attenuation);
    const float largeReach = engine::PointLightCutoff(wide.color, wide.attenuation);
    std::printf("     spell light reach: point %.2f yd, 10 yd glow %.2f yd\n", smallReach, largeReach);
    Check(std::fabs(smallReach - kSpellLightBaseReach) < 0.01f &&
              std::fabs(largeReach - kSpellLightMaxReach) < 0.01f && GlowNear(wide.color[0], 1.0f),
          "a full-strength spell light reaches 3 yd plus three times its glow's extent, at most 20 yd, and "
          "strength beyond 1 does not brighten it");

    point.Publish();
    SpellLightCollector crowded;
    for (uintptr_t model = 1; model <= kMaxSpellLightModels + 1; ++model)
        crowded.Add(GlowAt(model, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f));
    crowded.Publish();
    Check(point.Count() == 0 && crowded.Count() == kMaxSpellLightModels,
          "a frame without spell glows publishes none, and one frame gathers at most 32 spell models");
}

void CheckSpellLightsJoinTheCapture()
{
    const LocalLightCamera camera = ForwardCamera();
    SpellLightCollector lights;
    lights.Add(GlowAt(1, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f));
    SpellGlow behind = GlowAt(2, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f);
    behind.boundsMin[2] = kBehindDepth - 1.0f;
    behind.boundsMax[2] = kBehindDepth + 1.0f;
    lights.Add(behind);
    lights.Publish();

    LocalLightInputs inputs;
    const uint32_t added = engine::AddSpellLights(inputs, lights, camera, {false, 1.0f, 1.0f});
    LocalLightInputs off;
    const uint32_t none = engine::AddSpellLights(off, lights, camera, {false, 1.0f, 0.0f});
    LocalLightInputs refused;
    refused.capture = LocalLightCapture::UnsupportedClient;
    const uint32_t unsupported = engine::AddSpellLights(refused, lights, camera, {false, 1.0f, 1.0f});
    Check(added == 1 && inputs.pointLightCount == 1 && inputs.candidateCount == 1 &&
              inputs.pointLights[0].source == LocalLightSource::Spell && inputs.pointLights[0].nativeId == 1 &&
              none == 0 && off.pointLightCount == 0 && unsupported == 0,
          "spell lights in view join the captured point lights; SpellLights=0 or a refused client adds none");

    LocalLightStabiliser stabiliser;
    double seconds = 100.0;
    LocalLightInputs empty;
    StepFrame(stabiliser, camera, seconds, empty);
    LocalPointLight world = StabiliserLight(10, -2.0f, 0.0f, kFrontDepth, 1.0f);
    LocalPointLight spell = StabiliserLight(11, 2.0f, 0.0f, kFrontDepth, 1.0f);
    spell.source = LocalLightSource::Spell;
    float worldWeight = 0.0f;
    float spellWeight = 0.0f;
    for (double elapsed = 0.0; elapsed < kSpellFadeCheckSeconds; elapsed += kStabiliserFrameSeconds)
    {
        seconds += kStabiliserFrameSeconds;
        LocalLightInputs frame = CaptureInView({world, spell}, camera);
        StepFrame(stabiliser, camera, seconds, frame);
        Uploads(frame, 10, &worldWeight);
        Uploads(frame, 11, &spellWeight);
    }
    std::printf("     after %.1f s: spell light weight %.3f, world light weight %.3f\n", kSpellFadeCheckSeconds,
                spellWeight, worldWeight);
    Check(GlowNear(spellWeight, 1.0f) && worldWeight < 0.5f,
          "a spell light fades in within 0.15 s while a world light keeps its one-second fade");
}

void CheckSpellLightGuards()
{
    size_t count = 0;
    const ClientCodeGuard* guards = engine::SpellLightGuards(count);
    uintptr_t low = UINTPTR_MAX;
    uintptr_t high = 0;
    for (size_t i = 0; i < count; ++i)
    {
        low = std::min(low, guards[i].address);
        high = std::max(high, guards[i].address + guards[i].size);
    }
    std::vector<unsigned char> bytes(high - low, 0xCC);
    for (size_t i = 0; i < count; ++i)
        std::memcpy(&bytes[guards[i].address - low], guards[i].bytes, guards[i].size);
    const ClientCodeView view = {low, bytes.data(), bytes.size()};
    const bool matches = engine::SpellLightCodeMatches(view);
    bytes[guards[count - 1].address - low] ^= 0xFF;
    const bool changed = engine::SpellLightCodeMatches(view);
    Check(matches && !changed, "the spell light hooks need every guarded byte of the client's particle code");
}

struct ParticleRenderCall
{
    uintptr_t emitter = 0;
    uintptr_t frame = 0;
    uintptr_t buffer = 0;
    uintptr_t first = 0;
    int calls = 0;
};

ParticleRenderCall g_particleRenderCall;
uintptr_t g_thunkCallerEbp = 0;
uintptr_t g_thunkEsiAfter = 0;
uintptr_t g_thunkEbpAfter = 0;
uintptr_t g_thunkEspAfter = 0;
uintptr_t g_thunkEspExpected = 0;

void __fastcall FakeParticleRender(uintptr_t emitter, uintptr_t, uintptr_t frame, uintptr_t buffer, uintptr_t first)
{
    g_particleRenderCall.emitter = emitter;
    g_particleRenderCall.frame = frame;
    g_particleRenderCall.buffer = buffer;
    g_particleRenderCall.first = first;
    ++g_particleRenderCall.calls;
}

__declspec(naked) void __cdecl CallParticleRenderThunk(const void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                                       uintptr_t, uintptr_t)
{
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        push dword ptr [ebp + 32]
        push dword ptr [ebp + 28]
        push dword ptr [ebp + 24]
        mov eax, dword ptr [ebp + 8]
        mov ecx, dword ptr [ebp + 12]
        mov esi, dword ptr [ebp + 16]
        mov g_thunkCallerEbp, ebp
        mov ebp, dword ptr [ebp + 20]
        call eax
        mov g_thunkEsiAfter, esi
        mov g_thunkEbpAfter, ebp
        mov g_thunkEspAfter, esp
        mov ebp, g_thunkCallerEbp
        lea eax, [ebp - 12]
        mov g_thunkEspExpected, eax
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

bool ThunkKeptTheCallerState(uintptr_t esi, uintptr_t ebp)
{
    return g_thunkEsiAfter == esi && g_thunkEbpAfter == ebp && g_thunkEspAfter == g_thunkEspExpected;
}

bool RenderForwarded(uintptr_t emitter)
{
    return g_particleRenderCall.calls == 1 && g_particleRenderCall.emitter == emitter &&
           g_particleRenderCall.frame == kThunkFrameArgument && g_particleRenderCall.buffer == kThunkBufferArgument &&
           g_particleRenderCall.first == kThunkFirstArgument;
}

void CheckParticleRenderThunks()
{
    FakeSpellScene scene;
    const void* single = nullptr;
    const void* batched = nullptr;
    vf_test_particle_render_thunks(reinterpret_cast<uintptr_t>(&FakeParticleRender), scene.WorldSceneSlot(),
                                   &single, &batched);
    LocalPointLight published[kMaxSpellLightModels];
    vf_test_publish_spell_lights(published, kMaxSpellLightModels, 1.0f);

    const uintptr_t sceneRender = FakeSpellScene::Address(scene.sceneRender);
    g_particleRenderCall = {};
    CallParticleRenderThunk(single, scene.Emitter(), sceneRender, 0, kThunkFrameArgument, kThunkBufferArgument,
                            kThunkFirstArgument);
    const bool singleForwarded = RenderForwarded(scene.Emitter()) && ThunkKeptTheCallerState(sceneRender, 0);
    const uint32_t singleLights = vf_test_publish_spell_lights(published, kMaxSpellLightModels, 1.0f);

    const uintptr_t frame = FakeSpellScene::Address(scene.batchedFrame);
    g_particleRenderCall = {};
    CallParticleRenderThunk(batched, scene.Emitter(), 0, frame, kThunkFrameArgument, kThunkBufferArgument,
                            kThunkFirstArgument);
    const bool batchedForwarded = RenderForwarded(scene.Emitter()) && ThunkKeptTheCallerState(0, frame);
    const uint32_t batchedLights = vf_test_publish_spell_lights(published, kMaxSpellLightModels, 1.0f);
    std::printf("     particle render thunks: single %d light %u, batched %d light %u\n", singleForwarded,
                singleLights, batchedForwarded, batchedLights);
    Check(singleForwarded && batchedForwarded && singleLights == 1 && batchedLights == 1 &&
              published[0].nativeId == scene.Model() && published[0].source == LocalLightSource::Spell,
          "both particle render thunks forward the emitter and its three arguments, pop them, keep esi and ebp, and "
          "gather the drawn spell glow");
}

void PutDxt1Block(unsigned char* block, uint16_t first, uint16_t second, uint32_t indices)
{
    block[0] = static_cast<unsigned char>(first);
    block[1] = static_cast<unsigned char>(first >> 8);
    block[2] = static_cast<unsigned char>(second);
    block[3] = static_cast<unsigned char>(second >> 8);
    std::memcpy(block + 4, &indices, sizeof(indices));
}

bool TintNear(const float tint[3], float r, float g, float b)
{
    return std::fabs(tint[0] - r) < 0.02f && std::fabs(tint[1] - g) < 0.02f && std::fabs(tint[2] - b) < 0.02f;
}

void CheckTexelTints()
{
    unsigned char blue[8];
    PutDxt1Block(blue, kDxt1Blue, kDxt1DarkBlue, 0u);
    float dxt1[3] = {};
    const bool blueKnown = engine::AverageTexelTint(D3DFMT_DXT1, blue, 8, 4, 4, true, dxt1);

    unsigned char punched[8];
    PutDxt1Block(punched, kDxt1DarkBlue, kDxt1Blue, kAllThirdIndex);
    float hole[3] = {};
    const bool holeKnown = engine::AverageTexelTint(D3DFMT_DXT1, punched, 8, 4, 4, true, hole);

    unsigned char dxt5[16] = {};
    dxt5[0] = 255;
    dxt5[1] = 0;
    PutDxt1Block(dxt5 + 8, kDxt1Red, kDxt1Red, 0u);
    float red[3] = {};
    const bool redKnown = engine::AverageTexelTint(D3DFMT_DXT5, dxt5, 16, 4, 4, true, red);

    const unsigned char argb[8] = {0, 0, 255, 255, 0, 255, 0, 0};
    float weighted[3] = {};
    float unweighted[3] = {};
    const bool argbKnown = engine::AverageTexelTint(D3DFMT_A8R8G8B8, argb, 8, 2, 1, true, weighted) &&
                           engine::AverageTexelTint(D3DFMT_A8R8G8B8, argb, 8, 2, 1, false, unweighted);
    const unsigned char black[8] = {0, 0, 0, 255, 0, 0, 0, 255};
    float none[3] = {};
    const bool blackRefused = !engine::AverageTexelTint(D3DFMT_A8R8G8B8, black, 8, 2, 1, true, none);
    const bool formatRefused = !engine::AverageTexelTint(D3DFMT_L8, argb, 8, 2, 1, true, none);
    std::printf("     texel tints: dxt1 %.2f %.2f %.2f, dxt5 %.2f %.2f %.2f, argb weighted %.2f %.2f %.2f\n",
                dxt1[0], dxt1[1], dxt1[2], red[0], red[1], red[2], weighted[0], weighted[1], weighted[2]);
    Check(blueKnown && TintNear(dxt1, 0.0f, 0.0f, 1.0f) && !holeKnown && redKnown && TintNear(red, 1.0f, 0.0f, 0.0f) &&
              argbKnown && TintNear(weighted, 1.0f, 0.0f, 0.0f) && TintNear(unweighted, 1.0f, 1.0f, 0.0f) &&
              blackRefused && formatRefused,
          "a texture's tint is its mean texel colour scaled to a peak of 1, weighted by alpha for alpha-added "
          "glows; DXT1, DXT5 and A8R8G8B8 decode, and black, fully cut-out or unknown formats give no tint");
}

int g_tintReads = 0;

bool BlueTint(uintptr_t texture, bool, float tint[3])
{
    ++g_tintReads;
    if (texture != kBlueTexture)
        return false;
    tint[0] = 0.0f;
    tint[1] = 0.25f;
    tint[2] = 1.0f;
    return true;
}

void CheckTintedSpellLights()
{
    SpellLightCollector lights;
    lights.UseTextureTints(&BlueTint);
    g_tintReads = 0;
    SpellGlow white = GlowAt(1, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    white.color[1] = 1.0f;
    white.texture = kBlueTexture;
    lights.Add(white);
    lights.Add(white);
    SpellGlow untextured = GlowAt(2, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    untextured.color[1] = 1.0f;
    untextured.texture = kBlueTexture + 4;
    lights.Add(untextured);
    lights.Publish();
    LocalPointLight blue;
    LocalPointLight kept;
    const bool lit = lights.Light(0, 1.0f, blue) && lights.Light(1, 1.0f, kept);
    Check(lit && TintNear(blue.color, 0.0f, 0.25f, 1.0f) && TintNear(kept.color, 1.0f, 1.0f, 1.0f) &&
              g_tintReads == 2,
          "a white glow takes its texture's tint, a texture without one keeps the track colour, and each texture is "
          "read once");

    FakeSpellScene scene;
    std::vector<uint32_t> texture(0x48 / 4);
    std::vector<uint32_t> gxTexture(0x3C / 4);
    FakeSpellScene::Put(gxTexture, engine::kGxTextureDeviceTexture, kBlueTexture);
    FakeSpellScene::Put(texture, engine::kTextureGxTexture, FakeSpellScene::Address(gxTexture));
    FakeSpellScene::Put(scene.emitter, engine::kEmitterTexture, FakeSpellScene::Address(texture));
    SpellGlow added;
    const bool read = Reads(scene, added);
    FakeSpellScene::Put(scene.emitter, engine::kEmitterMaterialBlend, engine::kOpaqueAdditiveParticleBlend);
    SpellGlow opaque;
    const bool opaqueRead = Reads(scene, opaque);
    Check(read && added.texture == kBlueTexture && added.textureAlphaWeighted && opaqueRead &&
              opaque.texture == kBlueTexture && !opaque.textureAlphaWeighted,
          "a particle glow carries its emitter's device texture, weighted by alpha only for the alpha-added blend");
}

struct FakeMeshDraw
{
    std::vector<uint32_t> sceneRender = std::vector<uint32_t>(0xA0 / 4);
    std::vector<uint32_t> element = std::vector<uint32_t>(engine::kM2ElementSize / 4);
    std::vector<uint32_t> model = std::vector<uint32_t>(0x1B0 / 4);
    std::vector<uint32_t> shared = std::vector<uint32_t>(kSharedWords);
    std::vector<uint32_t> worldScene = std::vector<uint32_t>(0x110 / 4);
    std::vector<uint32_t> data = std::vector<uint32_t>(0x90 / 4);
    std::vector<uint32_t> bones = std::vector<uint32_t>(16);
    std::vector<uint32_t> colors = std::vector<uint32_t>(0x20 / 4);
    std::vector<uint32_t> textures = std::vector<uint32_t>(1);
    std::vector<uint32_t> texture = std::vector<uint32_t>(0x48 / 4);
    std::vector<uint32_t> gxTexture = std::vector<uint32_t>(0x3C / 4);
    std::vector<uint32_t> batch = std::vector<uint32_t>(0x18 / 4);
    std::vector<uint32_t> skin = std::vector<uint32_t>(0x30 / 4);
    std::vector<uint32_t> material = std::vector<uint32_t>(1);
    std::vector<uint16_t> boneCombos = {0};
    std::vector<uint16_t> textureCombos = {0};
    uintptr_t worldSceneSlot = 0;

    FakeMeshDraw()
    {
        using S = FakeSpellScene;
        worldSceneSlot = S::Address(worldScene);
        S::Put(sceneRender, engine::kSceneRenderData, S::Address(data));
        S::Put(sceneRender, engine::kSceneRenderCurrentElement, S::Address(element));
        S::Put(sceneRender, engine::kSceneRenderCurrentModel, S::Address(model));
        S::Put(sceneRender, engine::kSceneRenderCurrentBatch, S::Address(batch));
        S::Put(sceneRender, engine::kSceneRenderCurrentSkinSection, S::Address(skin));
        S::Put(sceneRender, engine::kSceneRenderCurrentMaterial, S::Address(material));
        S::Put(element, 0, engine::kM2MeshBatchElement);
        S::Put(element, engine::kM2ElementModel, S::Address(model));
        S::Put(element, engine::kM2ElementAlpha, kMeshAlpha);
        S::Put(model, engine::kM2ModelScene, S::Address(worldScene));
        S::Put(model, engine::kM2ModelShared, S::Address(shared));
        S::Put(model, engine::kM2ModelBoneMatrices, S::Address(bones));
        S::Put(model, engine::kM2ModelColors, S::Address(colors));
        S::Put(model, engine::kM2ModelTextures, S::Address(textures));
        for (int i = 0; i < 3; ++i)
            S::Put(model, engine::kM2ModelDiffuseColor + i * sizeof(float), 1.0f);
        std::memcpy(reinterpret_cast<unsigned char*>(shared.data()) + engine::kM2SharedFileName, kSpellModelName,
                    sizeof(kSpellModelName));
        PutMatrix(worldScene, engine::kM2SceneInverseView, 1.0f, kViewOrigin);
        PutMatrix(bones, 0, kBoneScale, kBoneOrigin);
        S::Put(data, engine::kM2DataBoneCount, 1u);
        S::Put(data, engine::kM2DataColorCount, 1u);
        S::Put(data, engine::kM2DataTextureCount, 1u);
        S::Put(data, engine::kM2DataBoneCombos, reinterpret_cast<uintptr_t>(boneCombos.data()));
        S::Put(data, engine::kM2DataTextureComboCount, 1u);
        S::Put(data, engine::kM2DataTextureCombos, reinterpret_cast<uintptr_t>(textureCombos.data()));
        const float trackColor[3] = {1.0f, 0.5f, 0.25f};
        for (int i = 0; i < 3; ++i)
            S::Put(colors, engine::kM2ModelColorValue + i * sizeof(float), trackColor[i]);
        S::Put(textures, 0, S::Address(texture));
        S::Put(texture, engine::kTextureGxTexture, S::Address(gxTexture));
        S::Put(gxTexture, engine::kGxTextureDeviceTexture, kBlueTexture);
        const float centre[3] = {0.5f, 0.0f, 0.0f};
        for (int i = 0; i < 3; ++i)
            S::Put(skin, engine::kM2SkinSectionBoundsCenter + i * sizeof(float), centre[i]);
        S::Put(skin, engine::kM2SkinSectionRadius, kSkinRadius);
        S::Put(material, engine::kM2MaterialBlend, static_cast<uint16_t>(engine::kM2AdditiveBlend));
    }

    static void PutMatrix(std::vector<uint32_t>& words, uintptr_t offset, float scale, const float origin[3])
    {
        float matrix[16] = {};
        for (int i = 0; i < 3; ++i)
        {
            matrix[i * 5] = scale;
            matrix[12 + i] = origin[i];
        }
        matrix[15] = 1.0f;
        std::memcpy(reinterpret_cast<unsigned char*>(words.data()) + offset, matrix, sizeof(matrix));
    }
    uintptr_t Render() const { return FakeSpellScene::Address(sceneRender); }
    uintptr_t Scene() const { return FakeSpellScene::Address(worldScene); }
    uintptr_t Slot() const { return reinterpret_cast<uintptr_t>(&worldSceneSlot); }
};

bool MeshRefused(const FakeMeshDraw& draw)
{
    SpellGlow glow;
    return !engine::ReadSpellMeshGlow(draw.Render(), draw.Scene(), glow);
}

void CheckSpellMeshGlows()
{
    FakeMeshDraw draw;
    SpellGlow glow;
    const bool read = engine::ReadSpellMeshGlow(draw.Render(), draw.Scene(), glow);
    float centre[3];
    float extentSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        centre[axis] = 0.5f * (glow.boundsMin[axis] + glow.boundsMax[axis]);
        const float half = 0.5f * (glow.boundsMax[axis] - glow.boundsMin[axis]);
        extentSquared += half * half;
    }
    std::printf("     mesh glow: centre %.2f %.2f %.2f, extent %.2f, colour %.2f %.2f %.2f, strength %.2f\n",
                centre[0], centre[1], centre[2], std::sqrt(extentSquared), glow.color[0], glow.color[1],
                glow.color[2], glow.strength);
    Check(read && glow.model == FakeSpellScene::Address(draw.model) && GlowNear(centre[0], 102.0f) &&
              GlowNear(centre[1], 202.0f) && GlowNear(centre[2], 303.0f) &&
              std::fabs(std::sqrt(extentSquared) - kSkinRadius * kBoneScale) < 1e-3f && GlowNear(glow.color[1], 0.5f) &&
              GlowNear(glow.color[2], 0.25f) && GlowNear(glow.strength, kMeshAlpha) &&
              glow.texture == kBlueTexture && glow.textureAlphaWeighted,
          "an additive mesh batch of a spell model glows at its skin section's bounds centre carried through its "
          "bone and the inverse view, as wide as the section's radius, in its batch colour, element alpha and "
          "texture");

    FakeMeshDraw alphaBlended;
    FakeSpellScene::Put(alphaBlended.material, engine::kM2MaterialBlend, uint16_t{2});
    FakeMeshDraw creature;
    std::memcpy(reinterpret_cast<unsigned char*>(creature.shared.data()) + engine::kM2SharedFileName,
                kCreatureModelName, sizeof(kCreatureModelName));
    FakeMeshDraw particle;
    FakeSpellScene::Put(particle.element, 0, 4u);
    FakeMeshDraw boneless;
    FakeSpellScene::Put(boneless.data, engine::kM2DataBoneCount, 0u);
    FakeMeshDraw faded;
    FakeSpellScene::Put(faded.element, engine::kM2ElementAlpha, 0.0f);
    SpellLightCollector lights;
    const bool observed = engine::ObserveMeshBatchDraw(lights, draw.Render(), draw.Slot());
    Check(MeshRefused(alphaBlended) && MeshRefused(creature) && MeshRefused(particle) && MeshRefused(boneless) &&
              MeshRefused(faded) && observed && lights.Gathering() == 1,
          "alpha-blended, non-spell, non-mesh, unbound or fully faded batches give no glow; an observed spell batch "
          "is gathered");
}

void __fastcall FakeMeshBatchDraw(uintptr_t sceneRender, uintptr_t)
{
    g_particleRenderCall.emitter = sceneRender;
    ++g_particleRenderCall.calls;
}

__declspec(naked) void __cdecl CallMeshBatchThunk(const void*, uintptr_t)
{
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov eax, dword ptr [ebp + 8]
        mov ecx, dword ptr [ebp + 12]
        call eax
        mov g_thunkEspAfter, esp
        lea eax, [ebp - 12]
        mov g_thunkEspExpected, eax
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

void CheckMeshBatchThunk()
{
    FakeMeshDraw draw;
    const void* single = nullptr;
    const void* batched = nullptr;
    vf_test_particle_render_thunks(reinterpret_cast<uintptr_t>(&FakeParticleRender), draw.Slot(), &single,
                                   &batched);
    const void* mesh = vf_test_mesh_batch_draw_thunk(reinterpret_cast<uintptr_t>(&FakeMeshBatchDraw));
    LocalPointLight published[kMaxSpellLightModels];
    vf_test_publish_spell_lights(published, kMaxSpellLightModels, 1.0f);
    g_particleRenderCall = {};
    CallMeshBatchThunk(mesh, draw.Render());
    const uint32_t lights = vf_test_publish_spell_lights(published, kMaxSpellLightModels, 1.0f);
    Check(g_particleRenderCall.calls == 1 && g_particleRenderCall.emitter == draw.Render() &&
              g_thunkEspAfter == g_thunkEspExpected && lights == 1 &&
              published[0].nativeId == FakeSpellScene::Address(draw.model),
          "the mesh batch thunk calls the client's batch draw with its scene renderer, returns with the stack as "
          "it was and gathers the drawn spell batch");
}

void CheckTextureTintOnTheDevice(IDirect3DDevice9* dev)
{
    IDirect3DTexture9* texture = nullptr;
    const bool created =
        dev && SUCCEEDED(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr));
    D3DLOCKED_RECT locked;
    if (created && SUCCEEDED(texture->LockRect(0, &locked, nullptr, 0)))
    {
        for (int y = 0; y < 4; ++y)
        {
            auto* row = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
            for (int x = 0; x < 4; ++x)
                row[x] = kOpaqueTealTexel;
        }
        texture->UnlockRect(0);
    }
    float tint[3] = {};
    const int known = created ? vf_test_texture_tint(reinterpret_cast<uintptr_t>(texture), 1, tint) : 0;
    const float notATexture[4] = {};
    float none[3] = {};
    const int refused = vf_test_texture_tint(reinterpret_cast<uintptr_t>(notATexture), 1, none);
    if (texture)
        texture->Release();
    std::printf("     device texture tint: %d (%.2f %.2f %.2f), plain memory %d\n", known, tint[0], tint[1], tint[2],
                refused);
    Check(created && known && TintNear(tint, 0.0f, 0.5f, 1.0f) && !refused,
          "a managed texture made through the device reads back its tint, and memory that is not a device texture "
          "is refused");
}

void CheckSpellLights()
{
    CheckSpellGlowReading();
    CheckSpellLightCollector();
    CheckSpellLightsJoinTheCapture();
    CheckSpellLightGuards();
    CheckParticleRenderThunks();
    CheckTexelTints();
    CheckTintedSpellLights();
    CheckSpellMeshGlows();
    CheckMeshBatchThunk();
}
}
