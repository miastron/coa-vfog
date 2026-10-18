#include "image_quality.h"

#include "d3d9_wrap.h"
#include "engine.h"
#include "log.h"
#include "status_log.h"

#include <windows.h>

namespace
{
constexpr const char* kImageOff = "off";
constexpr const char* kStoppedAfterException = "stopped after an exception";

struct CapturedWorld
{
    bool valid = false;
    D3DVIEWPORT9 viewport = {};
};

CapturedWorld g_world;
FogDevice* g_drawnDevice = nullptr;
bool g_failed = false;
StatusLog g_log;
ImageQualityStatus g_status;

int ImageQualityFilter(unsigned code, const char* where)
{
    VF_LOG_ERROR("exception 0x%08X in %s; antialiasing and sharpening are off for this session, the fog continues", code, where);
    return EXCEPTION_EXECUTE_HANDLER;
}

void Record(bool drawn, const char* state)
{
    g_status.drawn = drawn;
    g_status.state = state;
    if (drawn)
    {
        g_log.Drawn();
        return;
    }
    const StatusLogLine line = g_log.Skip(state);
    if (line.write)
        LogWrite(line.level, "antialiasing and sharpening skipped: %s", state);
}

void ReleaseWhenOff(FogDevice* current)
{
    FogDevice* device = g_drawnDevice;
    g_drawnDevice = nullptr;
    if (device && device == current)
        ReleaseImage(device);
    g_status = {false, kImageOff};
}

void EnhanceFrame(FogDevice* (*currentDevice)(), const CapturedWorld& world)
{
    FogDevice* device = currentDevice();
    const ImageSettings settings = ImageSettingsOf(GlobalConfig().Get());
    if (!settings.Any())
    {
        ReleaseWhenOff(device);
        return;
    }
    if (!world.valid)
        return;
    const char* skip = "";
    if (EnhanceWorldImage(device, world.viewport, settings, &skip))
    {
        g_drawnDevice = device;
        Record(true, "");
    }
    else
        Record(false, skip && *skip ? skip : "the image pass could not draw");
}
}

ImageSettings ImageSettingsOf(const Config& cfg)
{
    ImageSettings settings;
    if (!cfg.enable)
        return settings;
    settings.antialiasing = cfg.antialiasing;
    settings.sharpening = cfg.sharpening;
    return settings;
}

void ImageQualityAtWorldDone()
{
    D3DVIEWPORT9 viewport = {};
    g_world.valid = engine::OpaqueViewport(viewport);
    g_world.viewport = viewport;
}

void ImageQualityAtFrameEnd(FogDevice* (*currentDevice)())
{
    const CapturedWorld world = g_world;
    g_world = CapturedWorld();
    if (g_failed)
    {
        g_status = {false, kStoppedAfterException};
        return;
    }
    __try
    {
        EnhanceFrame(currentDevice, world);
    }
    __except (ImageQualityFilter(GetExceptionCode(), "the image pass"))
    {
        g_failed = true;
    }
}

ImageQualityStatus LastImageQualityStatus()
{
    return g_status;
}
