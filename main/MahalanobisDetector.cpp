

// Nilai kritis chi-square, df=4 (4 fitur sensor), sesuai dokumentasi header:
// 95% confidence = 9.49, 99% confidence = 13.28. Standar statistik, bukan tebakan.

// Baseline energi per-band dipakai DiagnosisClassifier (lihat header:
// modul ini menjawab APAKAH menyimpang, DiagnosisClassifier menjawab DI
// BAGIAN MANA). Disuplai dari luar lewat setDiagnosisBandBaseline() --
// modul ini sendiri tidak tahu cara mengkalibrasi, hanya memakainya.

    // LAPISAN LANJUTAN: begitu D^2 dihitung, tanya DiagnosisClassifier:
    // menyimpang di band frekuensi MANA (Unbalance/Misalignment/BPFO/BPFI).
    // FIX: kalau RPM tidak reliable (SNR rendah/motor mati), FFTProcessor
    // menge-nol-kan bandEnergies -- dan itu SELALU dibaca "NORMAL" oleh
    // Diagnosis_Classify (z-score energi 0 vs baseline malah negatif, di
    // bawah ambang). Data lapangan buktikan ini: 766 dari 1020 baris status
    // "Bahaya" punya diagnosis "NORMAL" bersamaan (rpm=0). Guard di bawah
    // mencegah label yang diam-diam salah -- kalau sinyal tidak reliable,
    // diagnosis tetap "N/A" (default), bukan "NORMAL" yang menyesatkan.

#include "MahalanobisDetector.h"
#include "MultiSensorFeatureMerger.h"
#include "AdaptiveBaselineLearner.h"
#include "CovarianceMatrixSolver.h"
#include "DualCoreTaskScheduler.h"
#include "DiagnosisClassifier.h"
#include "DriverAudioDiagnosisClassifier.h"   // BARU
#include "InitialBaselineCalibrator.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>   // BARU: untuk sqrtf

#define CHI_SQUARE_95 7.815f
#define CHI_SQUARE_99 11.345f
float getChiSquare99() {
    return CHI_SQUARE_99;
}
#define BAND_LEARNING_RATE 0.01f   // BARU

// GANTI nama diagBandStd -> diagBandVar (disimpan sebagai VARIANCE, bukan std,
// supaya EMA-nya matematis benar; di-sqrt() jadi std pas dipakai)
static float diagBandMean[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static float diagBandVar[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
static bool  diagBaselineReady = false;

// BARU: baseline band audio, placeholder, konvergen otomatis via EMA
static float audioBandMean[AUDIO_BAND_COUNT] = {0.20f, 0.20f, 0.20f};
static float audioBandVar[AUDIO_BAND_COUNT]  = {0.01f, 0.01f, 0.01f};

void setDiagnosisBandBaseline(float bandMean[4], float bandStd[4]) {
    for (int i = 0; i < 4; i++) {
        diagBandMean[i] = bandMean[i];
        diagBandVar[i]  = bandStd[i] * bandStd[i];   // input std, disimpan sebagai variance
    }
    diagBaselineReady = true;
}
// TAMBAHKAN fungsi baru ini:
void setAudioBandBaseline(float mean[AUDIO_BAND_COUNT], float std[AUDIO_BAND_COUNT]) {
    for (int i = 0; i < AUDIO_BAND_COUNT; i++) {
        audioBandMean[i] = mean[i];
        audioBandVar[i]  = std[i] * std[i];
    }
}

// BARU: EMA generik band mean/variance, dipakai untuk band getaran (n=4) & audio (n=3)
static void updateBandBaselineIfNormal(float mean[], float variance[], int n,
                                        float currentEnergies[], bool isNormal) {
    if (!isNormal) return;
    for (int i = 0; i < n; i++) {
        mean[i] += BAND_LEARNING_RATE * (currentEnergies[i] - mean[i]);
        float diff = currentEnergies[i] - mean[i];
        variance[i] += BAND_LEARNING_RATE * (diff * diff - variance[i]);
    }
}

const char* classifyStatusFromD2(float d2Value) {
    if (d2Value <= CHI_SQUARE_95) return "Normal";
    if (d2Value <= CHI_SQUARE_99) return "Waspada";
    return "Bahaya";
}

DetectionResult runDetectionCycle() {
    DetectionResult result;
    static unsigned long baselineReadyTimestamp = 0;
    static bool wasReady = false;
    result.rpm_estimated = 0.0f;
    result.mahalanobis_D2 = 0.0f;
    result.diagnosis_confidence = 0.0f;
    result.audio_diagnosis_confidence = 0.0f;   // BARU
    strncpy(result.status_label, "Unknown", sizeof(result.status_label) - 1);
    result.status_label[sizeof(result.status_label) - 1] = '\0';
    strncpy(result.diagnosis_label, "N/A", sizeof(result.diagnosis_label) - 1);
    result.diagnosis_label[sizeof(result.diagnosis_label) - 1] = '\0';
    strncpy(result.audio_diagnosis_label, "N/A", sizeof(result.audio_diagnosis_label) - 1);   // BARU
    result.audio_diagnosis_label[sizeof(result.audio_diagnosis_label) - 1] = '\0';

    if (!isBaselineLearnerReady()) {
        strncpy(result.status_label, "NotCalibrated", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
        return result;
    }
    if (!wasReady) {
        baselineReadyTimestamp = millis();
        wasReady = true;
    }

    SensorFeatures merged;
    bool fresh = getMergedFeatures(&merged);
    if (!fresh) {
        strncpy(result.status_label, "SensorFault", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
        return result;
    }

    float currentFeatures[3] = {
        merged.rms_getaran, Scheduler_GetLatestRoughness(), merged.suhu
    };

    float baselineMean[3];
    float sigmaInverse[3][3];
    getCurrentBaseline(baselineMean, sigmaInverse);

    // GANTI: getFeatureStdDev() (statis) -> getCurrentStdDev() (adaptif, SUDAH ADA
    // di AdaptiveBaselineLearner.cpp dari awal, cuma belum dipakai di sini)
    float featureStdDev[3];
    getCurrentStdDev(featureStdDev);

    float currentFeaturesStd[3];
    float zeroMean[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; i++) {
        currentFeaturesStd[i] = (currentFeatures[i] - baselineMean[i]) / featureStdDev[i];
    }

    float d2 = computeMahalanobisQuadraticForm(currentFeaturesStd, zeroMean, sigmaInverse);
    const char* label = classifyStatusFromD2(d2);
    bool isNormal = (strcmp(label, "Normal") == 0);

    // GANTI: currentFeaturesStd -> currentFeatures (RAW) -- sesuai kontrak
    // updateBaselineIfNormal() yang sudah direvisi di AdaptiveBaselineLearner.cpp
    updateBaselineIfNormal(currentFeatures, isNormal);

    result.rpm_estimated = Scheduler_GetLatestRPM();
    result.mahalanobis_D2 = d2;
    strncpy(result.status_label, label, sizeof(result.status_label) - 1);
    result.status_label[sizeof(result.status_label) - 1] = '\0';
    #if ENABLE_RPM_DIAGNOSIS
    if (diagBaselineReady && result.rpm_estimated > 0.0f) {
        float bandEnergies[4];
        Scheduler_GetLatestBandEnergies(bandEnergies);

        float diagBandStd[4];   // BARU: konversi variance->std tiap siklus
        for (int i = 0; i < 4; i++) diagBandStd[i] = sqrtf(diagBandVar[i] > 1e-8f ? diagBandVar[i] : 1e-8f);

        char diagLabel[20];
        float diagConfidence = 0.0f;
        Diagnosis_Classify(bandEnergies, diagBandMean, diagBandStd, diagLabel, &diagConfidence);

        strncpy(result.diagnosis_label, diagLabel, sizeof(result.diagnosis_label) - 1);
        result.diagnosis_label[sizeof(result.diagnosis_label) - 1] = '\0';
        result.diagnosis_confidence = diagConfidence;

        updateBandBaselineIfNormal(diagBandMean, diagBandVar, 4, bandEnergies, isNormal);   // BARU
    }
    #endif
    // BARU: seluruh blok ini -- diagnosis audio, modul lama yang baru disambung
    {
        float audioBandEnergies[AUDIO_BAND_COUNT];
        Scheduler_GetLatestAudioBandEnergies(audioBandEnergies);

        float audioBandStd[AUDIO_BAND_COUNT];
        for (int i = 0; i < AUDIO_BAND_COUNT; i++) audioBandStd[i] = sqrtf(audioBandVar[i] > 1e-8f ? audioBandVar[i] : 1e-8f);

        char audioLabel[20];
        float audioConf = 0.0f;
        DriverAudioDiagnosis_Classify(audioBandEnergies, audioBandMean, audioBandStd, audioLabel, &audioConf);

        strncpy(result.audio_diagnosis_label, audioLabel, sizeof(result.audio_diagnosis_label) - 1);
        result.audio_diagnosis_label[sizeof(result.audio_diagnosis_label) - 1] = '\0';
        result.audio_diagnosis_confidence = audioConf;

        updateBandBaselineIfNormal(audioBandMean, audioBandVar, AUDIO_BAND_COUNT, audioBandEnergies, isNormal);
    }

    return result;
}