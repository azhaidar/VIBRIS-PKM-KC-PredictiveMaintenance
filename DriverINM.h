// DriverINM.h
#pragma once

#include <Arduino.h>
#include "SharedTypes.h" // Menyediakan akses ke struct SensorFeatures lintas file

// ===================================================================
// PROTOTIPE FUNGSI UTAMA (TASK FREERTOS)
// ===================================================================
/**
 * @brief Task FreeRTOS untuk melakukan pembacaan stream audio digital dari mikrofon INMP441 via I2S DMA.
 * @param pvParameters Pointer mentah (void*) yang WAJIB di-cast ke (SensorFeatures*)
 * untuk memperbarui parameter rms_suara secara real-time.
 * @note Task ini dieksekusi secara periodik di CORE 0 (DSP & High-Speed Sampling) dengan prioritas tinggi.
 */
void TaskDriverINM(void *pvParameters);
