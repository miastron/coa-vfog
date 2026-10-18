#pragma once

#include "config.h"
#include "image_renderer.h"

class FogDevice;

struct ImageQualityStatus
{
    bool drawn = false;
    const char* state = "";
};

ImageSettings ImageSettingsOf(const Config& cfg);
void ImageQualityAtWorldDone();
void ImageQualityAtFrameEnd(FogDevice* (*currentDevice)());
ImageQualityStatus LastImageQualityStatus();
