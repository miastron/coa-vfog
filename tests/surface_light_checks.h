#pragma once

namespace surface_light_checks
{
constexpr UINT kViewWidth = 256;
constexpr UINT kViewHeight = 192;
constexpr float kEyeHeight = 9.0f;
constexpr float kLookAhead = 40.0f;
constexpr float kLightAhead = 22.0f;
constexpr float kLightHeight = 2.0f;
constexpr float kLightColour[3] = {1.0f, 0.6f, 0.3f};
constexpr float kLightConstantAttenuation = 1.0f;
constexpr float kLightQuadraticAttenuation = 0.05f;
constexpr float kBeyondReach = 5.0f;
constexpr float kHalfStrength = 0.5f;
constexpr float kFullStrength = 1.0f;
constexpr int kLitAtLeast = 8;

struct LitFrame
{
    Image image;
    bool rendered = false;
    bool statesKept = false;
    unsigned draws = 0;
};

struct Scene
{
    D3DVIEWPORT9 world = {0, 0, kViewWidth, kViewHeight, 0.0f, 1.0f};
    Vec3 eye = Add(kGameLikeWorldOffset, {0, 0, kEyeHeight});
    Vec3 at = Add(kGameLikeWorldOffset, {kLookAhead, 0, 0});
    float view[16] = {};
    float projection[16] = {};
};

Config SurfaceConfig(const Config& saved, float strength)
{
    Config config = saved;
    config.quality = 1;
    config.noiseAmount = 0;
    config.localLights = true;
    config.temporal = 0;
    config.godRays = 0;
    config.maxDistance = 1000;
    config.dataMode = 0;
    config.surfaceLights = strength;
    return config;
}

FrameInputs SceneInputs(Scene& scene, const Config& config)
{
    CameraRelativeLookAt(scene.eye, scene.at, scene.view);
    EngineProjection(static_cast<float>(kViewWidth) / kViewHeight, scene.projection);
    FrameInputs input = MakeInputs(scene.view, scene.projection, scene.eye, scene.at, scene.world);
    LocalPointLight light;
    const Vec3 position = Add(kGameLikeWorldOffset, {kLightAhead, 0, kLightHeight});
    light.position[0] = position.x;
    light.position[1] = position.y;
    light.position[2] = position.z;
    std::memcpy(light.color, kLightColour, sizeof(light.color));
    light.attenuation[0] = kLightConstantAttenuation;
    light.attenuation[2] = kLightQuadraticAttenuation;
    engine::SelectLocalPointLight(input.localLights, light, input.camPos, LocalLightUpload(config));
    return input;
}

LitFrame RenderLit(Harness& h, Scene& scene, const Config& saved, float strength, bool withLight)
{
    const Config config = SurfaceConfig(saved, strength);
    vf_test_set_config(&config);
    FrameInputs input = SceneInputs(scene, config);
    if (!withLight)
        input.localLights.pointLightCount = 0;
    LitFrame frame;
    const unsigned before = vf_test_surface_light_draws();
    h.BeginFrame();
    h.DrawScene(scene.eye, scene.view, scene.projection, scene.world);
    Sentinel s0;
    ReadSentinel(h.dev, s0);
    const char* skip = nullptr;
    frame.rendered = vf_test_render(&input, &skip) != 0;
    Sentinel s1;
    ReadSentinel(h.dev, s1);
    frame.statesKept = SameSentinel(s0, s1);
    if (!frame.statesKept)
        ReportSentinelDifferences(s0, s1);
    ReleaseSentinel(s0);
    ReleaseSentinel(s1);
    frame.image = Capture(h.dev);
    h.dev->EndScene();
    h.dev->Present(nullptr, nullptr, nullptr, nullptr);
    frame.draws = vf_test_surface_light_draws() - before;
    return frame;
}

bool GroundPixel(const Scene& scene, float ahead, UINT& x, UINT& y)
{
    const Vec3 ground = Add(kGameLikeWorldOffset, {ahead, 0, 0});
    const Vec3 relative = Sub(ground, scene.eye);
    const float world[3] = {relative.x, relative.y, relative.z};
    float view[3];
    TransformDirection(world, scene.view, view);
    if (view[2] <= 0.0f)
        return false;
    const float* p = scene.projection;
    const float ndcX = view[0] / view[2] * p[0] + p[8];
    const float ndcY = view[1] / view[2] * p[5] + p[9];
    const float px = scene.world.X + (ndcX * 0.5f + 0.5f) * scene.world.Width;
    const float py = scene.world.Y + (0.5f - ndcY * 0.5f) * scene.world.Height;
    if (px < 0.0f || py < 0.0f || px >= scene.world.Width || py >= scene.world.Height)
        return false;
    x = static_cast<UINT>(px);
    y = static_cast<UINT>(py);
    return true;
}

int GreenLift(const Image& lit, const Image& unlit, UINT x, UINT y)
{
    return static_cast<int>(lit.At(x, y)[1]) - static_cast<int>(unlit.At(x, y)[1]);
}

void CheckLightsOnSurfaces(Harness& h)
{
    Config saved;
    vf_test_get_config(&saved);
    Scene scene;
    const FrameInputs probe = SceneInputs(scene, SurfaceConfig(saved, kFullStrength));
    const float reach = probe.localLights.pointLightCount ? probe.localLights.pointLights[0].cutoff : 0.0f;
    const LitFrame off = RenderLit(h, scene, saved, 0.0f, true);
    const LitFrame half = RenderLit(h, scene, saved, kHalfStrength, true);
    const LitFrame full = RenderLit(h, scene, saved, kFullStrength, true);
    const LitFrame noLight = RenderLit(h, scene, saved, kFullStrength, false);
    const LitFrame noLightOff = RenderLit(h, scene, saved, 0.0f, false);
    UINT underX = 0;
    UINT underY = 0;
    UINT farX = 0;
    UINT farY = 0;
    const bool placed = GroundPixel(scene, kLightAhead, underX, underY) &&
                        GroundPixel(scene, kLightAhead + reach + kBeyondReach, farX, farY);
    const int halfLift = placed ? GreenLift(half.image, off.image, underX, underY) : 0;
    const int fullLift = placed ? GreenLift(full.image, off.image, underX, underY) : 0;
    const int farLift = placed ? GreenLift(full.image, off.image, farX, farY) : -1;
    std::printf("     ground under a point light %.1f yd away (reach %.1f yd): green lift %d at 0.5, %d at 1; beyond "
                "its reach %d; surface passes %u %u %u, without lights %u\n",
                kLightAhead, reach, halfLift, fullLift, farLift, off.draws, half.draws, full.draws, noLight.draws);
    Check(placed && off.rendered && full.rendered && half.rendered,
          "the surface light scene renders with the light's ground point and a point beyond its reach in view");
    Check(off.draws == 0 && half.draws == 1 && full.draws == 1 && halfLift >= kLitAtLeast && fullLift > halfLift,
          "SurfaceLights lights the ground under a point light, more at 1 than at 0.5");
    Check(farLift == 0, "ground beyond the light's reach is left exactly as it was");
    Check(noLight.draws == 0 && image_quality_checks::SameImage(noLight.image, noLightOff.image),
          "without point lights in view the surface pass is not drawn and the frame is unchanged");
    Check(full.statesKept, "the surface light pass restores render, sampler, texture, shader, constant, stream, "
                           "viewport, scissor, render target and depth state");
    vf_test_set_config(&saved);
}

void CheckShippedSetting(const std::wstring& shippedIni)
{
    const Config defaults = {};
    wchar_t text[16] = {};
    GetPrivateProfileStringW(L"CoAVolFog", L"SurfaceLights", L"", text, 16, shippedIni.c_str());
    Check(defaults.surfaceLights == 1.0f && std::wcscmp(text, L"1.0") == 0,
          "lights on surfaces are on at 1 by default and in the shipped INI");
}
}
