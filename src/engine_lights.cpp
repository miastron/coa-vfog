#include "engine_lights.h"

#include "client_memory.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace
{
constexpr uintptr_t kClientImageBase = 0x00400000;
constexpr uint32_t kClientTimestamp = 0x4C2452FE;
constexpr uintptr_t kScenePointLightBuckets = 0x24;
constexpr uintptr_t kSceneFrameStamp = 0x14;
constexpr uint32_t kHiddenModelFrames = 2;
constexpr float kHiddenLightViewInset = 2.0f;
constexpr uint32_t kPointLightBucketCount = 4096;
constexpr uint32_t kMaxPointLightBucketNodes = 512;
constexpr uint32_t kMaxPointLightNodes = 8192;
constexpr uint32_t kNativePointLightType = 1;
constexpr float kMaxWorldCoordinate = 1000000.0f;
constexpr float kMaxLightComponent = 10000.0f;
constexpr float kMaxUploadedLightComponent = 1000000.0f;
constexpr float kMaxAttenuationCoefficient = 1000000.0f;
constexpr uintptr_t kCameraWmoInstance = 0x00CD87A4;
constexpr uintptr_t kCameraWmoGroupCount = 0x00CDB0D8;
constexpr uintptr_t kCameraWmoGroupIds = 0x00CDB0DC;
constexpr uintptr_t kCameraInteriorFogBlend = 0x00D38B9C;
constexpr LightCaptureSources kClientLightSources = {
    engine::kWorldM2Scene, {kCameraWmoInstance, kCameraWmoGroupCount, kCameraWmoGroupIds, kCameraInteriorFogBlend}};
constexpr uint32_t kMaxCameraWmoGroups = 64;
constexpr uint32_t kMaxWmoGroups = 4096;

struct NativePointLight
{
    uintptr_t scene;
    uint32_t frameStamp;
    uint32_t type;
    float position[3];
    float viewPosition[3];
    float direction[3];
    float ambient[3];
    float color[3];
    float specular[3];
    float attenuation[3];
    uint32_t enabled;
    uintptr_t previousLink;
    uintptr_t next;
};

static_assert(sizeof(NativePointLight) == 0x6C);
static_assert(offsetof(NativePointLight, position) == 0xC);
static_assert(offsetof(NativePointLight, color) == 0x3C);
static_assert(offsetof(NativePointLight, attenuation) == 0x54);
static_assert(offsetof(NativePointLight, enabled) == 0x60);
static_assert(offsetof(NativePointLight, next) == 0x68);

bool ValidVector(const float values[3], float minimum, float maximum)
{
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(values[i]) || values[i] < minimum || values[i] > maximum)
            return false;
    return true;
}

double SquaredDistance(const float a[3], const float b[3])
{
    double result = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        const double delta = static_cast<double>(a[i]) - b[i];
        result += delta * delta;
    }
    return result;
}

float PeakColor(const float color[3])
{
    return std::max(color[0], std::max(color[1], color[2]));
}

float LinearChannel(float channel, float peak)
{
    const float gammaChannel = std::max(channel, 0.0f);
    if (peak <= 1.0f)
        return std::pow(gammaChannel, kLocalLightGamma);
    return std::pow(gammaChannel / peak, kLocalLightGamma) * peak;
}

bool Precedes(const LocalPointLight& a, const LocalPointLight& b, const float cameraPosition[3])
{
    const double aPriority = engine::LocalLightPriority(a, cameraPosition);
    const double bPriority = engine::LocalLightPriority(b, cameraPosition);
    if (aPriority != bPriority)
        return aPriority > bPriority;
    const double aDistance = SquaredDistance(a.position, cameraPosition);
    const double bDistance = SquaredDistance(b.position, cameraPosition);
    if (aDistance != bDistance)
        return aDistance < bDistance;
    for (int i = 0; i < 3; ++i)
        if (a.position[i] != b.position[i])
            return a.position[i] < b.position[i];
    for (int i = 0; i < 3; ++i)
        if (a.color[i] != b.color[i])
            return a.color[i] > b.color[i];
    for (int i = 0; i < 3; ++i)
        if (a.attenuation[i] != b.attenuation[i])
            return a.attenuation[i] < b.attenuation[i];
    return false;
}

bool InsertRankedLight(LocalPointLight* list, uint32_t& count, uint32_t capacity, const LocalPointLight& candidate,
                       const float cameraPosition[3])
{
    uint32_t index = 0;
    while (index < count && !Precedes(candidate, list[index], cameraPosition))
        ++index;
    if (index == capacity)
        return false;
    const uint32_t newCount = std::min(count + 1, capacity);
    for (uint32_t i = newCount - 1; i > index; --i)
        list[i] = list[i - 1];
    list[index] = candidate;
    count = newCount;
    return true;
}

struct ViewPlane
{
    float normal[3];
    float offset;
};

void ViewSpacePosition(const LocalPointLight& light, const LocalLightCamera& camera, float out[3])
{
    float relative[3];
    for (int i = 0; i < 3; ++i)
        relative[i] = light.position[i] - camera.position[i];
    const float* m = camera.cameraRelativeView;
    for (int j = 0; j < 3; ++j)
        out[j] = relative[0] * m[0 * 4 + j] + relative[1] * m[1 * 4 + j] + relative[2] * m[2 * 4 + j];
}

float ProjectionNear(const float* projection)
{
    const float divisor = 1.0f + projection[10];
    const float nearPlane = divisor != 0.0f ? -projection[14] / divisor : 0.0f;
    return std::isfinite(nearPlane) ? std::max(nearPlane, 0.0f) : 0.0f;
}

bool ProjectsView(const LocalLightCamera& camera)
{
    const float* p = camera.glProjection;
    return std::isfinite(p[0]) && std::isfinite(p[5]) && p[0] != 0.0f && p[5] != 0.0f;
}

bool SphereBeyondPlane(const float centre[3], float radius, const ViewPlane& plane)
{
    const float* n = plane.normal;
    const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(length > 0.0f))
        return false;
    const float signedDistance = (n[0] * centre[0] + n[1] * centre[1] + n[2] * centre[2] + plane.offset) / length;
    return signedDistance > radius;
}

bool SupportedLayoutUnsafe()
{
    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleA(nullptr));
    if (reinterpret_cast<uintptr_t>(base) != kClientImageBase)
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x1000)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != kClientTimestamp ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;
    const unsigned char sceneLoad[] = {0x8B, 0x3D, 0x4C, 0x75, 0xCD, 0x00};
    const unsigned char pointTypeAndBuckets[] = {
        0x83, 0x7E, 0x08, 0x01, 0x0F, 0x85, 0xC6, 0x00, 0x00, 0x00, 0x83, 0x78, 0x24, 0x00};
    const unsigned char pointLinks[] = {0x8D, 0x0C, 0xB8, 0x89, 0x4E, 0x64, 0x8B, 0x09, 0x8B, 0x56, 0x64};
    const unsigned char attenuationUpload[] = {
        0xD9, 0x40, 0x54, 0xD9, 0x5D, 0xD0, 0xD9, 0x40, 0x58, 0xD9, 0x5D, 0xD4, 0xD9, 0x40, 0x5C};
    const unsigned char interiorBlendStore[] = {0xD9, 0x15, 0x9C, 0x8B, 0xD3, 0x00};
    const unsigned char cameraGroups[] = {0x8B, 0x0D, 0xDC, 0xB0, 0xCD, 0x00, 0x8B, 0x14, 0xB1};
    const unsigned char interiorFlags[] = {0xF6, 0x40, 0x30, 0x48, 0x75, 0x23};
    const unsigned char loadedGroups[] = {
        0x83, 0xB9, 0xE0, 0x01, 0x00, 0x00, 0x00, 0x74, 0x19, 0x8B, 0x45, 0x08,
        0x8B, 0x84, 0x81, 0xF8, 0x01, 0x00, 0x00};
    return Matches(0x004F90EC, sceneLoad) && Matches(0x00834C8D, pointTypeAndBuckets) &&
           Matches(0x00834D3C, pointLinks) && Matches(0x00835539, attenuationUpload) &&
           Matches(0x007F1931, interiorBlendStore) && Matches(0x007A11B0, cameraGroups) &&
           Matches(0x007A11C7, interiorFlags) && Matches(0x007AEA83, loadedGroups);
}

bool SupportedLayout()
{
    __try
    {
        return SupportedLayoutUnsafe();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ModelStampsMatchUnsafe()
{
    const unsigned char sceneStampAdvance[] = {0x83, 0x46, 0x14, 0x01};
    const unsigned char lightStampSource[] = {0x8B, 0x53, 0x28, 0x8B, 0x42, 0x14};
    const unsigned char lightStampStore[] = {0x89, 0x46, 0x6C};
    const unsigned char staleLightTest[] = {0x8B, 0x41, 0x04, 0x8B, 0x55, 0xFC, 0x3B, 0x42, 0x14};
    return Matches(0x00821A2D, sceneStampAdvance) && Matches(0x00828B88, lightStampSource) &&
           Matches(0x00828B9C, lightStampStore) && Matches(0x0081E53F, staleLightTest);
}

bool ModelStampsMatch()
{
    __try
    {
        return ModelStampsMatchUnsafe();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ModelHiddenInView(const NativePointLight& native, uint32_t sceneStamp, const LocalLightCamera& camera)
{
    if (sceneStamp - native.frameStamp <= kHiddenModelFrames)
        return false;
    LocalPointLight centre;
    std::memcpy(centre.position, native.position, sizeof(centre.position));
    return PointLightMeetsView(centre, camera, -kHiddenLightViewInset);
}

bool LinkedIntoTable(const NativePointLight& native, uintptr_t scene, uintptr_t previousLink)
{
    return native.scene == scene && native.previousLink == previousLink;
}

LocalPointLight CapturedLight(const NativePointLight& native, uintptr_t address)
{
    LocalPointLight light;
    std::memcpy(light.position, native.position, sizeof(light.position));
    std::memcpy(light.color, native.color, sizeof(light.color));
    std::memcpy(light.attenuation, native.attenuation, sizeof(light.attenuation));
    light.enabled = native.enabled;
    light.nativeId = address;
    return light;
}

LocalLightCapture CapturePointLightsUnsafe(uintptr_t sceneSlot, const LocalLightCamera& camera,
                                           const PointLightUpload& upload, LocalLightInputs& out)
{
    const uintptr_t scene = Read<uintptr_t>(sceneSlot);
    if (!scene)
        return LocalLightCapture::Captured;
    if (!ValidPointer(scene, kScenePointLightBuckets + sizeof(uintptr_t)))
        return LocalLightCapture::DamagedTable;
    const uint32_t sceneStamp = Read<uint32_t>(scene + kSceneFrameStamp);
    const uintptr_t buckets = Read<uintptr_t>(scene + kScenePointLightBuckets);
    if (!buckets)
        return LocalLightCapture::Captured;
    if (!ValidPointer(buckets, kPointLightBucketCount * sizeof(uintptr_t)))
        return LocalLightCapture::DamagedTable;
    uint32_t nodeCount = 0;
    for (uint32_t bucket = 0; bucket < kPointLightBucketCount; ++bucket)
    {
        uintptr_t previousLink = buckets + bucket * sizeof(uintptr_t);
        uintptr_t address = Read<uintptr_t>(previousLink);
        uint32_t bucketNodes = 0;
        while (address)
        {
            if (++bucketNodes > kMaxPointLightBucketNodes || !ValidPointer(address, sizeof(NativePointLight)))
                return LocalLightCapture::DamagedTable;
            if (++nodeCount > kMaxPointLightNodes)
                return LocalLightCapture::TableTruncated;
            const NativePointLight native = Read<NativePointLight>(address);
            if (!LinkedIntoTable(native, scene, previousLink))
                return LocalLightCapture::DamagedTable;
            if (native.type != kNativePointLightType)
                ++out.skippedNonPoint;
            else if (!native.enabled)
                ++out.skippedDisabled;
            else if (upload.followObjects && ModelHiddenInView(native, sceneStamp, camera))
                ++out.skippedHidden;
            else
                engine::SelectLocalPointLightInView(out, CapturedLight(native, address), camera, upload);
            previousLink = address + offsetof(NativePointLight, next);
            address = native.next;
        }
    }
    const bool unchanged =
        Read<uintptr_t>(sceneSlot) == scene && Read<uintptr_t>(scene + kScenePointLightBuckets) == buckets;
    return unchanged ? LocalLightCapture::Captured : LocalLightCapture::TableChanged;
}

bool CaptureInteriorUnsafe(const CameraInteriorWords& words, LocalLightInputs& out)
{
    const uintptr_t instance = Read<uintptr_t>(words.instance);
    const uint32_t count = Read<uint32_t>(words.groupCount);
    if (!instance || !count)
        return true;
    if (count > kMaxCameraWmoGroups || !ValidPointer(instance, kWmoInstanceRoot + sizeof(uintptr_t)))
        return false;
    const uintptr_t root = Read<uintptr_t>(instance + kWmoInstanceRoot);
    if (!ValidPointer(root, kWmoRootGroups) || !Read<uint32_t>(root + kWmoRootLoaded))
        return false;
    const uint32_t groupCount = Read<uint32_t>(root + kWmoRootGroupCount);
    const uintptr_t ids = Read<uintptr_t>(words.groupIds);
    if (!groupCount || groupCount > kMaxWmoGroups || !ValidPointer(ids, count * sizeof(uint32_t)) ||
        !ValidPointer(root, kWmoRootGroups + groupCount * sizeof(uintptr_t)))
        return false;
    bool cameraInterior = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t groupId = Read<uint32_t>(ids + i * sizeof(uint32_t));
        if (groupId >= groupCount)
            return false;
        const uintptr_t group = Read<uintptr_t>(root + kWmoRootGroups + groupId * sizeof(uintptr_t));
        if (!ValidPointer(group, kWmoGroupLoaded + sizeof(uint32_t)))
            return false;
        if ((Read<uint32_t>(group + kWmoGroupLoaded) & 1) &&
            !(Read<uint32_t>(group + kWmoGroupFlags) & kWmoExteriorFogFlags))
            cameraInterior = true;
    }
    const float weight = Read<float>(words.fogBlend);
    const bool unchanged = Read<uintptr_t>(words.instance) == instance &&
                           Read<uint32_t>(words.groupCount) == count && Read<uintptr_t>(words.groupIds) == ids;
    if (!unchanged)
        return false;
    out.cameraInterior = cameraInterior;
    if (cameraInterior && std::isfinite(weight))
        out.interiorBlend = std::clamp(weight, 0.0f, 1.0f);
    return true;
}

LocalLightCapture CaptureInputsUnsafe(const LightCaptureSources& sources, const LocalLightCamera& camera,
                                      bool withPointLights, const PointLightUpload& upload, LocalLightInputs& out)
{
    const LocalLightCapture points =
        withPointLights ? CapturePointLightsUnsafe(sources.sceneSlot, camera, upload, out)
                        : LocalLightCapture::Captured;
    const bool interior = CaptureInteriorUnsafe(sources.interior, out);
    if (points != LocalLightCapture::Captured)
        return points;
    return interior ? LocalLightCapture::Captured : LocalLightCapture::InteriorRejected;
}

LocalLightCapture CaptureInputsGuarded(const LightCaptureSources& sources, const LocalLightCamera& camera,
                                       bool withPointLights, const PointLightUpload& upload, LocalLightInputs& out)
{
    __try
    {
        return CaptureInputsUnsafe(sources, camera, withPointLights, upload, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return LocalLightCapture::ReadFault;
    }
}

LocalLightCapture CapturePointLightTableGuarded(uintptr_t sceneSlot, const LocalLightCamera& camera,
                                                const PointLightUpload& upload, LocalLightInputs& out)
{
    __try
    {
        return CapturePointLightsUnsafe(sceneSlot, camera, upload, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return LocalLightCapture::ReadFault;
    }
}

bool CandidateInputsValid(const LocalLightInputs& out, const LocalPointLight& light, const float cameraPosition[3],
                          const PointLightUpload& upload)
{
    return out.pointLightCount <= kMaxLocalPointLights && out.candidateCount <= kLocalLightCandidatePool &&
           ValidVector(cameraPosition, -kMaxWorldCoordinate, kMaxWorldCoordinate) &&
           ValidVector(light.position, -kMaxWorldCoordinate, kMaxWorldCoordinate) &&
           ValidVector(light.color, 0.0f, kMaxLightComponent) && std::isfinite(upload.intensity) &&
           upload.intensity >= 0.0f;
}

bool BoundCandidate(const LocalPointLight& light, const PointLightUpload& upload, LocalPointLight& candidate)
{
    candidate = light;
    engine::UploadedPointLightColor(candidate.color, upload, candidate.uploadedColor);
    candidate.cutoff = engine::PointLightCutoff(candidate.uploadedColor, candidate.attenuation);
    return candidate.cutoff > 0.0f;
}

bool RankCandidate(LocalLightInputs& out, const LocalPointLight& candidate, const float cameraPosition[3])
{
    InsertRankedLight(out.candidates, out.candidateCount, kLocalLightCandidatePool, candidate, cameraPosition);
    return InsertRankedLight(out.pointLights, out.pointLightCount, kMaxLocalPointLights, candidate, cameraPosition);
}
}

bool PointLightMeetsView(const LocalPointLight& light, const LocalLightCamera& camera, float margin)
{
    if (!ProjectsView(camera))
        return true;
    float centre[3];
    ViewSpacePosition(light, camera, centre);
    const float* p = camera.glProjection;
    const ViewPlane planes[] = {
        {{0.0f, 0.0f, -1.0f}, ProjectionNear(p)},
        {{p[0], 0.0f, p[8] - 1.0f}, 0.0f},
        {{-p[0], 0.0f, -p[8] - 1.0f}, 0.0f},
        {{0.0f, p[5], p[9] - 1.0f}, 0.0f},
        {{0.0f, -p[5], -p[9] - 1.0f}, 0.0f},
    };
    for (const ViewPlane& plane : planes)
        if (SphereBeyondPlane(centre, light.cutoff + margin, plane))
            return false;
    return true;
}

namespace engine
{
void UploadedPointLightColor(const float captured[3], const PointLightUpload& upload, float uploaded[3])
{
    const float peak = PeakColor(captured);
    for (int channel = 0; channel < 3; ++channel)
        uploaded[channel] = (upload.linear ? LinearChannel(captured[channel], peak)
                                           : std::max(captured[channel], 0.0f)) * upload.intensity;
}

float PointLightCutoff(const float color[3], const float attenuation[3])
{
    if (!ValidVector(color, 0.0f, kMaxUploadedLightComponent) ||
        !ValidVector(attenuation, 0.0f, kMaxAttenuationCoefficient))
        return 0.0f;
    const double peak = PeakColor(color);
    const double target = peak / kLocalPointLightContributionCutoff;
    if (peak <= 0.0 || target <= std::max(static_cast<double>(attenuation[0]), 1.0))
        return 0.0f;
    const double remaining = target - attenuation[0];
    double radius = kMaxLocalPointLightRadius;
    if (attenuation[2] > 0.0f)
    {
        const double linear = attenuation[1];
        radius = 2.0 * remaining /
                 (linear + std::sqrt(linear * linear + 4.0 * attenuation[2] * remaining));
    }
    else if (attenuation[1] > 0.0f)
        radius = remaining / attenuation[1];
    return static_cast<float>(std::min(radius, static_cast<double>(kMaxLocalPointLightRadius)));
}

double LocalLightPriority(const LocalPointLight& light, const float cameraPosition[3])
{
    const double distanceSquared = SquaredDistance(light.position, cameraPosition);
    const double denominator = light.attenuation[0] + light.attenuation[1] * std::sqrt(distanceSquared) +
                               light.attenuation[2] * distanceSquared;
    return PeakColor(light.uploadedColor) / std::max(denominator, 1.0);
}

bool SelectLocalPointLight(LocalLightInputs& out, const LocalPointLight& light, const float cameraPosition[3],
                           const PointLightUpload& upload)
{
    LocalPointLight candidate;
    return CandidateInputsValid(out, light, cameraPosition, upload) && BoundCandidate(light, upload, candidate) &&
           RankCandidate(out, candidate, cameraPosition);
}

bool SelectLocalPointLightInView(LocalLightInputs& out, const LocalPointLight& light,
                                 const LocalLightCamera& camera, const PointLightUpload& upload)
{
    LocalPointLight candidate;
    return CandidateInputsValid(out, light, camera.position, upload) && BoundCandidate(light, upload, candidate) &&
           PointLightMeetsView(candidate, camera) && RankCandidate(out, candidate, camera.position);
}

bool CapturedLightsUsable(LocalLightCapture capture)
{
    return capture != LocalLightCapture::UnsupportedClient && capture != LocalLightCapture::CameraOutOfRange;
}

const char* LocalLightCaptureName(LocalLightCapture capture)
{
    switch (capture)
    {
    case LocalLightCapture::Captured:
        return "captured";
    case LocalLightCapture::UnsupportedClient:
        return "client code differs from build 12340";
    case LocalLightCapture::CameraOutOfRange:
        return "camera position out of range";
    case LocalLightCapture::DamagedTable:
        return "damaged light table";
    case LocalLightCapture::TableTruncated:
        return "light table walk stopped at 8192 nodes";
    case LocalLightCapture::TableChanged:
        return "table changed during the walk";
    case LocalLightCapture::ReadFault:
        return "read fault";
    case LocalLightCapture::InteriorRejected:
        return "camera interior groups rejected";
    }
    return "unknown";
}

LocalLightCapture CapturePointLightTable(uintptr_t sceneSlot, const LocalLightCamera& camera,
                                         const PointLightUpload& upload, LocalLightInputs& out)
{
    LocalLightInputs captured;
    const LocalLightCapture result = CapturePointLightTableGuarded(sceneSlot, camera, upload, captured);
    out = CapturedLightsUsable(result) ? captured : LocalLightInputs{};
    out.capture = result;
    return result;
}

LocalLightCapture CaptureLightInputsFrom(const LightCaptureSources& sources, const LocalLightCamera& camera,
                                         bool withPointLights, const PointLightUpload& upload, LocalLightInputs& out)
{
    LocalLightInputs captured;
    const LocalLightCapture result = CaptureInputsGuarded(sources, camera, withPointLights, upload, captured);
    out = CapturedLightsUsable(result) ? captured : LocalLightInputs{};
    out.capture = result;
    return result;
}

bool CaptureLocalLightInputs(const LocalLightCamera& camera, bool withPointLights, const PointLightUpload& upload,
                             LocalLightInputs& out)
{
    out = {};
    static const bool supported = SupportedLayout();
    if (!supported)
        out.capture = LocalLightCapture::UnsupportedClient;
    else if (!ValidVector(camera.position, -kMaxWorldCoordinate, kMaxWorldCoordinate))
        out.capture = LocalLightCapture::CameraOutOfRange;
    else
    {
        static const bool stamped = ModelStampsMatch();
        PointLightUpload capture = upload;
        capture.followObjects = upload.followObjects && stamped;
        CaptureLightInputsFrom(kClientLightSources, camera, withPointLights, capture, out);
    }
    return out.capture == LocalLightCapture::Captured;
}
}
