#pragma once

namespace local_light_stabiliser_checks
{
constexpr double kStabiliserFrameSeconds = 1.0 / 60.0;
constexpr float kStabiliserNear = 1.0f;
constexpr float kStabiliserFar = 1000.0f;
constexpr int kStabiliserMap = 1;
constexpr int kFadeFrames = static_cast<int>(kLocalLightFadeSeconds / kStabiliserFrameSeconds + 0.5);
constexpr int kHalfSecondFrames = 30;
constexpr float kPassedLightReachShare = 0.75f;

LocalLightCamera ForwardCamera()
{
    return ForwardViewCamera(kStabiliserNear, kStabiliserFar);
}

void SetPeak(LocalPointLight& light, float peak)
{
    light.color[0] = peak;
    light.color[1] = peak * 0.5f;
    light.color[2] = peak * 0.25f;
}

LocalPointLight StabiliserLight(uintptr_t id, float x, float y, float z, float peak)
{
    LocalPointLight light;
    light.position[0] = x;
    light.position[1] = y;
    light.position[2] = z;
    SetPeak(light, peak);
    light.attenuation[0] = 1.0f;
    light.attenuation[2] = 0.01f;
    light.nativeId = id;
    return light;
}

void StepFrame(LocalLightStabiliser& stabiliser, const LocalLightCamera& camera, double seconds,
               LocalLightInputs& inputs, int mapId = kStabiliserMap)
{
    static uint32_t frame = 0;
    stabiliser.Step(camera, mapId, ++frame, seconds, inputs);
}

const LocalPointLight* UploadedLight(const LocalLightInputs& inputs, uintptr_t id)
{
    for (uint32_t i = 0; i < inputs.pointLightCount; ++i)
        if (inputs.pointLights[i].nativeId == id)
            return &inputs.pointLights[i];
    return nullptr;
}

LocalLightInputs CaptureOf(const std::vector<LocalPointLight>& lights, const LocalLightCamera& camera)
{
    LocalLightInputs inputs;
    for (const LocalPointLight& light : lights)
        engine::SelectLocalPointLight(inputs, light, camera.position, {false, 1.0f});
    return inputs;
}

LocalLightInputs CaptureInView(const std::vector<LocalPointLight>& lights, const LocalLightCamera& camera)
{
    LocalLightInputs inputs;
    for (const LocalPointLight& light : lights)
        engine::SelectLocalPointLightInView(inputs, light, camera, {false, 1.0f});
    return inputs;
}

bool Uploads(const LocalLightInputs& inputs, uintptr_t id, float* weight = nullptr)
{
    for (uint32_t i = 0; i < inputs.pointLightCount; ++i)
        if (inputs.pointLights[i].nativeId == id)
        {
            if (weight)
                *weight = inputs.pointLights[i].uploadedColor[0] / inputs.pointLights[i].color[0];
            return true;
        }
    return false;
}

bool SameUploadedSet(const LocalLightInputs& a, const LocalLightInputs& b)
{
    if (a.pointLightCount != b.pointLightCount)
        return false;
    for (uint32_t i = 0; i < a.pointLightCount; ++i)
        if (!Uploads(b, a.pointLights[i].nativeId))
            return false;
    return true;
}

std::vector<LocalPointLight> NineLightsAhead()
{
    std::vector<LocalPointLight> lights;
    for (uintptr_t i = 1; i <= 9; ++i)
        lights.push_back(StabiliserLight(i, 0.0f, 0.0f, 20.0f + 2.0f * static_cast<float>(i), 1.0f));
    return lights;
}

void CheckBoundaryChurnIsAbsorbed()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = NineLightsAhead();
    LocalLightInputs first = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, first);
    bool instantaneousChurns = false;
    bool stabilisedSteady = true;
    LocalLightInputs previousInstant = CaptureOf(lights, camera);
    for (int frame = 1; frame <= 60; ++frame)
    {
        lights[8].color[0] = frame % 2 ? 1.3f : 0.7f;
        lights[7].color[0] = frame % 2 ? 0.7f : 1.3f;
        LocalLightInputs instant = CaptureOf(lights, camera);
        instantaneousChurns = instantaneousChurns || !SameUploadedSet(instant, previousInstant);
        previousInstant = instant;
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, instant);
        stabilisedSteady = stabilisedSteady && SameUploadedSet(instant, first) && instant.pointLightCount == 8;
    }
    Check(instantaneousChurns && stabilisedSteady,
          "two lights trading rank 8 and 9 every frame swap the instantaneous eight-light set but not the "
          "stabilised one");
}

void CheckRemovedLightFadesOut()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = {StabiliserLight(1, 0.0f, 0.0f, 20.0f, 1.0f),
                                           StabiliserLight(2, 5.0f, 0.0f, 30.0f, 1.0f)};
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    lights.pop_back();
    float weight = 0.0f;
    bool fading = true;
    for (int frame = 1; frame <= 6; ++frame)
    {
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        fading = fading && Uploads(inputs, 2, &weight) && weight > 0.0f && weight < 1.0f &&
                 inputs.pointLights[0].nativeId == 1;
    }
    bool halfwayVisible = false;
    for (int frame = 7; frame <= kFadeFrames + 1; ++frame)
    {
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        if (frame == kHalfSecondFrames)
            halfwayVisible = Uploads(inputs, 2, &weight) && weight > 0.4f;
    }
    Check(fading && halfwayVisible && !Uploads(inputs, 2) && inputs.pointLightCount == 1,
          "a light that leaves the client's table is still uploaded at more than 0.4 strength half a second later "
          "and is gone once the fade time has passed");
}

void CheckLightsFadeInOnceTheSceneRuns()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = {StabiliserLight(1, 0.0f, 0.0f, 20.0f, 1.0f)};
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    float weight = 0.0f;
    const bool sceneLightAtOnce = Uploads(inputs, 1, &weight) && weight == 1.0f;
    lights.push_back(StabiliserLight(2, 5.0f, 0.0f, 30.0f, 1.0f));
    bool risesGradually = true;
    float previous = 0.0f;
    for (int frame = 1; frame <= kFadeFrames + 2; ++frame)
    {
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        risesGradually = risesGradually && Uploads(inputs, 2, &weight) && weight >= previous &&
                         weight - previous <= 2.0f / kFadeFrames && (frame > 1 || weight == 0.0f);
        previous = weight;
    }
    const bool fadedIn = weight == 1.0f;
    lights.push_back(StabiliserLight(3, -5.0f, 0.0f, 40.0f, 1.0f));
    inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, (kFadeFrames + 3) * kStabiliserFrameSeconds, inputs, kStabiliserMap + 1);
    const bool newMapAtOnce = Uploads(inputs, 3, &weight) && weight == 1.0f;
    Check(sceneLightAtOnce && risesGradually && fadedIn && newMapAtOnce,
          "the lights of the first capture after a reset, at start or on a new map, shine at once, and a light that "
          "appears inside the view later fades in over the fade time without a jump");
}

void CheckRejectedCapturesHoldTheLastSet()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = {StabiliserLight(1, 0.0f, 0.0f, 20.0f, 1.0f),
                                           StabiliserLight(2, 5.0f, 0.0f, 30.0f, 1.0f)};
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    const LocalLightInputs settled = inputs;
    bool held = true;
    for (int frame = 1; frame <= 3; ++frame)
    {
        LocalLightInputs rejected;
        rejected.capture = LocalLightCapture::TableChanged;
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, rejected);
        held = held && rejected.capture == LocalLightCapture::TableChanged &&
               std::memcmp(rejected.pointLights, settled.pointLights, sizeof(settled.pointLights)) == 0 &&
               rejected.pointLightCount == settled.pointLightCount;
    }
    inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 4 * kStabiliserFrameSeconds, inputs);
    const bool resumed = SameUploadedSet(inputs, settled) && inputs.pointLights[0].uploadedColor[0] == 1.0f;
    bool released = false;
    for (int frame = 5; frame <= 5 + static_cast<int>(kLocalLightHoldCaptures) + kFadeFrames + 1; ++frame)
    {
        LocalLightInputs rejected;
        rejected.capture = LocalLightCapture::DamagedTable;
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, rejected);
        released = rejected.pointLightCount == 0;
    }
    Check(held && resumed && released && stabiliser.Activity().heldCaptures == 3 + kLocalLightHoldCaptures,
          "a capture that walked nothing because the table changed keeps the last uploaded set unchanged for up "
          "to eight captures, then the lights fade");
}

void CheckReusedNodeAddressIsANewLight()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = {StabiliserLight(1, 0.0f, 0.0f, 20.0f, 1.0f)};
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    lights[0].position[0] = 10.0f;
    inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, kStabiliserFrameSeconds, inputs);
    bool twoLights = inputs.pointLightCount == 2;
    float oldWeight = 1.0f;
    float newWeight = 0.0f;
    for (uint32_t i = 0; i < inputs.pointLightCount; ++i)
        (inputs.pointLights[i].position[0] == 10.0f ? newWeight : oldWeight) =
            inputs.pointLights[i].uploadedColor[0] / inputs.pointLights[i].color[0];
    Check(twoLights && newWeight == 0.0f && oldWeight < 1.0f && stabiliser.Activity().admissions == 2,
          "the same node address 10 yd away is a new light: it takes a slot at once and fades in while the old one "
          "fades out");
}

void CheckLightsOutsideTheViewFreeTheirSlot()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = NineLightsAhead();
    lights.pop_back();
    lights.pop_back();
    lights.push_back(StabiliserLight(9, 0.0f, 0.0f, -250.0f, 200.0f));
    LocalLightInputs inputs = CaptureOf(lights, camera);
    const bool behindRanksFirst = inputs.pointLights[0].nativeId == 9 && inputs.pointLightCount == 8;
    StepFrame(stabiliser, camera, 0.0, inputs);
    const bool behindSkipped = !Uploads(inputs, 9) && inputs.pointLightCount == 7;
    lights.back().position[2] = 60.0f;
    inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, kStabiliserFrameSeconds, inputs);
    float weight = 0.0f;
    const bool admittedAtOnce = Uploads(inputs, 9, &weight) && weight == 0.0f && inputs.pointLightCount == 8;
    const LocalPointLight beside = StabiliserLight(10, 300.0f, 0.0f, 0.5f, 1.0f);
    Check(behindRanksFirst && behindSkipped && admittedAtOnce &&
              CaptureInView({lights.back()}, camera).candidateCount == 1 &&
              CaptureInView({beside}, camera).candidateCount == 0,
          "a light whose sphere lies wholly behind or beside the view never takes a slot, and it takes a free slot "
          "the step it enters the view and fades in from there");
}

void CheckLightLeavingTheViewFreesItsSlotAtOnce()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights;
    for (uintptr_t i = 1; i <= 8; ++i)
        lights.push_back(StabiliserLight(i, 0.0f, 0.0f, 50.0f * static_cast<float>(i), 1.0f));
    LocalLightInputs inputs = CaptureInView(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    const bool eightAhead = inputs.pointLightCount == 8 && Uploads(inputs, 1);
    const float reach = engine::PointLightCutoff(lights[1].color, lights[1].attenuation);
    LocalLightCamera moved = camera;
    moved.position[2] = lights[1].position[2] + kPassedLightReachShare * reach;
    lights.push_back(StabiliserLight(9, 0.0f, 0.0f, 270.0f, 1.0f));
    inputs = CaptureInView(lights, moved);
    const bool firstLeftBehind = inputs.candidateCount == 8 && !Uploads(inputs, 1);
    StepFrame(stabiliser, moved, kStabiliserFrameSeconds, inputs);
    float weight = 0.0f;
    const bool swappedAtOnce = !Uploads(inputs, 1) && Uploads(inputs, 9, &weight) && weight == 0.0f &&
                               inputs.pointLightCount == 8;
    lights.erase(lights.begin() + 1);
    inputs = CaptureInView(lights, moved);
    StepFrame(stabiliser, moved, 2.0 * kStabiliserFrameSeconds, inputs);
    const bool removedInViewFades = Uploads(inputs, 2, &weight) && weight > 0.0f && weight < 1.0f &&
                                    inputs.pointLightCount == 8;
    Check(eightAhead && firstLeftBehind && swappedAtOnce && removedInViewFades &&
              stabiliser.Activity().fadeOuts == 0,
          "a slotted light the camera has passed, so its sphere left the view and the capture no longer carries "
          "it, frees its slot the same step for a newcomer that fades in, while a light that leaves the table "
          "inside the view fades out");
}

float StabiliserCutoff(const LocalPointLight& light)
{
    float uploaded[3];
    engine::UploadedPointLightColor(light.color, {false, 1.0f}, uploaded);
    return engine::PointLightCutoff(uploaded, light.attenuation);
}

void CheckLightJitteringAtTheViewEdgeKeepsItsSlot()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights;
    for (uintptr_t i = 1; i <= 7; ++i)
        lights.push_back(StabiliserLight(i, 0.0f, 0.0f, 50.0f * static_cast<float>(i), 1.0f));
    LocalPointLight edge = StabiliserLight(8, 0.0f, 0.0f, 120.0f, 1.0f);
    edge.position[0] = edge.position[2] + (StabiliserCutoff(edge) - 1.0f) * std::sqrt(2.0f);
    lights.push_back(edge);
    lights.push_back(StabiliserLight(9, 0.0f, 0.0f, 500.0f, 1.0f));
    LocalLightInputs inputs = CaptureInView(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    const bool edgeSlotted = inputs.pointLightCount == 8 && Uploads(inputs, 8) && !Uploads(inputs, 9);
    LocalLightCamera jittered = camera;
    jittered.position[0] = -2.0f;
    inputs = CaptureInView(lights, jittered);
    const bool edgeLeft = inputs.candidateCount == 8 && !Uploads(inputs, 8);
    StepFrame(stabiliser, jittered, kStabiliserFrameSeconds, inputs);
    float weight = 0.0f;
    const bool edgeDims = Uploads(inputs, 8, &weight) && weight > 0.0f && weight < 1.0f && !Uploads(inputs, 9) &&
                          inputs.pointLightCount == 8;
    inputs = CaptureInView(lights, camera);
    StepFrame(stabiliser, camera, 2.0 * kStabiliserFrameSeconds, inputs);
    const bool edgeReturns = Uploads(inputs, 8, &weight) && weight >= 0.99f && !Uploads(inputs, 9) &&
                             stabiliser.Activity().admissions == 8 && stabiliser.Activity().fadeOuts == 0;
    LocalLightCamera turned = camera;
    turned.position[0] = -20.0f;
    inputs = CaptureInView(lights, turned);
    StepFrame(stabiliser, turned, 3.0 * kStabiliserFrameSeconds, inputs);
    const bool edgeFreed = !Uploads(inputs, 8) && Uploads(inputs, 9, &weight) && weight == 0.0f &&
                           stabiliser.Activity().fadeOuts == 0;
    Check(edgeSlotted && edgeLeft && edgeDims && edgeReturns && edgeFreed,
          "a slotted light whose sphere jitters just outside the view's edge dims and keeps its slot until it is "
          "back, and only a sphere clearly outside the view frees the slot at once");
}

void CheckLightTurnedBackIntoViewShinesAtOnce()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    LocalPointLight lamp = StabiliserLight(1, 0.0f, 0.0f, 100.0f, 1.0f);
    lamp.attenuation[2] = 1.0f;
    lamp.cutoff = StabiliserCutoff(lamp);
    const std::vector<LocalPointLight> lights = {lamp};
    LocalLightInputs inputs = CaptureInView(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    LocalLightCamera turnedAway = camera;
    turnedAway.position[0] = -200.0f;
    inputs = CaptureInView(lights, turnedAway);
    StepFrame(stabiliser, turnedAway, kStabiliserFrameSeconds, inputs);
    const bool forgotten = inputs.pointLightCount == 0;
    LocalLightCamera edge = camera;
    edge.position[0] = -100.0f - lamp.cutoff * 0.5f * std::sqrt(2.0f);
    inputs = CaptureInView(lights, edge);
    const bool rimInView = inputs.candidateCount == 1;
    StepFrame(stabiliser, edge, 2.0 * kStabiliserFrameSeconds, inputs);
    float weight = 0.0f;
    const bool atOnce = Uploads(inputs, 1, &weight) && weight == 1.0f;
    inputs = CaptureInView(lights, camera);
    StepFrame(stabiliser, camera, 3.0 * kStabiliserFrameSeconds, inputs);
    Check(forgotten && rimInView && atOnce && Uploads(inputs, 1, &weight) && weight == 1.0f,
          "a light whose sphere comes back into the view from the edge, its centre still outside, shines at once "
          "instead of fading in again, so turning the camera away and back does not dim it");
}

void CheckStrongerNewcomerReplacesTheWeakestSlot()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = NineLightsAhead();
    lights.pop_back();
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    lights.push_back(StabiliserLight(9, 0.0f, 0.0f, 10.0f, 4.0f));
    bool waited = true;
    for (int frame = 1; frame <= 2; ++frame)
    {
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        waited = waited && !Uploads(inputs, 9);
    }
    bool replaced = false;
    for (int frame = 3; frame <= kFadeFrames + 10 && !replaced; ++frame)
    {
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        replaced = Uploads(inputs, 9) && !Uploads(inputs, 8) && inputs.pointLightCount == 8;
    }
    Check(waited && replaced && stabiliser.Activity().evictions == 1,
          "a newcomer far stronger than the weakest slot waits three captures, then takes the slot after the weakest "
          "has faded");
}

void CheckSwapsOverlap()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = NineLightsAhead();
    lights.pop_back();
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    lights.push_back(StabiliserLight(9, 0.0f, 0.0f, 10.0f, 4.0f));
    lights.push_back(StabiliserLight(10, -3.0f, 0.0f, 12.0f, 4.0f));
    for (int frame = 1; frame <= kFadeFrames + 10; ++frame)
    {
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
    }
    Check(Uploads(inputs, 9) && Uploads(inputs, 10) && !Uploads(inputs, 7) && !Uploads(inputs, 8) &&
              inputs.pointLightCount == 8 && stabiliser.Activity().evictions == 2,
          "two strong newcomers evict the two weakest slots in overlapping fades, so both shine within one fade "
          "time instead of one after the other");
}

void CheckEvictedLightDoesNotRetakeItsSlot()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = NineLightsAhead();
    lights.pop_back();
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    lights.push_back(StabiliserLight(9, 3.0f, 0.0f, 36.0f, 2.6f));
    int firstStepWithoutWeakest = 0;
    bool challengerTookOver = false;
    bool weakestStayedOut = true;
    for (int frame = 1; frame <= kFadeFrames + 30; ++frame)
    {
        SetPeak(lights.back(), frame <= 10 ? 2.6f : 0.9f);
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        if (!firstStepWithoutWeakest && !Uploads(inputs, 8))
        {
            firstStepWithoutWeakest = frame;
            float weight = 0.0f;
            challengerTookOver = Uploads(inputs, 9, &weight) && weight == 0.0f && inputs.pointLightCount == 8;
        }
        else if (firstStepWithoutWeakest)
            weakestStayedOut = weakestStayedOut && !Uploads(inputs, 8) && Uploads(inputs, 9);
    }
    Check(firstStepWithoutWeakest && challengerTookOver && weakestStayedOut &&
              stabiliser.Activity().evictions == 1 && stabiliser.Activity().admissions == 9,
          "an evicted light that is still captured does not take its slot back after fading, even when the "
          "challenger's priority has dropped below its own by then: the challenger takes the slot and fades in");
}

void CheckAttenuationDriftKeepsOneLight()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = {StabiliserLight(1, 0.0f, 0.0f, 20.0f, 1.0f),
                                           StabiliserLight(2, 5.0f, 0.0f, 30.0f, 1.0f)};
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    bool single = true;
    for (int frame = 1; frame <= 30; ++frame)
    {
        lights[0].attenuation[2] = std::nextafter(lights[0].attenuation[2], 1.0f);
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        const LocalPointLight* drifting = UploadedLight(inputs, 1);
        single = single && inputs.pointLightCount == 2 && drifting && drifting->uploadedColor[0] == 1.0f &&
                 drifting->attenuation[2] == lights[0].attenuation[2];
    }
    Check(single && stabiliser.Activity().admissions == 2 && stabiliser.Activity().fadeOuts == 0,
          "a light whose attenuation drifts between captures keeps its one slot at full strength and carries the "
          "latest attenuation");
}

void CheckSameFrameStepReplaysTheOutput()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = {StabiliserLight(1, 0.0f, 0.0f, 20.0f, 1.0f),
                                           StabiliserLight(2, 5.0f, 0.0f, 30.0f, 1.0f)};
    LocalLightInputs inputs = CaptureOf(lights, camera);
    stabiliser.Step(camera, kStabiliserMap, 7, 0.0, inputs);
    const LocalLightInputs first = inputs;
    lights.pop_back();
    LocalLightInputs sameFrame = CaptureOf(lights, camera);
    stabiliser.Step(camera, kStabiliserMap, 7, 0.0001, sameFrame);
    LocalLightInputs rejectedSameFrame;
    rejectedSameFrame.capture = LocalLightCapture::TableChanged;
    stabiliser.Step(camera, kStabiliserMap, 7, 0.0002, rejectedSameFrame);
    const bool replayed = std::memcmp(sameFrame.pointLights, first.pointLights, sizeof(first.pointLights)) == 0 &&
                          sameFrame.pointLightCount == 2 && rejectedSameFrame.pointLightCount == 2 &&
                          stabiliser.Activity().heldCaptures == 0;
    inputs = CaptureOf(lights, camera);
    stabiliser.Step(camera, kStabiliserMap, 8, kStabiliserFrameSeconds, inputs);
    float weight = 1.0f;
    Check(replayed && Uploads(inputs, 2, &weight) && weight < 1.0f,
          "a second step in the same frame replays the stabilised set without counting a capture, and the next "
          "frame goes on from it");
}

void CheckNoSwapWhileASlotFadesOut()
{
    const LocalLightCamera camera = ForwardCamera();
    LocalLightStabiliser stabiliser;
    std::vector<LocalPointLight> lights = NineLightsAhead();
    lights.pop_back();
    LocalLightInputs inputs = CaptureOf(lights, camera);
    StepFrame(stabiliser, camera, 0.0, inputs);
    lights.erase(lights.begin() + 2);
    lights.push_back(StabiliserLight(9, 3.0f, 0.0f, 36.0f, 2.6f));
    bool weakestSteady = true;
    int newcomerAdmitted = 0;
    for (int frame = 1; frame <= kFadeFrames + 10; ++frame)
    {
        inputs = CaptureOf(lights, camera);
        StepFrame(stabiliser, camera, frame * kStabiliserFrameSeconds, inputs);
        float weight = 0.0f;
        weakestSteady = weakestSteady && Uploads(inputs, 8, &weight) && weight == 1.0f;
        if (!newcomerAdmitted && Uploads(inputs, 9) && !Uploads(inputs, 3))
            newcomerAdmitted = frame;
    }
    Check(weakestSteady && newcomerAdmitted && stabiliser.Activity().evictions == 0 &&
              stabiliser.Activity().admissions == 9,
          "a strong newcomer that appears while another light fades out of its slot waits for that slot instead of "
          "evicting the weakest steady light");
}

void CheckLocalLightStabiliser()
{
    CheckBoundaryChurnIsAbsorbed();
    CheckRemovedLightFadesOut();
    CheckLightsFadeInOnceTheSceneRuns();
    CheckRejectedCapturesHoldTheLastSet();
    CheckReusedNodeAddressIsANewLight();
    CheckLightsOutsideTheViewFreeTheirSlot();
    CheckLightLeavingTheViewFreesItsSlotAtOnce();
    CheckLightJitteringAtTheViewEdgeKeepsItsSlot();
    CheckLightTurnedBackIntoViewShinesAtOnce();
    CheckStrongerNewcomerReplacesTheWeakestSlot();
    CheckSwapsOverlap();
    CheckEvictedLightDoesNotRetakeItsSlot();
    CheckAttenuationDriftKeepsOneLight();
    CheckSameFrameStepReplaysTheOutput();
    CheckNoSwapWhileASlotFadesOut();
}
}
