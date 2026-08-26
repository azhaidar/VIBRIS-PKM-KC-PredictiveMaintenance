//AdaptiveBaselineLearner.cpp
#include "AdaptiveBaselineLearner.h"
#include "CovarianceMatrixSolver.h"
#include <Arduino.h>
#include <math.h>

// Learning rate EMA  kecil supaya baseline bergerak pelan, tidak reaktif
// terhadap noise sesaat (sesuai prinsip di header: perubahan wajar bearing
// aus dipelajari bertahap, bukan tiba-tiba).
#define BASELINE_LEARNING_RATE 0.01f

// Invers matriks itu operasi berat — tidak dihitung ulang tiap update,
// cuma tiap N sample. Sesuai catatan performa di header aslinya.
#define INVERSE_RECOMPUTE_INTERVAL 20

static float currentMean[3];
static float currentVar[3];           // BARU: variance per fitur, EMA -- gantikan featureStdDev statis
static float currentRawSigma[3][3];   // kovarians di RUANG STANDARDISASI (pakai currentVar saat itu)
static float currentSigmaInverse[3][3];
static int   updatesSinceLastInverse = 0;
static bool  learnerInitialized = false;
// BARU (25 Agustus 2026): lihat penjelasan lengkap di AdaptiveBaselineLearner.h
// dekat getBaselineChangeTimestamp() -- distempel tiap kali baseline diganti.
static unsigned long baselineChangeTimestamp = 0;

bool isBaselineLearnerReady() {
    return learnerInitialized;
}
void initializeBaselineLearner(float initialMean[3], float initialStd[3], float initialSigmaInverse[3][3]) {
    for (int i = 0; i < 3; i++) {
        currentMean[i] = initialMean[i];
        float sd = initialStd[i];
        currentVar[i] = sd * sd;
        if (currentVar[i] < 1e-8f) currentVar[i] = 1e-8f;
    }
    // FIX (25 Agustus 2026): dulu di sini kebalik -- currentRawSigma (yang
    // NAMANYA aja udah bilang "raw", artinya covariance BELUM di-invers)
    // malah diisi initialSigmaInverse (yang JUSTRU udah dalam bentuk INVERS,
    // lihat cara dia dipakai LANGSUNG di computeMahalanobisQuadraticForm()
    // di MahalanobisDetector.cpp -- gak pernah di-invers dulu di sana).
    // Terus currentSigmaInverse (yang wajib pegang bentuk INVERS, karena itu
    // yang dipakai langsung buat hitung D^2) malah di-invers LAGI dari
    // initialSigmaInverse -- jadi 2x di-invers, hasilnya balik jadi
    // covariance biasa (BUKAN inversnya) tanpa disadari. Efeknya D^2 salah
    // hitung terus, cuma "kebetulan" masih kelihatan masuk akal di beberapa
    // sesi karena angkanya kebetulan berdekatan. Perbaikannya: simpan
    // currentSigmaInverse LANGSUNG dari initialSigmaInverse (sudah bentuk
    // yang benar, apa adanya), dan currentRawSigma didapat dengan
    // meng-invers-nya SEKALI (bukan nol kali/dua kali) supaya nanti
    // updateBaselineIfNormal() punya covariance mentah yang benar buat
    // belajar bertahap.
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            currentSigmaInverse[i][j] = initialSigmaInverse[i][j];
    solveMatrixInverse4x4(currentSigmaInverse, currentRawSigma);
    updatesSinceLastInverse = 0;
    learnerInitialized = true;
    // BARU (25 Agustus 2026): stempel WAKTU tiap kali baseline diganti --
    // dipakai MahalanobisDetector.cpp buat kasih jeda settle. Lihat
    // penjelasan lengkap di AdaptiveBaselineLearner.h.
    baselineChangeTimestamp = millis();
    Serial.println(F("[AdaptiveLearner] Baseline learner diinisialisasi (mean + std-dev + sigma, semuanya adaptif)."));
}

void updateBaselineIfNormal(float currentRawFeatures[3], bool isCurrentStatusNormal) {
    if (!learnerInitialized) {
        Serial.println(F("[AdaptiveLearner] ERROR: belum diinisialisasi, update diabaikan."));
        return;
    }
    if (!isCurrentStatusNormal) return;

    for (int i = 0; i < 3; i++) {
        currentMean[i] += BASELINE_LEARNING_RATE * (currentRawFeatures[i] - currentMean[i]);
    }

    float rawDiff[3];
    for (int i = 0; i < 3; i++) {
        rawDiff[i] = currentRawFeatures[i] - currentMean[i];
        float instantVar = rawDiff[i] * rawDiff[i];
        currentVar[i] += BASELINE_LEARNING_RATE * (instantVar - currentVar[i]);
        if (currentVar[i] < 1e-8f) currentVar[i] = 1e-8f;
    }

    float stdDiff[3];
    for (int i = 0; i < 3; i++) {
        float sd = sqrtf(currentVar[i]);
        stdDiff[i] = rawDiff[i] / sd;
    }

    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            float instantCov = stdDiff[a] * stdDiff[b];
            currentRawSigma[a][b] += BASELINE_LEARNING_RATE * (instantCov - currentRawSigma[a][b]);
        }
    }

    updatesSinceLastInverse++;
    if (updatesSinceLastInverse >= INVERSE_RECOMPUTE_INTERVAL) {
        applyShrinkageRegularization(currentRawSigma);
        solveMatrixInverse4x4(currentRawSigma, currentSigmaInverse);
        updatesSinceLastInverse = 0;
    }
}

void getCurrentBaseline(float meanOutput[3], float sigmaInverseOutput[3][3]) {
    for (int i = 0; i < 3; i++) meanOutput[i] = currentMean[i];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            sigmaInverseOutput[i][j] = currentSigmaInverse[i][j];
}

void getCurrentStdDev(float stdOutput[3]) {
    for (int i = 0; i < 3; i++) stdOutput[i] = sqrtf(currentVar[i]);
}
void resetBaselineLearner() {
    learnerInitialized = false;
    Serial.println(F("[AdaptiveLearner] Learner di-reset -- menunggu kalibrasi baru."));
}

// BARU (25 Agustus 2026): getter untuk baselineChangeTimestamp -- lihat
// penjelasan lengkap di AdaptiveBaselineLearner.h.
unsigned long getBaselineChangeTimestamp() {
    return baselineChangeTimestamp;
}