// DriverSuhu.h
#pragma once
#include <Arduino.h>
#include "SharedTypes.h" // Menyediakan akses ke struct SensorFeatures lintas file
float DriverSuhu_GetLastRawTemp();
void TaskDriverSuhu(void *pvParameters);
