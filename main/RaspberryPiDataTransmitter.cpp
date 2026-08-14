// RaspberryPiDataTransmitter.cpp
#include "RaspberryPiDataTransmitter.h"
#include <Arduino.h>
#include "DualCoreTaskScheduler.h"
#include "DriverArus.h"
#include "DriverSuhu.h"

void Transmitter_Init(long baudRate) {
    // Tidak perlu Serial1.begin() — Serial (USB) sudah di-init di setup() main.ino
    // dengan baudRate yang sama (115200). Fungsi ini sengaja jadi no-op,
    // dipertahankan agar kontrak fungsi tetap konsisten kalau nanti pindah
    // ke UART1/GPIO terpisah (produk final).
    (void)baudRate;
    Serial.println(F("[Transmitter] Mode USB — data dikirim lewat port Serial yang sama dengan debug."));
}
void Transmitter_SendResult(SensorFeatures features, DetectionResult result, const char* groundTruthLabel) {
    float rmsX = 0.0f, rmsY = 0.0f, rmsZ = 0.0f;
    Scheduler_GetLatestAxisRMS(&rmsX, &rmsY, &rmsZ);

    float bandEnergies[4];
    Scheduler_GetLatestBandEnergies(bandEnergies);

    float audioBandEnergies[AUDIO_BAND_COUNT];
    Scheduler_GetLatestAudioBandEnergies(audioBandEnergies);

    #if ENABLE_ARUS_SENSOR
        float arusValue = features.arus;
        float arusRawADC = DriverArus_GetLastRawADC();
    #else
        float arusValue = 0.0f;
        float arusRawADC = 0.0f;
    #endif

    Serial.printf(
        "{"
        "\"rms_v\":%.4f,\"rms_x\":%.4f,\"rms_y\":%.4f,\"rms_z\":%.4f,"
        "\"rms_a\":%.6f,\"cur\":%.4f,\"cur_raw_adc\":%.2f,"
        "\"temp\":%.2f,\"temp_raw\":%.3f,"
        "\"rpm\":%.2f,\"snr\":%.2f,\"severity\":%.3f,\"status\":\"%s\","
        "\"e_unbalance\":%.4f,\"e_misalign\":%.4f,\"e_bpfo\":%.4f,\"e_bpfi\":%.4f,"
        "\"diagnosis\":\"%s\",\"diag_conf\":%.2f,"
        "\"e_audio_low\":%.4f,\"e_audio_mid\":%.4f,\"e_audio_high\":%.4f,"
        "\"audio_diagnosis\":\"%s\",\"audio_diag_conf\":%.2f,"
        "\"roughness\":%.6f,\"brightness\":%.6f,"
        "\"health_score\":%.1f,\"trend\":\"%s\","
        "\"servis_estimasi\":\"%s\","
        "\"ml_label\":\"%s\",\"ml_conf\":%.2f,"
        "\"ground_truth\":\"%s\""
        "}\n",

        features.rms_getaran, rmsX, rmsY, rmsZ,
        features.rms_suara, arusValue, arusRawADC,  
        features.suhu, DriverSuhu_GetLastRawTemp(),
        result.rpm_estimated, Scheduler_GetLatestSNR(), result.mahalanobis_D2, result.status_label,
        bandEnergies[0], bandEnergies[1], bandEnergies[2], bandEnergies[3],
        result.diagnosis_label, result.diagnosis_confidence,
        audioBandEnergies[0], audioBandEnergies[1], audioBandEnergies[2],
        result.audio_diagnosis_label, result.audio_diagnosis_confidence,
        Scheduler_GetLatestRoughness(), Scheduler_GetLatestBrightness(),
        result.health_score, result.trend,
        result.servis_estimasi,
        result.ml_label, result.ml_confidence,
        groundTruthLabel
    );
}