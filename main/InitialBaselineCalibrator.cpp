//"InitialBaselineCalibrator.cpp
#include "InitialBaselineCalibrator.h"
#include "CovarianceMatrixSolver.h"
#include "InitialBaselineCalibrator.h"
#include "CovarianceMatrixSolver.h"
#include "MultiSensorFeatureMerger.h"
#include <Preferences.h>
#include <Arduino.h>
#include <Preferences.h>
#include <Arduino.h>
#include "config.h"


// FIX: kalibrasi sekarang digerbang WAKTU (180 detik nyata via millis() di
// main.ino), bukan jumlah sample -- karena rate loop() terbukti TIDAK
// konstan (data lapangan: 1-5 sample/detik). Buffer diperbesar supaya
// cukup menampung sample terbanyak yang mungkin masuk dalam 180 detik,
// bahkan setelah fix DriverArus.cpp bikin rate naik mendekati 10/detik.
#define CALIBRATION_MAX_SAMPLES 300

bool addBandEnergyCalibrationSample(float bandEnergies[4]);
void computeBandEnergyBaseline(float meanOutput[4], float stdOutput[4]);
void saveBandBaselineToFlash(float mean[4], float std[4]);
bool loadBandBaselineFromFlash(float meanOutput[4], float stdOutput[4]);

static float calibrationBuffer[CALIBRATION_MAX_SAMPLES][4];
static int   calibrationSampleCount = 0;
static bool  calibrationActive = false;
static float featureStdDev[4] = {1.0f, 1.0f, 1.0f};

static Preferences flashStorage;
static const char* NVS_NAMESPACE = "baseline";
static bool lastCalibrationValid = false;

bool isLastCalibrationValid() {
    return lastCalibrationValid;
}

void startCalibrationPhase() {
    lastCalibrationValid = false;
    calibrationSampleCount = 0;
    calibrationActive = true;
    Serial.println(F("[Calibrator] Fase kalibrasi dimulai — pastikan mesin dalam kondisi NORMAL."));
}

static float bandCalibrationBuffer[CALIBRATION_MAX_SAMPLES][4];
static int   bandCalibrationSampleCount = 0;

bool addBandEnergyCalibrationSample(float bandEnergies[4]) {
    if (!calibrationActive) return false;
    if (bandCalibrationSampleCount >= CALIBRATION_MAX_SAMPLES) return false;

    for (int i = 0; i < 4; i++) {
        bandCalibrationBuffer[bandCalibrationSampleCount][i] = bandEnergies[i];
    }
    bandCalibrationSampleCount++;
    return true;
}

void computeBandEnergyBaseline(float meanOutput[4], float stdOutput[4]) {
    if (bandCalibrationSampleCount < 2) {
        Serial.println(F("[Calibrator] ERROR: sample band energy terlalu sedikit."));
        for (int i = 0; i < 4; i++) { meanOutput[i] = 0.2f; stdOutput[i] = 0.1f; } // fallback aman, bukan 0
        return;
    }
    for (int f = 0; f < 4; f++) {
        double sum = 0.0;
        for (int i = 0; i < bandCalibrationSampleCount; i++) sum += bandCalibrationBuffer[i][f];
        meanOutput[f] = (float)(sum / bandCalibrationSampleCount);
    }
    for (int f = 0; f < 4; f++) {
        double sumSqDiff = 0.0;
        for (int i = 0; i < bandCalibrationSampleCount; i++) {
            double d = bandCalibrationBuffer[i][f] - meanOutput[f];
            sumSqDiff += d * d;
        }
        stdOutput[f] = (float)sqrt(sumSqDiff / (bandCalibrationSampleCount - 1));
    }
    Serial.printf("[Calibrator] Band energy baseline selesai dari %d sample.\n", bandCalibrationSampleCount);
}
static float audioCalibrationBuffer[CALIBRATION_MAX_SAMPLES][AUDIO_BAND_COUNT];
static int   audioCalibrationSampleCount = 0;

bool addAudioBandEnergyCalibrationSample(float audioBandEnergies[AUDIO_BAND_COUNT]) {
    if (!calibrationActive) return false;
    if (audioCalibrationSampleCount >= CALIBRATION_MAX_SAMPLES) return false;
    for (int i = 0; i < AUDIO_BAND_COUNT; i++) {
        audioCalibrationBuffer[audioCalibrationSampleCount][i] = audioBandEnergies[i];
    }
    audioCalibrationSampleCount++;
    return true;
}

void computeAudioBandBaseline(float meanOutput[AUDIO_BAND_COUNT], float stdOutput[AUDIO_BAND_COUNT]) {
    if (audioCalibrationSampleCount < 2) {
        Serial.println(F("[Calibrator] ERROR: sample audio band energy terlalu sedikit."));
        for (int i = 0; i < AUDIO_BAND_COUNT; i++) { meanOutput[i] = 0.2f; stdOutput[i] = 0.1f; }
        return;
    }
    for (int f = 0; f < AUDIO_BAND_COUNT; f++) {
        double sum = 0.0;
        for (int i = 0; i < audioCalibrationSampleCount; i++) sum += audioCalibrationBuffer[i][f];
        meanOutput[f] = (float)(sum / audioCalibrationSampleCount);
    }
    for (int f = 0; f < AUDIO_BAND_COUNT; f++) {
        double sumSqDiff = 0.0;
        for (int i = 0; i < audioCalibrationSampleCount; i++) {
            double d = audioCalibrationBuffer[i][f] - meanOutput[f];
            sumSqDiff += d * d;
        }
        stdOutput[f] = (float)sqrt(sumSqDiff / (audioCalibrationSampleCount - 1));
        if (stdOutput[f] < 1e-4f) stdOutput[f] = 1e-4f;
    }
    Serial.printf("[Calibrator] Audio band baseline selesai dari %d sample.\n", audioCalibrationSampleCount);
}

void saveAudioBandBaselineToFlash(int slot, float mean[AUDIO_BAND_COUNT], float std[AUDIO_BAND_COUNT]) {
    char ns[16];
    snprintf(ns, sizeof(ns), "audiobase%d", slot);
    flashStorage.begin(ns, false);
    flashStorage.putBytes("mean", mean, sizeof(float) * AUDIO_BAND_COUNT);
    flashStorage.putBytes("std", std, sizeof(float) * AUDIO_BAND_COUNT);
    flashStorage.end();
}

bool loadAudioBandBaselineFromFlash(int slot, float meanOutput[AUDIO_BAND_COUNT], float stdOutput[AUDIO_BAND_COUNT]) {
    char ns[16];
    snprintf(ns, sizeof(ns), "audiobase%d", slot);
    flashStorage.begin(ns, true);
    size_t meanLen = flashStorage.getBytesLength("mean");
    size_t stdLen  = flashStorage.getBytesLength("std");
    if (meanLen != sizeof(float) * AUDIO_BAND_COUNT || stdLen != sizeof(float) * AUDIO_BAND_COUNT) {
        flashStorage.end();
        return false;
    }
    flashStorage.getBytes("mean", meanOutput, meanLen);
    flashStorage.getBytes("std", stdOutput, stdLen);
    flashStorage.end();
    return true;
}
void saveBandBaselineToFlash(int slot, float mean[4], float std[4]) {
    char ns[16];
    snprintf(ns, sizeof(ns), "bandbase%d", slot);
    flashStorage.begin(ns, false);
    flashStorage.putBytes("mean", mean, sizeof(float) * 4);
    flashStorage.putBytes("std", std, sizeof(float) * 4);
    flashStorage.end();
}
bool loadBandBaselineFromFlash(int slot, float meanOutput[4], float stdOutput[4]) {
    char ns[16];
    snprintf(ns, sizeof(ns), "bandbase%d", slot);
    flashStorage.begin(ns, true);
    size_t meanLen = flashStorage.getBytesLength("mean");
    size_t stdLen  = flashStorage.getBytesLength("std");
    if (meanLen != sizeof(float)*4 || stdLen != sizeof(float)*4) {
        flashStorage.end();
        return false;
    }
    flashStorage.getBytes("mean", meanOutput, meanLen);
    flashStorage.getBytes("std", stdOutput, stdLen);
    flashStorage.end();
    return true;
}

bool addCalibrationSample(SensorFeatures sample) {
    if (!calibrationActive) return false;

    // GUARD PENTING: tolak sample yang stale/tidak valid. Kalibrasi yang
    // memasukkan data basi (misal salah satu sensor putus sesaat) akan
    // merusak mean & covariance baseline secara permanen — lebih baik
    // skip satu sample daripada baseline tercemar data buruk.
    if (!sample.valid) {
        Serial.println(F("[Calibrator] Sample ditolak: data tidak valid/stale."));
        return false;
    }

    if (calibrationSampleCount >= CALIBRATION_MAX_SAMPLES) {
        calibrationActive = false; // buffer penuh, fase otomatis selesai
        Serial.println(F("[Calibrator] Buffer kalibrasi penuh, fase selesai."));
        return false;
    }

    calibrationBuffer[calibrationSampleCount][FEAT_VIBRATION] = sample.rms_getaran;
    calibrationBuffer[calibrationSampleCount][FEAT_AUDIO]     = sample.rms_suara;
    calibrationBuffer[calibrationSampleCount][FEAT_TEMP]      = getSmoothedTempRate(sample.suhu);
    calibrationSampleCount++;
    return true;
}

void computeInitialBaseline(float meanOutput[3], float sigmaInverseOutput[3][3]) {
    if (calibrationSampleCount < 2) {
        Serial.println(F("[Calibrator] ERROR: sample terlalu sedikit untuk hitung baseline."));
        for (int i = 0; i < 3; i++) meanOutput[i] = 0.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                sigmaInverseOutput[i][j] = (i == j) ? 1.0f : 0.0f;
        return;
    }

    for (int f = 0; f < 3; f++) {
        double sum = 0.0;
        for (int i = 0; i < calibrationSampleCount; i++) sum += calibrationBuffer[i][f];
        meanOutput[f] = (float)(sum / calibrationSampleCount);
    }

    for (int f = 0; f < 3; f++) {
        double sumSqDiff = 0.0;
        for (int i = 0; i < calibrationSampleCount; i++) {
            double d = calibrationBuffer[i][f] - meanOutput[f];
            sumSqDiff += d * d;
        }
        featureStdDev[f] = (float)sqrt(sumSqDiff / (calibrationSampleCount - 1));
        if (featureStdDev[f] < 1e-4f) featureStdDev[f] = 1e-4f;
    }

    static double standardizedBuffer[CALIBRATION_MAX_SAMPLES][3];
    for (int i = 0; i < calibrationSampleCount; i++)
        for (int f = 0; f < 3; f++)
            standardizedBuffer[i][f] = (calibrationBuffer[i][f] - meanOutput[f]) / featureStdDev[f];

    float rawCovariance[3][3];
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            double sum = 0.0;
            for (int i = 0; i < calibrationSampleCount; i++) {
                sum += standardizedBuffer[i][a] * standardizedBuffer[i][b];
            }
            rawCovariance[a][b] = (float)(sum / (calibrationSampleCount - 1));
        }
    }

    const char* featureNames[3] = {"Getaran", "Suara", "Suhu"};
    const float MIN_ACCEPTABLE_VARIANCE = 5e-4f;
    bool calibrationValid = true;

    for (int f = 0; f < 3; f++) {
        if (rawCovariance[f][f] < MIN_ACCEPTABLE_VARIANCE) {
            Serial.printf("[Calibrator] ERROR: variance %s = %.8f, terlalu rendah. "
                          "Mesin kemungkinan TIDAK AKTIF saat kalibrasi. Kalibrasi DITOLAK.\n",
                          featureNames[f], rawCovariance[f][f]);
            calibrationValid = false;
        }
    }

    if (!calibrationValid) {
        for (int i = 0; i < 3; i++) meanOutput[i] = 0.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                sigmaInverseOutput[i][j] = (i == j) ? 1.0f : 0.0f;
        return;
    }

    applyShrinkageRegularization(rawCovariance);

    if (!checkMatrixWellConditioned(rawCovariance)) {
        Serial.println(F("[Calibrator] ERROR: matriks kovarians ill-conditioned setelah shrinkage. Kalibrasi DITOLAK."));
        for (int i = 0; i < 3; i++) meanOutput[i] = 0.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                sigmaInverseOutput[i][j] = (i == j) ? 1.0f : 0.0f;
        return;
    }

    solveMatrixInverse4x4(rawCovariance, sigmaInverseOutput);
    lastCalibrationValid = true;
    Serial.printf("[Calibrator] Baseline selesai dari %d sample.\n", calibrationSampleCount);
}

void saveBaselineToFlash(int slot, float mean[3], float sigmaInverse[3][3], float stdDev[3]) {
    char ns[16];
    snprintf(ns, sizeof(ns), "baseline%d", slot);
    flashStorage.begin(ns, false);
    flashStorage.putBytes("mean", mean, sizeof(float) * 3);
    flashStorage.putBytes("sigmaInv", sigmaInverse, sizeof(float) * 9);
    flashStorage.putBytes("stdDev", stdDev, sizeof(float) * 3);
    flashStorage.end();
    Serial.printf("[Calibrator] Baseline mesin #%d tersimpan ke flash.\n", slot);
}
bool loadBaselineFromFlash(int slot, float meanOutput[3], float sigmaInverseOutput[3][3], float stdDevOutput[3]) {
    char ns[16];
    snprintf(ns, sizeof(ns), "baseline%d", slot);
    flashStorage.begin(ns, true);
    size_t meanLen = flashStorage.getBytesLength("mean");
    size_t sigmaLen = flashStorage.getBytesLength("sigmaInv");
    size_t stdLen = flashStorage.getBytesLength("stdDev");
    if (meanLen != sizeof(float) * 3 || sigmaLen != sizeof(float) * 9 || stdLen != sizeof(float) * 3) {
        flashStorage.end();
        Serial.printf("[Calibrator] Tidak ada baseline tersimpan untuk mesin #%d.\n", slot);
        return false;
    }
    flashStorage.getBytes("mean", meanOutput, meanLen);
    flashStorage.getBytes("sigmaInv", sigmaInverseOutput, sigmaLen);
    flashStorage.getBytes("stdDev", stdDevOutput, stdLen);
    flashStorage.end();
    Serial.printf("[Calibrator] Baseline mesin #%d berhasil dimuat dari flash.\n", slot);
    return true;
}

void setFeatureStdDev(float stdDev[4]) {
    for (int i = 0; i < 3; i++) featureStdDev[i] = stdDev[i];
}



// BARU: getter supaya file lain (MahalanobisDetector.cpp) bisa mengambil
// featureStdDev yang sama persis dipakai saat kalibrasi, untuk menstandardisasi
// fitur real-time dengan skala yang konsisten dengan baseline.
void getFeatureStdDev(float stdDevOutput[4]) {
    for (int i = 0; i < 3; i++) stdDevOutput[i] = featureStdDev[i];
}
