#pragma once
#include "SharedTypes.h"

void FFTProcessor_Init();
void FFTProcessor_Process(VibrationBuffer *input, SensorFeatures *features, float *rpm_out, float *bandEnergies_out, float *snr_out);
void setBearingType(bool rollingBearing);