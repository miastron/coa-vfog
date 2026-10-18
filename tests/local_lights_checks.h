#pragma once

float InvalidLocalLightFloat()
{
    const uint32_t bits = 0x7FC00000;
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool LocalNear(float a, float b)
{
    return std::fabs(a - b) <= 1.0e-5f * std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
}

float UnitQuadraticReach()
{
    return std::sqrt(1.0f / kLocalPointLightContributionCutoff - 1.0f);
}

LocalPointLight PointLightFixture(float x)
{
    LocalPointLight light;
    light.position[0] = x;
    light.color[0] = 1.0f;
    light.color[1] = 0.5f;
    light.color[2] = 0.25f;
    light.attenuation[0] = 1.0f;
    light.attenuation[2] = 1.0f;
    return light;
}

LocalLightCamera ForwardViewCamera(float nearPlane, float farPlane)
{
    LocalLightCamera camera;
    for (int i = 0; i < 4; ++i)
        camera.cameraRelativeView[i * 4 + i] = 1.0f;
    camera.glProjection[0] = 1.0f;
    camera.glProjection[5] = 1.0f;
    camera.glProjection[10] = (farPlane + nearPlane) / (farPlane - nearPlane);
    camera.glProjection[11] = 1.0f;
    camera.glProjection[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
    return camera;
}

void CheckLocalLightInputs()
{
    LocalPointLight light = PointLightFixture(0.0f);
    const float quadraticRadius = engine::PointLightCutoff(light.color, light.attenuation);
    light.attenuation[1] = 2.0f;
    light.attenuation[2] = 0.0f;
    const float linearRadius = engine::PointLightCutoff(light.color, light.attenuation);
    light.attenuation[1] = 0.0f;
    const float constantRadius = engine::PointLightCutoff(light.color, light.attenuation);
    Check(LocalNear(quadraticRadius, UnitQuadraticReach()) &&
              LocalNear(linearRadius, (1.0f / kLocalPointLightContributionCutoff - 1.0f) / 2.0f) &&
              constantRadius == kMaxLocalPointLightRadius,
          "point-light bounds follow native quadratic, linear and constant attenuation");
    light.attenuation[0] = 1000.0f;
    const float dimRadius = engine::PointLightCutoff(light.color, light.attenuation);
    light.attenuation[0] = -1.0f;
    const float negativeRadius = engine::PointLightCutoff(light.color, light.attenuation);
    light.attenuation[0] = 1.0f;
    light.color[1] = InvalidLocalLightFloat();
    const float invalidRadius = engine::PointLightCutoff(light.color, light.attenuation);
    Check(dimRadius == 0.0f && negativeRadius == 0.0f && invalidRadius == 0.0f,
          "dim, negative and nonfinite point-light inputs cannot create a fog light");

    const float camera[3] = {};
    LocalLightInputs forwards;
    LocalLightInputs backwards;
    for (int i = 0; i < 16; ++i)
    {
        engine::SelectLocalPointLight(forwards, PointLightFixture(static_cast<float>(i)), camera, {});
        engine::SelectLocalPointLight(backwards, PointLightFixture(static_cast<float>(15 - i)), camera, {});
    }
    bool strongest = forwards.pointLightCount == kMaxLocalPointLights &&
                     backwards.pointLightCount == kMaxLocalPointLights;
    for (uint32_t i = 0; i < kMaxLocalPointLights; ++i)
        strongest = strongest && forwards.pointLights[i].position[0] == static_cast<float>(i) &&
                    backwards.pointLights[i].position[0] == static_cast<float>(i);
    bool pooled = forwards.candidateCount == 16 && backwards.candidateCount == 16;
    for (uint32_t i = 0; i < 16; ++i)
        pooled = pooled && forwards.candidates[i].position[0] == static_cast<float>(i) &&
                 backwards.candidates[i].position[0] == static_cast<float>(i);
    Check(strongest && pooled,
          "eight strongest nearby point lights are selected independently of native list order, and the ranked "
          "candidate pool keeps the ones behind them");
    light = PointLightFixture(40.0f);
    light.color[0] = 64.0f;
    const bool brighterSelected = engine::SelectLocalPointLight(forwards, light, camera, {});
    bool present = false;
    for (uint32_t i = 0; i < forwards.pointLightCount; ++i)
        present = present || forwards.pointLights[i].position[0] == 40.0f;
    Check(brighterSelected && present && forwards.pointLightCount == kMaxLocalPointLights,
          "point-light selection accounts for brightness as well as distance");
    light = PointLightFixture(1000.0f);
    const bool farSelected = engine::SelectLocalPointLight(forwards, light, camera, {});
    const bool farPooled = forwards.candidateCount == 18 && forwards.candidates[17].position[0] == 1000.0f &&
                           LocalNear(forwards.candidates[17].uploadedColor[0], 1.0f);
    light.position[0] = InvalidLocalLightFloat();
    const bool invalidSelected = engine::SelectLocalPointLight(forwards, light, camera, {});
    Check(!farSelected && farPooled && !invalidSelected && forwards.pointLightCount == kMaxLocalPointLights,
          "a far point light joins the candidate pool at full strength behind the nearer eight, and an invalid "
          "position is rejected");

    const LocalLightCamera ahead = ForwardViewCamera(1.0f, 1000.0f);
    LocalPointLight distant = PointLightFixture(0.0f);
    distant.position[2] = 900.0f;
    LocalPointLight beside = PointLightFixture(300.0f);
    beside.position[2] = 0.5f;
    LocalPointLight crossing = PointLightFixture(20.0f);
    crossing.position[2] = 10.0f;
    LocalLightInputs inView;
    LocalLightInputs unprojected;
    const bool distantSelected = engine::SelectLocalPointLightInView(inView, distant, ahead, {});
    const bool besideSelected = engine::SelectLocalPointLightInView(inView, beside, ahead, {});
    const bool crossingSelected = engine::SelectLocalPointLightInView(inView, crossing, ahead, {});
    const bool besideWithoutView = engine::SelectLocalPointLightInView(unprojected, beside, LocalLightCamera{}, {});
    Check(distantSelected && !besideSelected && crossingSelected && inView.pointLightCount == 2 &&
              inView.candidateCount == 2 && inView.pointLights[1].position[2] == 900.0f &&
              LocalNear(inView.pointLights[1].uploadedColor[0], 1.0f) &&
              LocalNear(inView.pointLights[1].cutoff, UnitQuadraticReach()) && besideWithoutView &&
              unprojected.pointLightCount == 1,
          "a point light is captured at full strength at any distance while its sphere meets the view, even with "
          "its centre outside, a sphere wholly outside the view is not a candidate, and a camera without a "
          "projection sees every light");

    LocalLightInputs crowded;
    for (int i = 0; i < 30; ++i)
    {
        LocalPointLight behind = PointLightFixture(static_cast<float>(i % 6) - 2.5f);
        behind.position[2] = -200.0f - static_cast<float>(i);
        behind.color[0] = 64.0f;
        engine::SelectLocalPointLightInView(crowded, behind, ahead, {});
    }
    engine::SelectLocalPointLightInView(crowded, distant, ahead, {});
    Check(crowded.candidateCount == 1 && crowded.pointLightCount == 1 && crowded.pointLights[0].position[2] == 900.0f,
          "thirty brighter lights wholly behind the camera cannot crowd a far light ahead out of the candidate pool");

    LocalLightInputs flat;
    LocalPointLight farFlat = PointLightFixture(0.0f);
    farFlat.position[2] = 1500.0f;
    farFlat.attenuation[0] = 0.0f;
    farFlat.attenuation[2] = 0.0f;
    LocalPointLight nearFlat = farFlat;
    nearFlat.position[0] = 10.0f;
    nearFlat.position[2] = 20.0f;
    engine::SelectLocalPointLightInView(flat, farFlat, ahead, {});
    engine::SelectLocalPointLightInView(flat, nearFlat, ahead, {});
    Check(flat.pointLightCount == 2 && flat.pointLights[0].position[2] == 20.0f &&
              flat.pointLights[0].cutoff == kMaxLocalPointLightRadius,
          "lights of equal priority rank by camera distance, so a constant-attenuation light 1500 yd ahead ranks "
          "behind one 20 yd ahead");

    bool clearedBoth = true;
    for (bool withPointLights : {true, false})
    {
        LocalLightInputs unsupported = forwards;
        unsupported.cameraInterior = true;
        unsupported.interiorBlend = 1.0f;
        const bool captured = engine::CaptureLocalLightInputs(LocalLightCamera{}, withPointLights, {}, unsupported);
        clearedBoth = clearedBoth && !captured && unsupported.pointLightCount == 0 &&
                      !unsupported.cameraInterior && unsupported.interiorBlend == 0.0f &&
                      unsupported.capture == LocalLightCapture::UnsupportedClient;
    }
    Check(clearedBoth,
          "native local-light acquisition rejects the harness image as an unsupported client and clears stale inputs "
          "whether or not the point-light walk is requested (the walk skip itself needs the client image)");
}

struct TablePointLight
{
    uintptr_t scene = 0;
    uint32_t frameStamp = 0;
    uint32_t type = 1;
    float position[3] = {};
    float viewPosition[3] = {};
    float direction[3] = {};
    float ambient[3] = {};
    float color[3] = {1.0f, 0.5f, 0.25f};
    float specular[3] = {};
    float attenuation[3] = {0.0f, 0.7f, 0.03f};
    uint32_t enabled = 1;
    uintptr_t previousLink = 0;
    uintptr_t next = 0;
};

static_assert(sizeof(TablePointLight) == 0x6C && offsetof(TablePointLight, color) == 0x3C &&
                  offsetof(TablePointLight, enabled) == 0x60 && offsetof(TablePointLight, next) == 0x68,
              "the table fixture follows the client's point-light layout recorded in README");

constexpr size_t kSceneBucketsWord = 0x24 / sizeof(uintptr_t);
constexpr size_t kTableBucketCount = 4096;
constexpr size_t kLitBucket = 1234;

struct PointLightTable
{
    uintptr_t sceneSlot = 0;
    uintptr_t scene[kSceneBucketsWord + 1] = {};
    uintptr_t buckets[kTableBucketCount] = {};
    TablePointLight lights[2];
};

void LinkPointLightTable(PointLightTable& table)
{
    table.sceneSlot = reinterpret_cast<uintptr_t>(table.scene);
    table.scene[kSceneBucketsWord] = reinterpret_cast<uintptr_t>(table.buckets);
    uintptr_t* link = &table.buckets[kLitBucket];
    float x = 10.0f;
    for (TablePointLight& light : table.lights)
    {
        light = {};
        light.position[0] = x;
        x += 10.0f;
        light.scene = table.sceneSlot;
        light.previousLink = reinterpret_cast<uintptr_t>(link);
        *link = reinterpret_cast<uintptr_t>(&light);
        link = &light.next;
    }
}

template <typename Table>
LocalLightCapture CaptureTable(Table& table, LocalLightInputs& out)
{
    const LocalLightCamera camera;
    return engine::CapturePointLightTable(reinterpret_cast<uintptr_t>(&table.sceneSlot), camera, {}, out);
}

bool CapturedAs(PointLightTable& table, LocalLightCapture expected, uint32_t lights)
{
    LocalLightInputs out;
    out.pointLightCount = 1;
    return CaptureTable(table, out) == expected && out.capture == expected && out.pointLightCount == lights;
}

constexpr size_t kTruncatedTableLights = 8192 + 1;
constexpr size_t kTruncatedTableBuckets = 32;

struct LongPointLightTable
{
    uintptr_t sceneSlot = 0;
    uintptr_t scene[kSceneBucketsWord + 1] = {};
    uintptr_t buckets[kTableBucketCount] = {};
    std::vector<TablePointLight> lights = std::vector<TablePointLight>(kTruncatedTableLights);
};

void LinkLongPointLightTable(LongPointLightTable& table)
{
    table.sceneSlot = reinterpret_cast<uintptr_t>(table.scene);
    table.scene[kSceneBucketsWord] = reinterpret_cast<uintptr_t>(table.buckets);
    uintptr_t* links[kTruncatedTableBuckets];
    for (size_t i = 0; i < kTruncatedTableBuckets; ++i)
        links[i] = &table.buckets[kLitBucket + i];
    for (size_t i = 0; i < table.lights.size(); ++i)
    {
        TablePointLight& light = table.lights[i];
        light = {};
        light.position[0] = 10.0f + 0.001f * static_cast<float>(i);
        light.scene = table.sceneSlot;
        uintptr_t*& link = links[i % kTruncatedTableBuckets];
        light.previousLink = reinterpret_cast<uintptr_t>(link);
        *link = reinterpret_cast<uintptr_t>(&light);
        link = &light.next;
    }
}

constexpr float kInteriorFixtureBlend = 0.75f;

struct CameraInteriorFixture
{
    uintptr_t instanceWord = 0;
    uint32_t groupCountWord = 0;
    uintptr_t groupIdsWord = 0;
    float fogBlendWord = kInteriorFixtureBlend;
    uint32_t ids[1] = {0};
    alignas(sizeof(uintptr_t)) unsigned char instance[kWmoInstanceRoot + sizeof(uintptr_t)] = {};
    alignas(sizeof(uintptr_t)) unsigned char root[kWmoRootGroups + sizeof(uintptr_t)] = {};
    alignas(sizeof(uintptr_t)) unsigned char group[kWmoGroupLoaded + sizeof(uint32_t)] = {};

    template <typename T>
    void Put(unsigned char* object, uintptr_t offset, T value)
    {
        std::memcpy(object + offset, &value, sizeof(value));
    }
    void Link()
    {
        instanceWord = reinterpret_cast<uintptr_t>(instance);
        groupCountWord = 1;
        groupIdsWord = reinterpret_cast<uintptr_t>(ids);
        Put(instance, kWmoInstanceRoot, reinterpret_cast<uintptr_t>(root));
        Put(root, kWmoRootLoaded, uint32_t{1});
        Put(root, kWmoRootGroupCount, uint32_t{1});
        Put(root, kWmoRootGroups, reinterpret_cast<uintptr_t>(group));
        Put(group, kWmoGroupLoaded, uint32_t{1});
        Put(group, kWmoGroupFlags, uint32_t{0});
    }
    CameraInteriorWords Words() const
    {
        return {reinterpret_cast<uintptr_t>(&instanceWord), reinterpret_cast<uintptr_t>(&groupCountWord),
                reinterpret_cast<uintptr_t>(&groupIdsWord), reinterpret_cast<uintptr_t>(&fogBlendWord)};
    }
};

LocalLightCapture CaptureWithInterior(PointLightTable& table, const CameraInteriorFixture& interior,
                                      LocalLightInputs& out)
{
    const LocalLightCamera camera;
    return engine::CaptureLightInputsFrom({reinterpret_cast<uintptr_t>(&table.sceneSlot), interior.Words()}, camera,
                                          true, {}, out);
}

void CheckInteriorInputsSurviveAnIncompleteWalk()
{
    auto table = std::make_unique<PointLightTable>();
    auto interior = std::make_unique<CameraInteriorFixture>();
    LinkPointLightTable(*table);
    interior->Link();
    LocalLightInputs complete;
    const bool whole = CaptureWithInterior(*table, *interior, complete) == LocalLightCapture::Captured &&
                       complete.pointLightCount == 2 && complete.cameraInterior &&
                       complete.interiorBlend == kInteriorFixtureBlend;

    table->lights[1].previousLink = reinterpret_cast<uintptr_t>(&table->buckets[kLitBucket]);
    LocalLightInputs damaged;
    const bool kept = CaptureWithInterior(*table, *interior, damaged) == LocalLightCapture::DamagedTable &&
                      damaged.pointLightCount == 1 && damaged.cameraInterior &&
                      damaged.interiorBlend == kInteriorFixtureBlend;

    LinkPointLightTable(*table);
    interior->ids[0] = 1;
    LocalLightInputs rejected;
    const bool exterior = CaptureWithInterior(*table, *interior, rejected) == LocalLightCapture::InteriorRejected &&
                          rejected.pointLightCount == 2 && !rejected.cameraInterior &&
                          rejected.interiorBlend == 0.0f;

    interior->ids[0] = 0;
    void* unmapped = VirtualAlloc(nullptr, 65536, MEM_RESERVE, PAGE_NOACCESS);
    interior->instanceWord = reinterpret_cast<uintptr_t>(unmapped);
    LocalLightInputs faulted;
    const bool fault = unmapped && CaptureWithInterior(*table, *interior, faulted) == LocalLightCapture::ReadFault &&
                       faulted.pointLightCount == 2 && !faulted.cameraInterior && faulted.interiorBlend == 0.0f;
    if (unmapped)
        VirtualFree(unmapped, 0, MEM_RELEASE);
    Check(whole && kept && exterior && fault,
          "the camera-interior inputs are read after the walk whatever it returned: kept beside the lights of a "
          "damaged table, exterior when their reads disagree, and exterior with the lights kept on a read fault");
}

void CheckPointLightTableCapture()
{
    auto table = std::make_unique<PointLightTable>();
    LinkPointLightTable(*table);
    LocalLightInputs walked;
    const LocalLightCapture captured = CaptureTable(*table, walked);
    Check(captured == LocalLightCapture::Captured && walked.capture == LocalLightCapture::Captured &&
              walked.pointLightCount == 2 && walked.candidateCount == 2 &&
              walked.pointLights[0].nativeId == reinterpret_cast<uintptr_t>(&table->lights[0]) &&
              walked.pointLights[1].nativeId == reinterpret_cast<uintptr_t>(&table->lights[1]) &&
              walked.pointLights[1].enabled == 1 && walked.pointLights[1].attenuation[2] == 0.03f &&
              walked.skippedDisabled == 0 && walked.skippedNonPoint == 0,
          "the point-light walk captures every enabled point light of a linked table with its address and inputs");

    table->lights[0].enabled = 0;
    LocalLightInputs disabledWalk;
    const bool disabled = CaptureTable(*table, disabledWalk) == LocalLightCapture::Captured &&
                          disabledWalk.pointLightCount == 1 && disabledWalk.skippedDisabled == 1 &&
                          disabledWalk.pointLights[0].nativeId == reinterpret_cast<uintptr_t>(&table->lights[1]);
    LinkPointLightTable(*table);
    table->lights[0].type = 0;
    LocalLightInputs nonPointWalk;
    const bool nonPoint = CaptureTable(*table, nonPointWalk) == LocalLightCapture::Captured &&
                          nonPointWalk.pointLightCount == 1 && nonPointWalk.skippedNonPoint == 1 &&
                          nonPointWalk.pointLights[0].nativeId == reinterpret_cast<uintptr_t>(&table->lights[1]);
    Check(disabled && nonPoint,
          "a disabled or non-point node is skipped and counted, and the walk goes on to the lights behind it");

    LinkPointLightTable(*table);
    table->lights[1].previousLink = reinterpret_cast<uintptr_t>(&table->buckets[kLitBucket]);
    LocalLightInputs damagedWalk;
    const bool damaged = CaptureTable(*table, damagedWalk) == LocalLightCapture::DamagedTable &&
                         damagedWalk.capture == LocalLightCapture::DamagedTable && damagedWalk.pointLightCount == 1 &&
                         damagedWalk.pointLights[0].nativeId == reinterpret_cast<uintptr_t>(&table->lights[0]);
    Check(damaged, "a broken back link stops the walk as a damaged table but keeps the lights walked before it");

    auto longTable = std::make_unique<LongPointLightTable>();
    LinkLongPointLightTable(*longTable);
    LocalLightInputs truncatedWalk;
    const LocalLightCapture truncated = CaptureTable(*longTable, truncatedWalk);
    Check(truncated == LocalLightCapture::TableTruncated &&
              truncatedWalk.capture == LocalLightCapture::TableTruncated &&
              truncatedWalk.pointLightCount == kMaxLocalPointLights &&
              truncatedWalk.candidateCount == kLocalLightCandidatePool,
          "a table beyond 8192 nodes is truncated, not rejected: the lights walked so far stay usable");

    void* unmapped = VirtualAlloc(nullptr, 65536, MEM_RESERVE, PAGE_NOACCESS);
    LinkPointLightTable(*table);
    table->sceneSlot = reinterpret_cast<uintptr_t>(unmapped);
    const bool fault = unmapped && CapturedAs(*table, LocalLightCapture::ReadFault, 0);
    if (unmapped)
        VirtualFree(unmapped, 0, MEM_RELEASE);
    table->sceneSlot = 0;
    LocalLightInputs noScene;
    const bool empty = CaptureTable(*table, noScene) == LocalLightCapture::Captured && noScene.pointLightCount == 0;
    Check(fault && empty,
          "a scene pointer into unmapped memory is a read fault with no lights, and no scene is an empty capture");
}

constexpr size_t kSceneFrameStampWord = 0x14 / sizeof(uintptr_t);
constexpr uint32_t kDrawnFrame = 100;
constexpr float kObjectDepth = 50.0f;
constexpr float kBehindObjectDepth = -20.0f;

uint32_t CapturedWithStamps(PointLightTable& table, uint32_t secondStamp, float secondDepth, bool follow,
                            LocalLightInputs& out)
{
    LinkPointLightTable(table);
    table.scene[kSceneFrameStampWord] = kDrawnFrame;
    table.lights[0].frameStamp = kDrawnFrame;
    table.lights[0].position[2] = kObjectDepth;
    table.lights[1].frameStamp = secondStamp;
    table.lights[1].position[2] = secondDepth;
    const LocalLightCamera camera = ForwardViewCamera(1.0f, 1000.0f);
    PointLightUpload upload;
    upload.followObjects = follow;
    out = {};
    engine::CapturePointLightTable(reinterpret_cast<uintptr_t>(&table.sceneSlot), camera, upload, out);
    return out.pointLightCount;
}

void CheckLightsFollowTheirObjects()
{
    auto table = std::make_unique<PointLightTable>();
    LocalLightInputs hidden;
    const uint32_t followed = CapturedWithStamps(*table, kDrawnFrame - 10, kObjectDepth, true, hidden);
    LocalLightInputs unfollowed;
    const uint32_t everyLight = CapturedWithStamps(*table, kDrawnFrame - 10, kObjectDepth, false, unfollowed);
    LocalLightInputs recent;
    const uint32_t recentlyDrawn = CapturedWithStamps(*table, kDrawnFrame - 2, kObjectDepth, true, recent);
    LocalLightInputs offView;
    const uint32_t outsideView = CapturedWithStamps(*table, kDrawnFrame - 10, kBehindObjectDepth, true, offView);
    Check(followed == 1 && hidden.skippedHidden == 1 &&
              hidden.pointLights[0].nativeId == reinterpret_cast<uintptr_t>(&table->lights[0]) &&
              everyLight == 2 && unfollowed.skippedHidden == 0 && recentlyDrawn == 2 && outsideView == 2 &&
              offView.skippedHidden == 0,
          "a light in view whose model the client has not drawn for more than two frames is skipped and counted; "
          "LightsFollowObjects=0, a model drawn two frames ago or a light centre outside the view keeps it");
}

constexpr float kStatueGreen = 255.0f;
constexpr float kStatueRed = 65.9f;
constexpr float kLinearGamma = 2.2f;

LocalPointLight ColouredLightFixture(float x, float red, float green, float blue)
{
    LocalPointLight light = PointLightFixture(x);
    light.color[0] = red;
    light.color[1] = green;
    light.color[2] = blue;
    return light;
}

bool UploadedColourIs(const LocalPointLight& light, const PointLightUpload& upload, const float (&expected)[3])
{
    float uploaded[3];
    engine::UploadedPointLightColor(light.color, upload, uploaded);
    bool matches = true;
    for (int channel = 0; channel < 3; ++channel)
        matches = matches && LocalNear(uploaded[channel], expected[channel]);
    if (!matches)
        std::printf("     uploaded colour %.6g %.6g %.6g, expected %.6g %.6g %.6g\n", uploaded[0], uploaded[1],
                    uploaded[2], expected[0], expected[1], expected[2]);
    return matches;
}

void CheckHdrLocalLightColours()
{
    const PointLightUpload linear = {true, 1.0f};
    const PointLightUpload gamma = {false, 1.0f};
    const LocalPointLight statue = ColouredLightFixture(0.0f, kStatueRed, kStatueGreen, kStatueGreen);
    const float statueChroma = std::pow(kStatueRed / kStatueGreen, kLinearGamma) * kStatueGreen;
    Check(UploadedColourIs(statue, linear, {statueChroma, kStatueGreen, kStatueGreen}),
          "an HDR point light linearises its chromaticity and keeps its peak intensity linear (255, not 255^2.2)");
    const LocalPointLight halfStatue =
        ColouredLightFixture(0.0f, kStatueRed * 0.5f, kStatueGreen * 0.5f, kStatueGreen * 0.5f);
    Check(UploadedColourIs(halfStatue, linear, {statueChroma * 0.5f, kStatueGreen * 0.5f, kStatueGreen * 0.5f}),
          "halving an HDR light's intensity halves its uploaded colour");
    const LocalPointLight torch = ColouredLightFixture(0.0f, 1.0f, 0.5f, 0.25f);
    Check(UploadedColourIs(torch, linear, {1.0f, std::pow(0.5f, kLinearGamma), std::pow(0.25f, kLinearGamma)}) &&
              UploadedColourIs(torch, {true, 2.0f},
                               {2.0f, 2.0f * std::pow(0.5f, kLinearGamma), 2.0f * std::pow(0.25f, kLinearGamma)}),
          "a point light no brighter than 1 keeps the gamma 2.2 decode, times LocalLightIntensity");
    Check(UploadedColourIs(statue, gamma, {kStatueRed, kStatueGreen, kStatueGreen}),
          "with ColorSpace=0 the captured colour is uploaded unchanged");

    const float camera[3] = {};
    const LocalPointLight dim = ColouredLightFixture(0.0f, 0.5f, 0.5f, 0.5f);
    LocalLightInputs linearDim;
    LocalLightInputs gammaDim;
    engine::SelectLocalPointLight(linearDim, dim, camera, linear);
    engine::SelectLocalPointLight(gammaDim, dim, camera, gamma);
    const float linearPeak = std::pow(0.5f, kLinearGamma);
    Check(linearDim.pointLightCount == 1 && gammaDim.pointLightCount == 1 &&
              LocalNear(linearDim.pointLights[0].cutoff,
                        std::sqrt(linearPeak / kLocalPointLightContributionCutoff - 1.0f)) &&
              LocalNear(gammaDim.pointLights[0].cutoff, std::sqrt(0.5f / kLocalPointLightContributionCutoff - 1.0f)) &&
              LocalNear(linearDim.pointLights[0].uploadedColor[0], linearPeak),
          "a point light's reach is cut off at 1/256 of the colour it uploads, not of the captured gamma colour");

    const LocalPointLight nearGrey = ColouredLightFixture(0.0f, 0.9f, 0.9f, 0.9f);
    const LocalPointLight fartherBright = ColouredLightFixture(1.163f, 2.0f, 2.0f, 2.0f);
    LocalLightInputs linearOrder;
    LocalLightInputs gammaOrder;
    for (const LocalPointLight* light : {&nearGrey, &fartherBright})
    {
        engine::SelectLocalPointLight(linearOrder, *light, camera, linear);
        engine::SelectLocalPointLight(gammaOrder, *light, camera, gamma);
    }
    Check(linearOrder.pointLightCount == 2 && gammaOrder.pointLightCount == 2 &&
              linearOrder.pointLights[0].position[0] == fartherBright.position[0] &&
              gammaOrder.pointLights[0].position[0] == nearGrey.position[0],
          "point lights are ranked by the colour they upload: in linear light a farther HDR light outranks a nearer "
          "grey one that is brighter in gamma");

    LocalLightInputs unlit;
    Check(!engine::SelectLocalPointLight(unlit, torch, camera, {true, 0.0f}) && unlit.pointLightCount == 0,
          "LocalLightIntensity=0 uploads no point light");
}

FrameInputs InteriorFogFixture()
{
    FrameInputs input = {};
    input.farClip = 1000.0f;
    input.fogStart = 60.0f;
    input.fogEnd = 600.0f;
    input.zoneFogDistance = 600.0f;
    input.toLight[2] = 1.0f;
    input.fogColor = 0xFF4080C0;
    input.directColor = 0xFFFFFFFF;
    input.sunColor = 0xFFFFFFFF;
    return input;
}

void CheckInteriorFogInputs()
{
    Config config;
    FrameInputs input = InteriorFogFixture();
    const FogParams outside = BuildFogParams(input, config, nullptr);
    input.localLights.interiorBlend = 1.0f;
    const FogParams unclassified = BuildFogParams(input, config, nullptr);
    input.localLights.cameraInterior = true;
    input.localLights.interiorBlend = 0.0f;
    const FogParams threshold = BuildFogParams(input, config, nullptr);
    Check(LocalNear(outside.layers[0].density, unclassified.layers[0].density) &&
              LocalNear(outside.layers[0].density, threshold.layers[0].density) &&
              LocalNear(outside.layers[0].diffuse[0], unclassified.layers[0].diffuse[0]),
          "outdoor fog is unchanged without interior membership or at the doorway threshold");

    input.localLights.interiorBlend = 0.5f;
    const FogParams middle = BuildFogParams(input, config, nullptr);
    input.localLights.interiorBlend = 1.0f;
    const FogParams inside = BuildFogParams(input, config, nullptr);
    bool fades = true;
    for (int i = 0; i < kSceneLayers; ++i)
        fades = fades && LocalNear(middle.layers[i].density, outside.layers[i].density * 0.575f) &&
                LocalNear(inside.layers[i].density, outside.layers[i].density * 0.15f);
    for (int i = 0; i < kFogLayers; ++i)
        for (int channel = 0; channel < 3; ++channel)
            fades = fades && LocalNear(middle.layers[i].diffuse[channel], outside.layers[i].diffuse[channel] * 0.5f) &&
                    inside.layers[i].diffuse[channel] == 0.0f;
    Check(fades && inside.lightVisibility == 0.0f && LocalNear(middle.lightVisibility, 0.5f),
          "interior transitions continuously reduce outdoor density and direct sunlight");
    Check(LocalNear(inside.layers[kDistanceFogLayer].density, outside.layers[kDistanceFogLayer].density) &&
              LocalNear(middle.layers[kDistanceFogLayer].density, outside.layers[kDistanceFogLayer].density) &&
              LocalNear(inside.layers[kDistanceFogLayer].emissive[2], outside.layers[kDistanceFogLayer].emissive[2]) &&
              inside.farLimit == input.fogEnd,
          "interior transitions preserve the native WMO distance-fog colour and range");

    for (float retained : {0.0f, 0.15f, 0.4f, 1.0f, -1.0f, 2.0f, InvalidLocalLightFloat()})
    {
        config.interiorFarFog = retained;
        const float expected = std::isfinite(retained) ? std::clamp(retained, 0.0f, 1.0f) : 1.0f;
        input.localLights.interiorBlend = 1.0f;
        const FogParams full = BuildFogParams(input, config, nullptr);
        input.localLights.interiorBlend = 0.5f;
        const FogParams half = BuildFogParams(input, config, nullptr);
        Check(LocalNear(full.layers[kDistanceFogLayer].density,
                        outside.layers[kDistanceFogLayer].density * expected) &&
                  LocalNear(half.layers[kDistanceFogLayer].density,
                            outside.layers[kDistanceFogLayer].density * (1.0f + expected) * 0.5f) &&
                  LocalNear(full.layers[0].density, inside.layers[0].density),
              "InteriorFarFog clamps, fades continuously and leaves scene-layer density independent");
    }
    config.interiorFarFog = 0.4f;
    input.localLights.interiorBlend = 1.0f;
    config.interiorAware = false;
    const FogParams disabled = BuildFogParams(input, config, nullptr);
    config.interiorAware = true;
    input.localLights.interiorBlend = InvalidLocalLightFloat();
    const FogParams invalid = BuildFogParams(input, config, nullptr);
    Check(LocalNear(disabled.layers[0].density, outside.layers[0].density) &&
              LocalNear(disabled.layers[0].diffuse[0], outside.layers[0].diffuse[0]) &&
              LocalNear(invalid.layers[0].density, outside.layers[0].density) &&
              LocalNear(disabled.layers[kDistanceFogLayer].density, outside.layers[kDistanceFogLayer].density) &&
              LocalNear(invalid.layers[kDistanceFogLayer].density, outside.layers[kDistanceFogLayer].density),
          "interior bypass and nonfinite transition weights preserve outdoor fog");

    config.interiorFarFog = 1.0f;
    AuthoredFog authored = {};
    authored.layerCount = 1;
    authored.coverage = 1.0f;
    authored.layers[0].density = 1.0f;
    authored.layers[0].exponent = 1.0f;
    authored.layers[0].intensity = 1.0f;
    authored.layers[0].diffuse[0] = 1.0f;
    authored.layers[0].emissive[0] = 0.5f;
    input.localLights.interiorBlend = 0.0f;
    const FogParams authoredOutside = BuildFogParams(input, config, &authored);
    input.localLights.interiorBlend = 1.0f;
    const FogParams authoredInside = BuildFogParams(input, config, &authored);
    Check(LocalNear(authoredInside.layers[0].density, authoredOutside.layers[0].density * 0.15f) &&
              authoredInside.layers[0].diffuse[0] == 0.0f &&
              authoredOutside.layers[kDistanceFogLayer].density == 0.0f &&
              LocalNear(authoredInside.layers[kDistanceFogLayer].density, inside.layers[kDistanceFogLayer].density),
          "dense Classic outdoor layers give way to native interior distance fog");

    ConfigStore settings;
    Config edited = settings.Get();
    edited.localLightIntensity = 20.0f;
    edited.localLightPhase = 2.0f;
    edited.interiorDensity = -1.0f;
    edited.interiorFarFog = 2.0f;
    edited.localLights = false;
    edited.interiorAware = false;
    settings.Apply(edited);
    Check(settings.Get().localLightIntensity == 8.0f && settings.Get().localLightPhase == 0.9f &&
              settings.Get().interiorDensity == 0.0f && settings.Get().interiorFarFog == 1.0f &&
              !settings.Get().localLights &&
              !settings.Get().interiorAware && settings.HasUnsavedChanges(),
          "local-light and interior controls clamp and participate in live-setting changes");
    edited.interiorFarFog = -1.0f;
    edited.localLightPhase = -2.0f;
    settings.Apply(edited);
    Config shipped;
    Config farEdited = shipped;
    farEdited.interiorFarFog = 0.8f;
    Check(settings.Get().interiorFarFog == 0.0f && shipped.interiorFarFog == 1.0f &&
              !SameFogSettings(shipped, farEdited) &&
              SettingChanges(shipped, farEdited) == "InteriorFarFog 1 -> 0.8",
          "InteriorFarFog defaults to 1, clamps to 0..1, discards fog history and logs its changes");
    Config phased = shipped;
    phased.localLightPhase = 0.5f;
    Check(settings.Get().localLightPhase == -0.9f && shipped.localLightPhase == 0.3f &&
              !SameFogSettings(shipped, phased) &&
              SettingChanges(shipped, phased) == "LocalLightPhase 0.3 -> 0.5",
          "LocalLightPhase defaults to 0.3, clamps to -0.9..0.9, discards fog history and logs its changes");
}
