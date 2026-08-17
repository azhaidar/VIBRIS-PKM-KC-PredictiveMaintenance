// DualCoreTaskScheduler.h
#pragma once
#include "SharedTypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
Mengatur pembagian kerja dua core ESP32-S3 lewat FreeRTOS task
pinning. Tanpa modul ini, semua kode default jalan sekuensial di
Core 1 saja, Core 0 nganggur (kecuali WiFi aktif) — buang setengah
kapasitas komputasi device.

Pembagian:
Core 0 — akuisisi sensor (4 Driver: Arus/SCT, Getaran/LIS3DH,
Suara/INMP441, Suhu/DS18B20). I/O-bound, butuh timing sampling
presisi dan konsisten, terutama untuk getaran (target 1000Hz sesuai
Nyquist untuk klaim 0-500Hz di proposal BAB 3.3).

Core 1 — compute-heavy: FFTProcessor, RPMEstimator, CovarianceMatrixSolver,
InitialBaselineCalibrator, AdaptiveBaselineLearner, MahalanobisDetector,
DiagnosisClassifier, RaspberryPiDataTransmitter.

Alasan pemisahan: kalau akuisisi sensor dan komputasi berat
digabung di core yang sama, sampling getaran bisa jitter (interval
antar-sample tidak konsisten) karena tertunda proses FFT/Mahalanobis
yang sedang jalan. Sampling tidak konsisten = distorsi FFT
(spectral leakage) = RPM estimation dan seluruh deteksi meleset.

Komunikasi antar-core memakai FreeRTOS Queue (VibrationBuffer dan
struct sejenis), bukan variabel global biasa — variabel global lintas
task/core rawan race condition karena dua core bisa baca-tulis
bersamaan tanpa sinkronisasi.
*/
void Scheduler_InitTasks();
QueueHandle_t Scheduler_GetVibrationQueue();
QueueHandle_t Scheduler_GetAudioQueue();      
float Scheduler_GetLatestRPM();   // <-- pastikan baris ini ADA di .h, bukan cuma di .cpp
float Scheduler_GetLatestSNR();
void Scheduler_GetLatestBandEnergies(float *dest);   // TAMBAHAN
void Scheduler_GetLatestAudioBandEnergies(float *dest);
void Scheduler_GetLatestAxisRMS(float *xOut, float *yOut, float *zOut);
// TAMBAH DUA BARIS INI:
float Scheduler_GetLatestRoughness();
float Scheduler_GetLatestBrightness();