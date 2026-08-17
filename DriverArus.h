// DriverArus.h
#pragma once
#include <Arduino.h>
#include "SharedTypes.h" // Menyediakan akses ke struct SensorFeatures lintas file
//arus gunakan 2 resisto dan 1 kapasitor ditenga
// ===================================================================
// PROTOTIPE FUNGSI UTAMA (TASK FREERTOS)
// ===================================================================
float DriverArus_GetLastRawADC();
void TaskDriverArus(void *pvParameters);
//end DriverArus.h
