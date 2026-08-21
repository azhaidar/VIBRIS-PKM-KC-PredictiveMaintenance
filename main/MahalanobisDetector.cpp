// Nilai kritis chi-square, df=3 (3 fitur sensor: getaran, suara, laju suhu):
// 95% confidence = 7.815, 99% confidence = 11.345. Standar statistik, bukan tebakan.
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
void resetDiagnosisBandBaseline() {
    diagBaselineReady = false;
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
static char stableStatusLabel[16] = "Normal";
static char pendingStatusLabel[16] = "Normal";
static int  statusStreak = 0;
#define STATUS_CONFIRM_STREAK 5

const char* getDebounceStatus(const char* newLabel) {
    if (strcmp(newLabel, pendingStatusLabel) == 0) {
        statusStreak++;
    } else {
        strncpy(pendingStatusLabel, newLabel, sizeof(pendingStatusLabel) - 1);
        pendingStatusLabel[sizeof(pendingStatusLabel) - 1] = '\0';
        statusStreak = 1;
    }
    if (statusStreak >= STATUS_CONFIRM_STREAK) {
        strncpy(stableStatusLabel, pendingStatusLabel, sizeof(stableStatusLabel) - 1);
        stableStatusLabel[sizeof(stableStatusLabel) - 1] = '\0';
    }
    return stableStatusLabel;
}
// State debounce -- taruh di scope file, sejajar dengan diagBandMean dkk
    DetectionResult runDetectionCycle() {
    DetectionResult result{};
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
    static float smoothedRms = 0.0f, smoothedRoughness = 0.0f;
    #define FEATURE_SMOOTH_ALPHA 0.3f
    smoothedRms       = FEATURE_SMOOTH_ALPHA * merged.rms_getaran + (1-FEATURE_SMOOTH_ALPHA) * smoothedRms;
    // FIX (20 Agustus 2026): sebelumnya baris ini pakai Scheduler_GetLatestRoughness()
    // -- itu SINYAL BEDA TOTAL dari yang direkam pas kalibrasi. Buktinya:
    // InitialBaselineCalibrator.cpp baris 200 nyimpen sample.rms_suara (RMS
    // amplitudo mentah dari mikrofon INMP441) sebagai fitur audio ke-2,
    // BUKAN "roughness" (fitur spektral lain, beda skala total). Data nyata
    // 20 Agustus 2026 buktiin dampaknya: mean roughness pas kalibrasi =
    // 0.023940, tapi mean roughness pas monitoring 1 menit kemudian (mesin
    // & lingkungan SAMA persis) = 0.006213 -- 4x lebih kecil. Bukan
    // mesinnya berubah, tapi dari awal baseline & pembacaan real-time
    // membandingkan 2 besaran yang beda, jadi D^2 selalu meleset tinggi
    // dan status kejebak "Bahaya" terus walau mesin normal. Diganti ke
    // merged.rms_suara biar SAMA PERSIS sinyal yang direkam calibrator.
    smoothedRoughness = FEATURE_SMOOTH_ALPHA * merged.rms_suara + (1-FEATURE_SMOOTH_ALPHA) * smoothedRoughness;

    float currentFeatures[3] = {
        smoothedRms, smoothedRoughness, getSmoothedTempRate(merged.suhu)
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
    float currentRpm = Scheduler_GetLatestRPM();
    const char* rawLabel = (currentRpm <= 0.0f) ? "Diam" : classifyStatusFromD2(d2);
    const char* label = getDebounceStatus(rawLabel);
    bool isNormal = (strcmp(label, "Normal") == 0);

    // GANTI: currentFeaturesStd -> currentFeatures (RAW) -- sesuai kontrak
    // updateBaselineIfNormal() yang sudah direvisi di AdaptiveBaselineLearner.cpp
    #if ENABLE_ONLINE_BASELINE_LEARNING
    updateBaselineIfNormal(currentFeatures, isNormal);
    #else
    // FIX (21 Agustus 2026): dimatikan -- lihat penjelasan lengkap di
    // config.h dekat ENABLE_ONLINE_BASELINE_LEARNING. Baseline sekarang
    // HANYA berubah lewat kalibrasi eksplisit (tombol 'R' / preset
    // pabrikan), tidak lagi "belajar" diam-diam dari bacaan sendiri tiap
    // siklus. (void) di sini cuma buat matiin warning "variable unused".
    (void)isNormal;
    #endif

    result.rpm_estimated = currentRpm;
    result.mahalanobis_D2 = d2;
    strncpy(result.status_label, label, sizeof(result.status_label) - 1);
    result.status_label[sizeof(result.status_label) - 1] = '\0';
    #if ENABLE_RPM_DIAGNOSIS
    if (diagBaselineReady && result.rpm_estimated > 0.0f) {
        float bandEnergies[4];
        Scheduler_GetLatestBandEnergies(bandEnergies);

        // FIX (21 Agustus 2026): FFTProcessor_Process() (FFTProcessor.cpp
        // baris 106-112 & 194-199) SENGAJA nge-nol-in ke-4 bandEnergies_out
        // setiap siklus spektrum belum "matang" -- FFT baru dianggap valid
        // sekali per SPECTRAL_AVG_COUNT (=12) akumulasi, jadi ~11 dari 12
        // pemanggilan di sini nerima {0,0,0,0}, BUKAN bacaan getaran asli.
        // Sebelumnya nol ini tetap dimasukkan ke Diagnosis_Classify() --
        // dibandingkan ke baseline yang meannya jauh di atas nol (lihat
        // presetMesin2_bandMean di FactoryPresets.h, ~1700-an), Z-score-nya
        // jadi SANGAT negatif, dan `Diagnosis_Classify` membacanya sebagai
        // "NORMAL" (di bawah ambang) walau motornya lagi jelas rusak. Data
        // uji `kondisiUnbalance` 20:41 buktikan ini: diagnosis "NORMAL" di
        // 1320/1481 baris (89%) walau status sudah benar "Bahaya" di 91%
        // baris yang SAMA -- bandEnergies waktu itu 0 di >75% baris (lihat
        // e_unbalance/e_misalign/e_bpfo/e_bpfi, median-nya 0.0). Ini bug
        // yang SAMA KELUARGANYA dengan fix RPM=0 di komentar atas file ini
        // (nol yang bukan bacaan asli, dibaca "NORMAL" yang menyesatkan).
        // Fix: kalau bandEnergies masih placeholder nol (belum ada spektrum
        // baru), LEWATI klasifikasi diagnosis siklus ini -- biarkan
        // diagnosis_label tetap default "N/A" (sudah di-set di awal fungsi
        // ini), JANGAN dipaksa "NORMAL" dari data yang bukan bacaan asli.
        bool bandEnergiesFresh = (bandEnergies[0] != 0.0f || bandEnergies[1] != 0.0f ||
                                   bandEnergies[2] != 0.0f || bandEnergies[3] != 0.0f);
        if (bandEnergiesFresh) {
            float diagBandStd[4];   // BARU: konversi variance->std tiap siklus
            for (int i = 0; i < 4; i++) diagBandStd[i] = sqrtf(diagBandVar[i] > 1e-8f ? diagBandVar[i] : 1e-8f);

            char diagLabel[20];
            float diagConfidence = 0.0f;

            uint8_t diagFlags = 0;
            Diagnosis_Classify(bandEnergies, diagBandMean, diagBandStd, diagLabel, &diagConfidence, &diagFlags);
            strncpy(result.diagnosis_label, diagLabel, sizeof(result.diagnosis_label) - 1);
            result.diagnosis_label[sizeof(result.diagnosis_label) - 1] = '\0';
            result.diagnosis_confidence = diagConfidence;

            #if ENABLE_ONLINE_BASELINE_LEARNING
            updateBandBaselineIfNormal(diagBandMean, diagBandVar, 4, bandEnergies, isNormal);   // BARU
            #endif
            // FIX (21 Agustus 2026): baseline band diagnosis (Unbalance/Misalign/
            // BPFO/BPFI) ikut dimatikan adaptasi online-nya, sama alasannya
            // seperti baseline Mahalanobis utama -- lihat config.h.
        }
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
        uint8_t audioFlags = 0; 
        DriverAudioDiagnosis_Classify(audioBandEnergies, audioBandMean, audioBandStd, audioLabel, &audioConf, &audioFlags);

        strncpy(result.audio_diagnosis_label, audioLabel, sizeof(result.audio_diagnosis_label) - 1);
        result.audio_diagnosis_label[sizeof(result.audio_diagnosis_label) - 1] = '\0';
        result.audio_diagnosis_confidence = audioConf;

        #if ENABLE_ONLINE_BASELINE_LEARNING
        updateBandBaselineIfNormal(audioBandMean, audioBandVar, AUDIO_BAND_COUNT, audioBandEnergies, isNormal);
        #endif
        // FIX (21 Agustus 2026): baseline band audio ikut dimatikan adaptasi
        // online-nya, alasan sama -- lihat config.h.
    }

    return result;
}