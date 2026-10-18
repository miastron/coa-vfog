#pragma once

void CheckDepthWriteStateBlockRestore(IDirect3DDevice9* device)
{
    DWORD initial = TRUE;
    device->GetRenderState(D3DRS_ZWRITEENABLE, &initial);
    for (DWORD desired : {static_cast<DWORD>(FALSE), static_cast<DWORD>(TRUE)})
    {
        IDirect3DStateBlock9* state = nullptr;
        device->SetRenderState(D3DRS_ZWRITEENABLE, desired);
        bool ok = SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &state)) && state;
        if (ok)
        {
            device->SetRenderState(D3DRS_ZWRITEENABLE, !desired);
            ok = SUCCEEDED(state->Apply());
            DWORD forced = FALSE;
            DWORD restored = !desired;
            vf_test_force_depth_write(1);
            device->GetRenderState(D3DRS_ZWRITEENABLE, &forced);
            vf_test_force_depth_write(0);
            device->GetRenderState(D3DRS_ZWRITEENABLE, &restored);
            ok = ok && forced == TRUE && restored == desired;
            vf_test_suppress_depth_write(1);
            device->GetRenderState(D3DRS_ZWRITEENABLE, &forced);
            vf_test_suppress_depth_write(0);
            device->GetRenderState(D3DRS_ZWRITEENABLE, &restored);
            ok = ok && forced == FALSE && restored == desired;
        }
        if (state)
            state->Release();
        Check(ok, desired ? "depth overrides restore enabled writes applied by a state block" :
                            "depth overrides restore disabled writes applied by a state block");
    }
    device->SetRenderState(D3DRS_ZWRITEENABLE, initial);
}

void CheckWorldTextDepthIsolation(Harness& h)
{
    IDirect3DDevice9* device = h.dev;
    IDirect3DStateBlock9* state = nullptr;
    const bool ready = SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &state));
    Check(ready, "world text depth regression captures device state");
    if (!ready)
        return;
    const D3DVIEWPORT9 viewport = {0, 0, h.pp.BackBufferWidth, h.pp.BackBufferHeight, 0, 1};
    device->SetViewport(&viewport);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    for (int suppress = 0; suppress < 2; ++suppress)
    {
        device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 0.8f, 0);
        device->BeginScene();
        h.DrawPretransformedQuadAtRawDepth(0, 0, 16, 32, 0.1f, 0xFFFFFFFF);
        vf_test_suppress_depth_write(suppress);
        device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        h.DrawPretransformedQuadAtRawDepth(8, 8, 32, 24, 0.2f, 0xFF0000FF);
        vf_test_suppress_depth_write(0);
        device->EndScene();
        const Image text = Capture(device);
        if (suppress)
        {
            Check(text.At(24, 16)[0] == 255 && text.At(24, 16)[1] == 0,
                  "world text still draws its visible colour with depth writes suppressed");
            Check(text.At(12, 16)[1] == 255, "world text still tests against foreground geometry");
        }
        device->BeginScene();
        h.DrawPretransformedQuadAtRawDepth(16, 8, 32, 24, 0.5f, 0xFF00FF00);
        device->EndScene();
        const Image probe = Capture(device);
        Check(probe.At(24, 16)[1] == (suppress ? 255 : 0),
              suppress ? "world text leaves the world depth behind its glyphs intact" :
                         "unprotected world text reproduces glyph contamination of world depth");
    }
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    vf_test_force_depth_write(1);
    vf_test_suppress_depth_write(1);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    DWORD suppressed = TRUE;
    DWORD forced = FALSE;
    DWORD restored = TRUE;
    device->GetRenderState(D3DRS_ZWRITEENABLE, &suppressed);
    vf_test_suppress_depth_write(0);
    device->GetRenderState(D3DRS_ZWRITEENABLE, &forced);
    vf_test_force_depth_write(0);
    device->GetRenderState(D3DRS_ZWRITEENABLE, &restored);
    Check(suppressed == FALSE && forced == TRUE && restored == FALSE,
          "text suppression nests with liquid depth writes and restores the client's last request");
    state->Apply();
    state->Release();
}

float RawDepthAtViewDistance(const float* glProjection, float viewZ)
{
    return (1.0f + glProjection[10]) * 0.5f + glProjection[14] * 0.5f / viewZ;
}

struct SunOcclusionView
{
    D3DVIEWPORT9 world = {0, 0, 1280, 688, 0, 1};
    Vec3 eye = Add({0, 0, 9}, kGameLikeWorldOffset);
    Vec3 at = Add({100, 2, 4}, kGameLikeWorldOffset);
    float view[16] = {};
    float projection[16] = {};
    FrameInputs input = {};
    float sunColumn = 0;
    float sunRow = 0;
    float horizonRow = 0;

    SunOcclusionView()
    {
        CameraRelativeLookAt(eye, at, view);
        EngineProjection(1280.0f / 688.0f, projection);
        input = MakeInputs(view, projection, eye, at, world);
        const float sunViewX = input.toLight[0] * view[0] + input.toLight[1] * view[4] + input.toLight[2] * view[8];
        const float sunViewY = input.toLight[0] * view[1] + input.toLight[1] * view[5] + input.toLight[2] * view[9];
        const float sunViewZ = input.toLight[0] * view[2] + input.toLight[1] * view[6] + input.toLight[2] * view[10];
        sunColumn = (sunViewX / sunViewZ * projection[0] * 0.5f + 0.5f) * world.Width;
        sunRow = (0.5f - sunViewY / sunViewZ * projection[5] * 0.5f) * world.Height;
        horizonRow = (0.5f + view[10] / view[9] * projection[5] * 0.5f) * world.Height;
    }
};

struct ScreenOccluder
{
    float bottomRow;
    float distance;
};

constexpr ScreenOccluder kNoOccluder = {0, 0};
constexpr float kFloatingOccluderDistance = 20.0f;
constexpr float kCliffDistance = 150.0f;
constexpr float kSkyRowsAboveSun = 40.0f;
constexpr float kCliffRowsBelowSun = 40.0f;
constexpr float kMaxKeptSunlightBehindCliff = 0.1f;

Image RenderSunRadiance(Harness& h, const SunOcclusionView& v, bool lightShafts, ScreenOccluder occluder,
                        bool& rendered, float sunScatter = 1.0f)
{
    Config config;
    config.sunScatter = sunScatter;
    config.temporal = 0.0f;
    config.noiseAmount = 0.0f;
    config.godRays = 0.0f;
    config.debugView = 1;
    config.lightShafts = lightShafts;
    vf_test_set_config(&config);
    h.BeginFrame();
    h.DrawScene(v.eye, v.view, v.projection, v.world);
    if (occluder.distance > 0)
        h.DrawPretransformedQuadAtRawDepth(0, 0, static_cast<float>(v.world.Width), occluder.bottomRow,
                                           RawDepthAtViewDistance(v.projection, occluder.distance));
    const char* skip = "";
    rendered = vf_test_render(&v.input, &skip) != 0 && rendered;
    Image radiance = Capture(h.dev);
    h.dev->EndScene();
    h.dev->Present(nullptr, nullptr, nullptr, nullptr);
    return radiance;
}

int BrightestChannel(const Image& image, UINT x, UINT y)
{
    const unsigned char* p = image.At(x, y);
    return std::max({p[0], p[1], p[2]});
}

UINT ColumnBesideSun(const SunOcclusionView& v, float columnFromSun)
{
    return static_cast<UINT>(std::clamp(v.sunColumn + columnFromSun, 20.0f, v.world.Width - 20.0f));
}

void CheckDisabledShaftsLeaveFogBehindOccluderLit(Harness& h, const SunOcclusionView& v)
{
    const ScreenOccluder floating = {(v.sunRow + v.horizonRow) * 0.5f, kFloatingOccluderDistance};
    bool rendered = true;
    const Image open = RenderSunRadiance(h, v, false, kNoOccluder, rendered);
    const Image blocked = RenderSunRadiance(h, v, false, floating, rendered);
    int worstChange = 0;
    int dimmestFog = 255;
    for (float rowBelowHorizon : {30.0f, 60.0f})
        for (float columnFromSun : {-200.0f, 0.0f, 200.0f})
        {
            const UINT x = ColumnBesideSun(v, columnFromSun);
            const UINT y = static_cast<UINT>(v.horizonRow + rowBelowHorizon);
            dimmestFog = std::min(dimmestFog, BrightestChannel(open, x, y));
            for (int channel = 0; channel < 3; ++channel)
                worstChange = std::max(worstChange, std::abs(blocked.At(x, y)[channel] - open.At(x, y)[channel]));
        }
    std::printf("     LightShafts=0, sun occluder at %.0f yd above rows %.0f (sun row %.0f, horizon %.0f): fog "
                "radiance on the ground changes by at most %d/255, dimmest sample %d/255\n",
                floating.distance, floating.bottomRow, v.sunRow, v.horizonRow, worstChange, dimmestFog);
    Check(rendered && v.sunRow < floating.bottomRow && floating.bottomRow < v.horizonRow && dimmestFog > 8 &&
              worstChange <= 1,
          "with LightShafts=0 a sun occluder leaves the in-scattered sunlight of the fog behind it unshadowed");
}

void CheckShaftsBlockSunBehindCliff(Harness& h, const SunOcclusionView& v)
{
    const ScreenOccluder cliff = {static_cast<float>(v.world.Height), kCliffDistance};
    bool rendered = true;
    const Image unshadowed = RenderSunRadiance(h, v, false, cliff, rendered);
    const Image shadowed = RenderSunRadiance(h, v, true, cliff, rendered);
    const Image unlit = RenderSunRadiance(h, v, false, cliff, rendered, 0.0f);
    float worstFraction = 0.0f;
    int dimmestSunlight = 255;
    for (float rowBelowSun : {0.0f, kCliffRowsBelowSun})
        for (float columnFromSun : {-60.0f, 0.0f, 60.0f})
        {
            const UINT x = ColumnBesideSun(v, columnFromSun);
            const UINT y = static_cast<UINT>(v.sunRow + rowBelowSun);
            const int ambient = BrightestChannel(unlit, x, y);
            const int sunlight = BrightestChannel(unshadowed, x, y) - ambient;
            const int keptSunlight = BrightestChannel(shadowed, x, y) - ambient;
            dimmestSunlight = std::min(dimmestSunlight, sunlight);
            worstFraction = std::max(worstFraction, keptSunlight / std::max(1.0f, sunlight * 1.0f));
        }
    std::printf("     cliff at %.0f yd hiding the sun: fog in front of it keeps at most %.2f of its in-scattered "
                "sunlight with LightShafts=1 (dimmest unshadowed sunlight %d/255)\n",
                kCliffDistance, worstFraction, dimmestSunlight);
    Check(rendered && dimmestSunlight > 8 && worstFraction <= kMaxKeptSunlightBehindCliff,
          "with LightShafts=1 a cliff hiding the sun blocks the sunlight in the fog in front of it: the halo no "
          "longer shines through terrain");
}

void CheckShaftsLeaveOpenSkyLit(Harness& h, const SunOcclusionView& v)
{
    bool rendered = true;
    const Image unshadowed = RenderSunRadiance(h, v, false, kNoOccluder, rendered);
    const Image shadowed = RenderSunRadiance(h, v, true, kNoOccluder, rendered);
    int worstChange = 0;
    int dimmestSky = 255;
    for (float columnFromSun : {-200.0f, 0.0f, 200.0f})
    {
        const UINT x = ColumnBesideSun(v, columnFromSun);
        const UINT y = static_cast<UINT>(v.sunRow - kSkyRowsAboveSun);
        dimmestSky = std::min(dimmestSky, BrightestChannel(unshadowed, x, y));
        for (int channel = 0; channel < 3; ++channel)
            worstChange = std::max(worstChange, std::abs(shadowed.At(x, y)[channel] - unshadowed.At(x, y)[channel]));
    }
    std::printf("     open sky %.0f rows above the sun: LightShafts=1 changes the fog radiance by at most %d/255 "
                "(dimmest sample %d/255)\n",
                kSkyRowsAboveSun, worstChange, dimmestSky);
    Check(rendered && dimmestSky > 8 && worstChange <= 2,
          "with LightShafts=1 the fog toward open sky around the sun keeps its in-scattered light");
}

void CheckSunOcclusion(Harness& h)
{
    Config saved;
    vf_test_get_config(&saved);
    const SunOcclusionView view;
    CheckDisabledShaftsLeaveFogBehindOccluderLit(h, view);
    CheckShaftsBlockSunBehindCliff(h, view);
    CheckShaftsLeaveOpenSkyLit(h, view);
    vf_test_set_config(&saved);
}

struct FogFixtureHeader
{
    char magic[4];
    uint32_t version;
    uint32_t lightCount;
    uint32_t paramsCount;
    uint32_t keyCount;
    uint32_t layerCount;
    uint32_t zoneCount;
    uint32_t pointCount;
    uint32_t gradingCurveCount;
};

constexpr size_t kFogFixtureLightBytes = 60;
constexpr size_t kFogFixtureParamsBytes = 16;
constexpr size_t kFogFixtureKeyBytes = 16;
constexpr size_t kFogFixtureFirstKeyOffset = 4;
constexpr size_t kFogFixtureKeyCountOffset = 8;
constexpr size_t kFogFixtureFirstLayerOffset = 4;
constexpr size_t kFogFixtureLayerCountOffset = 2;
constexpr size_t kFogFixtureGradingCurveOffset = 12;
constexpr uint32_t kFogFixtureEarlierFormat = 3;

template <typename T>
void SetFogFixtureValue(std::vector<unsigned char>& bytes, size_t offset, T value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void CheckRejectedFogFixture(const std::wstring& path, const std::vector<unsigned char>& bytes, const char* label)
{
    std::FILE* file = _wfopen(path.c_str(), L"wb");
    bool written = false;
    if (file)
    {
        written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        written = std::fclose(file) == 0 && written;
    }
    bool loaded = true;
    bool threw = false;
    FogData data;
    if (written)
    {
        try
        {
            loaded = data.Load(NarrowPath(path));
        }
        catch (...)
        {
            threw = true;
        }
    }
    Check(written && !threw && !loaded && !data.Loaded(), label);
}

void CheckFogDataBounds(const std::wstring& outDir, const std::string& dataPath)
{
    std::FILE* source = std::fopen(dataPath.c_str(), "rb");
    std::vector<unsigned char> original;
    if (source)
    {
        if (std::fseek(source, 0, SEEK_END) == 0)
        {
            const long size = std::ftell(source);
            if (size >= static_cast<long>(sizeof(FogFixtureHeader)) && std::fseek(source, 0, SEEK_SET) == 0)
            {
                original.resize(static_cast<size_t>(size));
                if (std::fread(original.data(), 1, original.size(), source) != original.size())
                    original.clear();
            }
        }
        std::fclose(source);
    }
    Check(!original.empty(), "Classic data is available for malformed-record regression checks");
    if (original.empty())
        return;
    FogFixtureHeader header;
    std::memcpy(&header, original.data(), sizeof(header));
    const size_t paramsOffset = sizeof(header) + header.lightCount * kFogFixtureLightBytes;
    const size_t keysOffset = paramsOffset + header.paramsCount * kFogFixtureParamsBytes;
    if (header.paramsCount == 0 || header.keyCount == 0 || keysOffset + kFogFixtureKeyBytes > original.size())
    {
        Check(false, "Classic fixture has parameter and layer ranges to validate");
        return;
    }
    const std::wstring fixture = FullPath(outDir + L"\\invalid-fogdata.bin");
    auto bytes = original;
    header.lightCount = UINT32_MAX;
    std::memcpy(bytes.data(), &header, sizeof(header));
    CheckRejectedFogFixture(fixture, bytes, "fogdata rejects impossible counts before allocating or throwing");

    bytes = original;
    SetFogFixtureValue<uint32_t>(bytes, paramsOffset + kFogFixtureFirstKeyOffset, UINT32_MAX);
    SetFogFixtureValue<uint32_t>(bytes, paramsOffset + kFogFixtureKeyCountOffset, 2);
    CheckRejectedFogFixture(fixture, bytes, "fogdata rejects overflowing time-key ranges");

    bytes = original;
    SetFogFixtureValue<uint32_t>(bytes, keysOffset + kFogFixtureFirstLayerOffset, UINT32_MAX);
    SetFogFixtureValue<uint16_t>(bytes, keysOffset + kFogFixtureLayerCountOffset, 2);
    CheckRejectedFogFixture(fixture, bytes, "fogdata rejects overflowing layer ranges");

    bytes = original;
    SetFogFixtureValue<uint32_t>(bytes, keysOffset + kFogFixtureGradingCurveOffset, header.gradingCurveCount + 1);
    CheckRejectedFogFixture(fixture, bytes, "fogdata rejects a grading curve index beyond its curve table");

    bytes = original;
    bytes.pop_back();
    CheckRejectedFogFixture(fixture, bytes, "fogdata rejects a truncated record array");

    bytes = original;
    SetFogFixtureValue<uint32_t>(bytes, offsetof(FogFixtureHeader, version), kFogFixtureEarlierFormat);
    const size_t logStart = ReadText(g_harnessLog).size();
    CheckRejectedFogFixture(fixture, bytes, "fogdata rejects the earlier format 3");
    const std::string logged = ReadText(g_harnessLog).substr(logStart);
    Check(logged.find("is format 3, but this build reads format 4; regenerate it") != std::string::npos,
          "a rejected fogdata format names both formats and the converter in the log");
    DeleteFileW(fixture.c_str());
}
