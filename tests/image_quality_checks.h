#pragma once

namespace image_quality_checks
{
constexpr int kDarkGrey = 96;
constexpr int kLightGrey = 160;
constexpr int kRampColumns = 8;
constexpr UINT kSampleRow = 300;
constexpr int kFlatProbeInset = 40;
constexpr D3DCOLOR kOutsideColour = D3DCOLOR_XRGB(200, 40, 40);
constexpr float kHalfSharpening = 0.5f;
constexpr float kFullSharpening = 1.0f;
constexpr int kFlatTolerance = 1;
constexpr int kSteeperRampAtLeast = 2;

struct SharpenedFrame
{
    Image image;
    bool statesKept = false;
    ImageStats before;
    ImageStats after;
    ImageQualityStatus status;

    unsigned Draws() const { return after.sharpened - before.sharpened; }
};

LONG RampStart(const D3DVIEWPORT9& world)
{
    return static_cast<LONG>(world.X + world.Width / 2);
}

void FillSoftEdge(IDirect3DDevice9* dev, const D3DVIEWPORT9& world)
{
    const LONG top = static_cast<LONG>(world.Y);
    const LONG bottom = static_cast<LONG>(world.Y + world.Height);
    const LONG ramp = RampStart(world);
    forever_look_checks::ClearRect(dev, static_cast<LONG>(world.X), top, ramp, bottom,
                                   D3DCOLOR_XRGB(kDarkGrey, kDarkGrey, kDarkGrey));
    for (int column = 0; column < kRampColumns; ++column)
    {
        const int grey = kDarkGrey + (kLightGrey - kDarkGrey) * (column + 1) / (kRampColumns + 1);
        forever_look_checks::ClearRect(dev, ramp + column, top, ramp + column + 1, bottom,
                                       D3DCOLOR_XRGB(grey, grey, grey));
    }
    forever_look_checks::ClearRect(dev, ramp + kRampColumns, top, static_cast<LONG>(world.X + world.Width), bottom,
                                   D3DCOLOR_XRGB(kLightGrey, kLightGrey, kLightGrey));
}

SharpenedFrame RenderSharpenedFrame(Harness& h, const D3DVIEWPORT9& world, float sharpening, bool worldDone)
{
    Config cfg = {};
    cfg.sharpening = sharpening;
    vf_test_set_config(&cfg);
    SharpenedFrame result;
    h.BeginFrame();
    forever_look_checks::PrepareClearableTarget(h.dev);
    h.dev->Clear(0, nullptr, D3DCLEAR_TARGET, kOutsideColour, 1.0f, 0);
    FillSoftEdge(h.dev, world);
    forever_look_checks::CaptureWorldViewport(h.dev, world);
    vf_test_image_stats(&result.before);
    if (worldDone)
        vf_test_image_world_done();
    h.SetEngineState(world);
    Sentinel s0;
    ReadSentinel(h.dev, s0);
    vf_test_hook_frame_end();
    Sentinel s1;
    ReadSentinel(h.dev, s1);
    result.statesKept = SameSentinel(s0, s1);
    if (!result.statesKept)
        ReportSentinelDifferences(s0, s1);
    ReleaseSentinel(s0);
    ReleaseSentinel(s1);
    result.image = Capture(h.dev);
    h.dev->EndScene();
    vf_test_image_stats(&result.after);
    vf_test_image_status(&result.status);
    return result;
}

int GreyAt(const Image& image, LONG x)
{
    return image.At(static_cast<UINT>(x), kSampleRow)[1];
}

int SteepestStep(const Image& image, const D3DVIEWPORT9& world)
{
    int steepest = 0;
    const LONG ramp = RampStart(world);
    for (LONG x = ramp - 2; x < ramp + kRampColumns + 2; ++x)
        steepest = std::max(steepest, std::abs(GreyAt(image, x + 1) - GreyAt(image, x)));
    return steepest;
}

bool FlatAreasKept(const Image& image, const D3DVIEWPORT9& world)
{
    const LONG left = static_cast<LONG>(world.X) + kFlatProbeInset;
    const LONG right = static_cast<LONG>(world.X + world.Width) - kFlatProbeInset;
    return std::abs(GreyAt(image, left) - kDarkGrey) <= kFlatTolerance &&
           std::abs(GreyAt(image, right) - kLightGrey) <= kFlatTolerance;
}

bool OutsideKept(const Image& image, const D3DVIEWPORT9& world)
{
    const unsigned char* below = image.At(world.X + world.Width / 2, world.Y + world.Height);
    return below[2] == 200 && below[1] == 40 && below[0] == 40;
}

bool SameImage(const Image& a, const Image& b)
{
    return a.w == b.w && a.h == b.h && a.bgra == b.bgra;
}

void CheckSharpening(Harness& h, const D3DVIEWPORT9& world)
{
    const SharpenedFrame off = RenderSharpenedFrame(h, world, 0.0f, true);
    const SharpenedFrame half = RenderSharpenedFrame(h, world, kHalfSharpening, true);
    const SharpenedFrame full = RenderSharpenedFrame(h, world, kFullSharpening, true);
    const SharpenedFrame withoutWorld = RenderSharpenedFrame(h, world, kFullSharpening, false);
    const SharpenedFrame offAgain = RenderSharpenedFrame(h, world, 0.0f, true);
    std::printf("     soft edge steepest step: off %d, sharpening 0.5 %d, 1 %d; draws %u %u %u, without a world %u, "
                "state \"%s\"\n",
                SteepestStep(off.image, world), SteepestStep(half.image, world), SteepestStep(full.image, world),
                off.Draws(), half.Draws(), full.Draws(), withoutWorld.Draws(), full.status.state);
    Check(half.Draws() == 1 && full.Draws() == 1 && full.status.drawn &&
              SteepestStep(half.image, world) >= SteepestStep(off.image, world) + kSteeperRampAtLeast &&
              SteepestStep(full.image, world) > SteepestStep(half.image, world),
          "sharpening at the frame end steepens a soft edge in the world viewport, more at 1 than at 0.5");
    Check(FlatAreasKept(full.image, world) && OutsideKept(full.image, world),
          "sharpening leaves flat areas and everything outside the world viewport unchanged");
    Check(half.statesKept && full.statesKept,
          "sharpening restores render, sampler, texture, shader, pixel constant, stream, viewport, scissor, render "
          "target and depth state");
    Check(off.Draws() == 0 && offAgain.Draws() == 0 && !offAgain.after.holdsSceneCopy &&
              SameImage(off.image, offAgain.image) && std::strcmp(offAgain.status.state, "off") == 0,
          "Sharpening=0 draws nothing, leaves the frame as it was and releases the scene copy");
    Check(withoutWorld.Draws() == 0,
          "a frame end without a world render this frame (login screen, loading) is not sharpened");
    Config defaults = {};
    vf_test_set_config(&defaults);
}

void CheckShippedSetting(const std::wstring& shippedIni)
{
    Config defaults = {};
    wchar_t text[16] = {};
    GetPrivateProfileStringW(L"CoAVolFog", L"Sharpening", L"", text, 16, shippedIni.c_str());
    Check(defaults.sharpening == 0.0f && std::wcscmp(text, L"0") == 0,
          "sharpening is off by default and in the shipped INI");
}
}
