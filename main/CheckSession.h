#pragma once
#include "SharedTypes.h"

void startCheckSession(int slot);
void updateCheckSession(DetectionResult result, float currentTemp);
bool isCheckSessionActive();
CheckSessionSummary getCheckSessionSummary();
void saveCheckSummaryToFlash(CheckSessionSummary s);
bool loadCheckSummaryFromFlash(int slot, CheckSessionSummary *out);
void deleteCheckSummaryFromFlash(int slot);
