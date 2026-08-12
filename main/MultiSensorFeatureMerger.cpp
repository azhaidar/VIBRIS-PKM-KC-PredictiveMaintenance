#include "MultiSensorFeatureMerger.h"
#include "config.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ===================================================================
// STATE INTERNAL — SATU-SATUNYA PEMILIK DATA GABUNGAN
// Driver TIDAK BOLEH lagi menulis dataMesinGlobal secara langsung.
// Semua penulisan wajib lewat fungsi updateXFeature() di bawah ini.
// ===================================================================

static SemaphoreHandle_t mergerMutex = NULL;

static float latestFeatures[FEAT_COUNT] = {0.0f, 0.0f, SUHU_DEFAULT_VALID};
static uint32_t lastUpdateTimestamp[FEAT_COUNT] = {0, 0, 0};

// Lazy-init mutex: aman dipanggil dari task manapun yang start duluan
static void ensureMutexInitialized() {
    if (mergerMutex == NULL) {
        mergerMutex = xSemaphoreCreateMutex();
    }
}

static void writeFeature(FeatureIndex idx, float value) {
    ensureMutexInitialized();
    if (xSemaphoreTake(mergerMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        latestFeatures[idx] = value;
        lastUpdateTimestamp[idx] = millis();
        xSemaphoreGive(mergerMutex);
    }
    // Kalau mutex gagal diambil dalam 50ms, update dilewati siklus ini.
    // Lebih aman drop satu sample daripada block task sensor selamanya.
}

// ===================================================================
// API PUBLIK — dipanggil oleh masing-masing Driver
// ===================================================================

void updateVibrationFeature(float rmsValue) {
    writeFeature(FEAT_VIBRATION, rmsValue);
}

void updateAudioFeature(float rmsValue) {
    writeFeature(FEAT_AUDIO, rmsValue);
}

void updateCurrentFeature(float rmsValue) {
    (void)rmsValue;  // Sensor arus nonaktif sementara (lihat ENABLE_ARUS_SENSOR di config.h).
                      // Tidak lagi ditulis ke vektor fitur Mahalanobis (sekarang 3 dimensi).
}

void updateTemperatureFeature(float value) {
    writeFeature(FEAT_TEMP, value);
}

// ===================================================================
// PEMBACAAN GABUNGAN — dipanggil oleh MahalanobisDetector, dll.
// ===================================================================

bool getMergedFeatures(SensorFeatures *output) {
    ensureMutexInitialized();

    if (xSemaphoreTake(mergerMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false; // Gagal ambil lock, jangan kasih data setengah-update
    }

    uint32_t now = millis();
    bool allFresh = true;

    for (int i = 0; i < FEAT_COUNT; i++) {
        // Overflow-safe: millis() wrap-around tetap benar karena unsigned subtraction
        if ((now - lastUpdateTimestamp[i]) > FEATURE_STALENESS_MS) {
            allFresh = false;
        }
    }

    output->rms_getaran = latestFeatures[FEAT_VIBRATION];
    output->rms_suara   = latestFeatures[FEAT_AUDIO];
    output->arus        = 0.0f;
    output->suhu        = latestFeatures[FEAT_TEMP];
    output->valid       = allFresh;

    xSemaphoreGive(mergerMutex);
    return allFresh;
}
