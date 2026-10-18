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
constexpr int kStairDark = 32;
constexpr int kStairLight = 224;
constexpr LONG kRowsPerStairStep = 4;
constexpr int kBlendedLow = 40;
constexpr int kBlendedHigh = 216;
constexpr LONG kEdgeBand = 3;
constexpr float kBlendedRowsAtLeast = 0.5f;
constexpr int kBloomGround = 96;
constexpr int kBelowThresholdGrey = 176;
constexpr int kSpotWhite = 255;
constexpr LONG kSpotHalf = 8;
constexpr LONG kPatchInset = 40;
constexpr LONG kPatchSize = 160;
constexpr LONG kNearHalo = 8;
constexpr LONG kWideHalo = 48;
constexpr int kNearHaloAtLeast = 4;
constexpr float kHalfBloom = 0.5f;
constexpr float kFullBloom = 1.0f;

using FillWorld = void (*)(IDirect3DDevice9*, const D3DVIEWPORT9&);

struct SharpenedFrame
{
    Image image;
    bool statesKept = false;
    ImageStats before;
    ImageStats after;
    ImageQualityStatus status;

    unsigned Draws() const { return after.sharpened - before.sharpened; }
    unsigned Smoothings() const { return after.smoothed - before.smoothed; }
    unsigned Blooms() const { return after.bloomed - before.bloomed; }
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

LONG StairEdge(const D3DVIEWPORT9& world, LONG y)
{
    return static_cast<LONG>(world.X + world.Width / 4) + (y - static_cast<LONG>(world.Y)) / kRowsPerStairStep;
}

void FillStairs(IDirect3DDevice9* dev, const D3DVIEWPORT9& world)
{
    const LONG right = static_cast<LONG>(world.X + world.Width);
    for (LONG y = static_cast<LONG>(world.Y); y < static_cast<LONG>(world.Y + world.Height); ++y)
    {
        const LONG edge = StairEdge(world, y);
        forever_look_checks::ClearRect(dev, static_cast<LONG>(world.X), y, edge, y + 1,
                                       D3DCOLOR_XRGB(kStairDark, kStairDark, kStairDark));
        forever_look_checks::ClearRect(dev, edge, y, right, y + 1,
                                       D3DCOLOR_XRGB(kStairLight, kStairLight, kStairLight));
    }
}

POINT SpotCentre(const D3DVIEWPORT9& world)
{
    return {static_cast<LONG>(world.X + world.Width / 2), static_cast<LONG>(world.Y + world.Height / 2)};
}

void FillBrightSpot(IDirect3DDevice9* dev, const D3DVIEWPORT9& world)
{
    const LONG left = static_cast<LONG>(world.X);
    const LONG top = static_cast<LONG>(world.Y);
    forever_look_checks::ClearRect(dev, left, top, left + static_cast<LONG>(world.Width),
                                   top + static_cast<LONG>(world.Height),
                                   D3DCOLOR_XRGB(kBloomGround, kBloomGround, kBloomGround));
    forever_look_checks::ClearRect(dev, left + kPatchInset, top + kPatchInset, left + kPatchInset + kPatchSize,
                                   top + kPatchInset + kPatchSize,
                                   D3DCOLOR_XRGB(kBelowThresholdGrey, kBelowThresholdGrey, kBelowThresholdGrey));
    const POINT spot = SpotCentre(world);
    forever_look_checks::ClearRect(dev, spot.x - kSpotHalf, spot.y - kSpotHalf, spot.x + kSpotHalf, spot.y + kSpotHalf,
                                   D3DCOLOR_XRGB(kSpotWhite, kSpotWhite, kSpotWhite));
}

SharpenedFrame RenderImageFrame(Harness& h, const D3DVIEWPORT9& world, const Config& cfg, FillWorld fill,
                                bool worldDone)
{
    vf_test_set_config(&cfg);
    SharpenedFrame result;
    h.BeginFrame();
    forever_look_checks::PrepareClearableTarget(h.dev);
    h.dev->Clear(0, nullptr, D3DCLEAR_TARGET, kOutsideColour, 1.0f, 0);
    fill(h.dev, world);
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

SharpenedFrame RenderSharpenedFrame(Harness& h, const D3DVIEWPORT9& world, float sharpening, bool worldDone)
{
    Config cfg = {};
    cfg.sharpening = sharpening;
    return RenderImageFrame(h, world, cfg, &FillSoftEdge, worldDone);
}

SharpenedFrame RenderBloom(Harness& h, const D3DVIEWPORT9& world, float bloom, bool antialiasing = false,
                           float sharpening = 0.0f)
{
    Config cfg = {};
    cfg.bloom = bloom;
    cfg.antialiasing = antialiasing;
    cfg.sharpening = sharpening;
    return RenderImageFrame(h, world, cfg, &FillBrightSpot, true);
}

SharpenedFrame RenderStairs(Harness& h, const D3DVIEWPORT9& world, bool antialiasing, float sharpening)
{
    Config cfg = {};
    cfg.antialiasing = antialiasing;
    cfg.sharpening = sharpening;
    return RenderImageFrame(h, world, cfg, &FillStairs, true);
}

int GreyAt(const Image& image, LONG x, LONG y)
{
    return image.At(static_cast<UINT>(x), static_cast<UINT>(y))[1];
}

int GreyAt(const Image& image, LONG x)
{
    return GreyAt(image, x, static_cast<LONG>(kSampleRow));
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

float BlendedRowShare(const Image& image, const D3DVIEWPORT9& world)
{
    int blendedRows = 0;
    int rows = 0;
    const LONG margin = kRowsPerStairStep * 2;
    for (LONG y = static_cast<LONG>(world.Y) + margin; y < static_cast<LONG>(world.Y + world.Height) - margin; ++y)
    {
        ++rows;
        const LONG edge = StairEdge(world, y);
        for (LONG x = edge - kEdgeBand; x <= edge + kEdgeBand; ++x)
        {
            const int grey = GreyAt(image, x, y);
            if (grey >= kBlendedLow && grey <= kBlendedHigh)
            {
                ++blendedRows;
                break;
            }
        }
    }
    return rows ? static_cast<float>(blendedRows) / rows : 0.0f;
}

bool StairFlatsKept(const Image& image, const D3DVIEWPORT9& world)
{
    const LONG y = static_cast<LONG>(world.Y + world.Height / 2);
    const LONG edge = StairEdge(world, y);
    return GreyAt(image, static_cast<LONG>(world.X) + kFlatProbeInset, y) == kStairDark &&
           GreyAt(image, edge + kFlatProbeInset, y) == kStairLight;
}

void CheckAntialiasing(Harness& h, const D3DVIEWPORT9& world)
{
    const SharpenedFrame off = RenderStairs(h, world, false, 0.0f);
    const SharpenedFrame smoothed = RenderStairs(h, world, true, 0.0f);
    const SharpenedFrame both = RenderStairs(h, world, true, kHalfSharpening);
    const SharpenedFrame sharpenedOnly = RenderStairs(h, world, false, kHalfSharpening);
    const SharpenedFrame offAgain = RenderStairs(h, world, false, 0.0f);
    std::printf("     stair edge rows with a blended pixel: off %.2f, SMAA %.2f, SMAA and sharpening %.2f; SMAA draws "
                "%u %u, linear-light blend %d\n",
                BlendedRowShare(off.image, world), BlendedRowShare(smoothed.image, world),
                BlendedRowShare(both.image, world), smoothed.Smoothings(), both.Smoothings(),
                smoothed.after.linearBlend ? 1 : 0);
    Check(off.Smoothings() == 0 && BlendedRowShare(off.image, world) == 0.0f,
          "without antialiasing the stair edge keeps only its two greys");
    Check(smoothed.Smoothings() == 1 && smoothed.status.drawn &&
              BlendedRowShare(smoothed.image, world) >= kBlendedRowsAtLeast,
          "SMAA at the frame end blends the pixels along a shallow stair edge in the world viewport");
    Check(StairFlatsKept(smoothed.image, world) && OutsideKept(smoothed.image, world),
          "SMAA leaves flat areas and everything outside the world viewport unchanged");
    Check(smoothed.statesKept && both.statesKept,
          "SMAA, alone and followed by sharpening, restores render, sampler, texture, shader, pixel constant, stream, "
          "viewport, scissor, render target and depth state");
    Check(both.Smoothings() == 1 && both.Draws() == 1 && BlendedRowShare(both.image, world) >= kBlendedRowsAtLeast &&
              both.after.holdsAntialiasingTargets,
          "with SMAA and sharpening both on, the smoothed image is sharpened in the same frame-end pass");
    Check(sharpenedOnly.Smoothings() == 0 && !sharpenedOnly.after.holdsAntialiasingTargets &&
              offAgain.Draws() == 0 && !offAgain.after.holdsSceneCopy,
          "turning SMAA off releases its textures and turning both off releases the scene copy");
    Config defaults = {};
    vf_test_set_config(&defaults);
}

int HaloLift(const Image& image, const D3DVIEWPORT9& world, LONG distance)
{
    const POINT spot = SpotCentre(world);
    return GreyAt(image, spot.x + kSpotHalf + distance, spot.y) - kBloomGround;
}

bool BloomFlatsKept(const Image& image, const D3DVIEWPORT9& world)
{
    const LONG patchCentre = kPatchInset + kPatchSize / 2;
    const LONG farX = static_cast<LONG>(world.X + world.Width) - kPatchInset;
    const LONG farY = static_cast<LONG>(world.Y + world.Height) - kPatchInset;
    return GreyAt(image, static_cast<LONG>(world.X) + patchCentre, static_cast<LONG>(world.Y) + patchCentre) ==
               kBelowThresholdGrey &&
           GreyAt(image, farX, farY) == kBloomGround;
}

void CheckBloom(Harness& h, const D3DVIEWPORT9& world)
{
    const SharpenedFrame off = RenderBloom(h, world, 0.0f);
    const SharpenedFrame half = RenderBloom(h, world, kHalfBloom);
    const SharpenedFrame full = RenderBloom(h, world, kFullBloom);
    const SharpenedFrame all = RenderBloom(h, world, kHalfBloom, true, kHalfSharpening);
    const SharpenedFrame offAgain = RenderBloom(h, world, 0.0f);
    std::printf("     bright spot halo %ld and %ld px out: off %d %d, bloom 0.5 %d %d, 1 %d %d, with SMAA and "
                "sharpening %d %d; draws %u %u %u, float levels %d\n",
                kNearHalo, kWideHalo, HaloLift(off.image, world, kNearHalo), HaloLift(off.image, world, kWideHalo),
                HaloLift(half.image, world, kNearHalo), HaloLift(half.image, world, kWideHalo),
                HaloLift(full.image, world, kNearHalo), HaloLift(full.image, world, kWideHalo),
                HaloLift(all.image, world, kNearHalo), HaloLift(all.image, world, kWideHalo), half.Blooms(),
                full.Blooms(), all.Blooms(), half.after.floatBloom ? 1 : 0);
    Check(off.Blooms() == 0 && HaloLift(off.image, world, kNearHalo) == 0,
          "without bloom the ground beside a bright spot keeps its grey");
    Check(half.Blooms() == 1 && half.status.drawn && HaloLift(half.image, world, kNearHalo) >= kNearHaloAtLeast &&
              HaloLift(half.image, world, kWideHalo) > 0 &&
              HaloLift(full.image, world, kNearHalo) > HaloLift(half.image, world, kNearHalo),
          "bloom at the frame end puts a halo that fades with distance around a bright spot, stronger at 1 than at "
          "0.5");
    Check(BloomFlatsKept(half.image, world) && BloomFlatsKept(full.image, world) && OutsideKept(full.image, world),
          "bloom leaves areas below its threshold, ground far from the spot and everything outside the world viewport "
          "unchanged");
    Check(half.statesKept && all.statesKept,
          "bloom, alone and after SMAA and sharpening, restores render, blend, sampler, texture, shader, pixel "
          "constant, stream, viewport, scissor, render target and depth state");
    Check(all.Blooms() == 1 && all.Smoothings() == 1 && all.Draws() == 1 &&
              HaloLift(all.image, world, kNearHalo) >= kNearHaloAtLeast,
          "with SMAA and sharpening on as well, the halo is added after them in the same frame-end pass");
    Check(offAgain.Blooms() == 0 && !offAgain.after.holdsBloomTargets && !offAgain.after.holdsSceneCopy &&
              SameImage(off.image, offAgain.image),
          "Bloom=0 draws nothing, leaves the frame as it was and releases the bloom levels");
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
    Check(!defaults.antialiasing && GetPrivateProfileIntW(L"CoAVolFog", L"Antialiasing", -1, shippedIni.c_str()) == 0,
          "SMAA antialiasing is off by default and in the shipped INI");
    GetPrivateProfileStringW(L"CoAVolFog", L"Bloom", L"", text, 16, shippedIni.c_str());
    Check(defaults.bloom == 0.0f && std::wcscmp(text, L"0") == 0, "bloom is off by default and in the shipped INI");
}
}
