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
void updateKurtosisFeature(float value) {
    writeFeature(FEAT_KURTOSIS, value);
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

// void updateCurrentFeature(float rmsValue) {
//     (void)rmsValue;  // Sensor arus nonaktif sementara (lihat ENABLE_ARUS_SENSOR di config.h).
//                       // Tidak lagi ditulis ke vektor fitur Mahalanobis (sekarang 3 dimensi).
// }

// BARU: arus AKTIF LAGI, tapi jalurnya TERPISAH -- HANYA buat TinyML,
// SENGAJA TIDAK ditulis ke FEAT_COUNT/writeFeature() supaya TIDAK ikut
// masuk ke vektor Mahalanobis (yang tetap 3 dimensi: getaran, suara, suhu).
static volatile float latestArusForTinyML = 0.0f;

void updateCurrentFeature(float rmsValue) {
    latestArusForTinyML = rmsValue;
}

float getLatestArusForTinyML() {
    return latestArusForTinyML;
}

void updateTemperatureFeature(float value) {
    writeFeature(FEAT_TEMP, value);
}

// ===================================================================
// PEMBACAAN GABUNGAN — dipanggil oleh MahalanobisDetector, dll.
// ===================================================================
// ===================================================================
// LAJU PERUBAHAN SUHU (dT/dt) -- fitur ke-3 Mahalanobis, gantikan suhu absolut.
// Alasan: suhu absolut naik wajar saat motor manasin (bukan anomali),
// bikin D2 meledak. Yang menandai bahaya itu suhu MELONJAK MENDADAK
// (dT besar), bukan suhu tinggi. EMA smoothing meredam noise dT.
// ===================================================================
static float lastTempForRate = -999.0f;   // -999 = belum ada bacaan sebelumnya
static uint32_t lastTempRateTime = 0;
static float smoothedTempRate = 0.0f;
#define TEMP_RATE_EMA_ALPHA 0.2f   // 0-1: makin kecil makin halus (tuning di sini)

float getSmoothedTempRate(float currentTemp) {
    uint32_t now = millis();

    if (lastTempForRate < -900.0f) {
        // bacaan pertama: belum bisa hitung laju, anggap 0
        lastTempForRate = currentTemp;
        lastTempRateTime = now;
        return 0.0f;
    }

    float dtSeconds = (now - lastTempRateTime) / 1000.0f;
    if (dtSeconds < 0.001f) return smoothedTempRate;   // hindari bagi nol

    float rawRate = (currentTemp - lastTempForRate) / dtSeconds;   // derajat/detik
    // EMA smoothing: rate baru = alpha*rawRate + (1-alpha)*rate lama
    smoothedTempRate = TEMP_RATE_EMA_ALPHA * rawRate + (1.0f - TEMP_RATE_EMA_ALPHA) * smoothedTempRate;

    lastTempForRate = currentTemp;
    lastTempRateTime = now;
    return smoothedTempRate;
}
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
    output->kurtosis    = latestFeatures[FEAT_KURTOSIS];
    output->valid       = allFresh;

    xSemaphoreGive(mergerMutex);
    return allFresh;
}
