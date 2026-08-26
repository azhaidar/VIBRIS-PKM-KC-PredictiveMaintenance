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
#include "config.h"   // FIX 25 Agustus 2026: perlu VIBRATION_ABSOLUTE_FLOOR utk bedain
                       // "motor beneran diam" vs "RPM gagal kebaca padahal motor jalan"
#include <Arduino.h>
#include <string.h>
#include <math.h>   // BARU: untuk sqrtf

#define CHI_SQUARE_95 7.815f
#define CHI_SQUARE_99 11.345f
float getChiSquare99() {
    return CHI_SQUARE_99;
}
// BARU (25 Agustus 2026, FIX LONJAKAN D^2 SESAAT SETELAH GANTI BASELINE):
// Berapa lama status dipaksa "Settling" (bukan Normal/Waspada/Bahaya) tiap
// kali baseline baru saja diganti. Lihat catatan lengkap di runDetectionCycle().
#define BASELINE_SETTLE_MS 2000UL
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
// ============================================================================
// BARU (26 Agustus 2026): Helper function DEBUG — lihat D² sebelum & sesudah
// Huber clipping per sensor
// ============================================================================
void debugPrintD2Contribution(float z_scores_raw[3], float z_scores_clipped[3], float sigmaInverse[3][3]) {
    Serial.println(F("\n[DEBUG D² BREAKDOWN]"));

    // Hitung D² raw (sebelum clip)
    float temp_raw[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            temp_raw[i] += sigmaInverse[i][j] * z_scores_raw[j];
        }
    }
    float d2_raw = 0.0f;
    for (int i = 0; i < 3; i++) {
        d2_raw += z_scores_raw[i] * temp_raw[i];
    }

    // Hitung D² clipped (sesudah clip)
    float temp_clipped[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            temp_clipped[i] += sigmaInverse[i][j] * z_scores_clipped[j];
        }
    }
    float d2_clipped = 0.0f;
    for (int i = 0; i < 3; i++) {
        d2_clipped += z_scores_clipped[i] * temp_clipped[i];
    }

    // Kontribusi individual tiap sensor (SEBELUM Huber)
    float contrib_raw[3];
    for (int i = 0; i < 3; i++) {
        contrib_raw[i] = z_scores_raw[i] * temp_raw[i];
    }

    // Kontribusi individual tiap sensor (SESUDAH Huber)
    float contrib_clipped[3];
    for (int i = 0; i < 3; i++) {
        contrib_clipped[i] = z_scores_clipped[i] * temp_clipped[i];
    }

    // Print hasil
    Serial.printf("  D² RAW (sebelum Huber)  = %.3f\n", d2_raw);
    Serial.printf("  D² CLIPPED (sesudah Huber) = %.3f\n", d2_clipped);
    Serial.printf("  Delta = %.3f\n\n", d2_clipped - d2_raw);

    const char* sensorName[3] = {"Getaran", "Audio", "Suhu"};
    const float HUBER_K[3] = {5.0f, 3.0f, 4.0f};

    for (int i = 0; i < 3; i++) {
        Serial.printf("  %s (index %d):\n", sensorName[i], i);
        Serial.printf("    z-score RAW     = %.4f\n", z_scores_raw[i]);
        Serial.printf("    z-score CLIPPED = %.4f (k=%.1f)\n", z_scores_clipped[i], HUBER_K[i]);
        Serial.printf("    Kontribusi RAW     = %.4f\n", contrib_raw[i]);
        Serial.printf("    Kontribusi CLIPPED = %.4f\n", contrib_clipped[i]);
        Serial.printf("    Pengurangan = %.4f\n\n", contrib_raw[i] - contrib_clipped[i]);
    }
}

// State debounce -- taruh di scope file, sejajar dengan diagBandMean dkk
    DetectionResult runDetectionCycle() {
    DetectionResult result{};
    // FIX (25 Agustus 2026, ROOT CAUSE "D^2 MELEDAK PADAHAL MOTOR GAK
    // BERUBAH"): dulu ada 'wasReady'/'baselineReadyTimestamp' persis di
    // sini, tapi CUMA di-set -- TIDAK PERNAH dipakai buat apa-apa (kode
    // setengah jadi, niatnya kasih grace period tapi lupa dipasang).
    // BUKTI dari data lapangan (log 21:08:18, CSV slot2 25 Agustus): tepat
    // setelah kalibrasi selesai, D^2 = 24.249 -> 12.904 -> 6.943 -> 3.530
    // -> 3.011 -> 2.147(Normal) -- turun SENDIRI dalam <1 detik TANPA motor
    // berubah apa-apa. Sebabnya: smoothedRms/smoothedRoughness di bawah ini
    // (EMA alpha=0.3) masih bawa nilai dari baseline LAMA selama beberapa
    // sample sebelum "narik" ke baseline BARU -- selama proses narik itu
    // D^2 melonjak PALSU. Sekarang dipasang beneran: getBaselineChangeTimestamp()
    // (AdaptiveBaselineLearner.cpp) distempel tiap kali baseline berubah
    // (kalibrasi baru selesai / pindah slot / pindah regime), dan status
    // dipaksa "Settling" selama BASELINE_SETTLE_MS pertama sesudahnya --
    // sama prinsipnya dengan WARMUP_GRACE_MS di main.ino, tapi buat
    // kejadian ganti-baseline, bukan cuma boot pertama kali.
    static unsigned long lastSeenBaselineChange = 0;
    static unsigned long settleUntilMillis = 0;
    unsigned long currentBaselineChange = getBaselineChangeTimestamp();
    if (currentBaselineChange != lastSeenBaselineChange) {
        lastSeenBaselineChange = currentBaselineChange;
        settleUntilMillis = millis() + BASELINE_SETTLE_MS;
    }
    bool stillSettling = (millis() < settleUntilMillis);
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
    // FIX (25 Agustus 2026): blok "if (!wasReady) {...}" yang dulu di sini
    // DIHAPUS -- itu sisa kode lama yang sudah digantikan logic settle
    // window di atas (stillSettling, pakai getBaselineChangeTimestamp()).
    // wasReady/baselineReadyTimestamp sudah tidak dideklarasikan lagi di
    // atas, jadi blok lama ini kalau dibiarkan bikin compile error
    // "not declared in this scope".

    SensorFeatures merged;
    bool fresh = getMergedFeatures(&merged);
    if (!fresh) {
        strncpy(result.status_label, "SensorFault", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
        return result;
    }
    static float smoothedRms = 0.0f, smoothedRoughness = 0.0f;
    #define FEATURE_SMOOTH_ALPHA 0.3f
    #define AUDIO_SMOOTH_ALPHA 0.1f      // BARU (26 Agustus 2026): Audio lebih smooth dari getaran
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
    //
    // FIX (26 Agustus 2026): sebelumnya audio dan getaran pakai smoothing
    // alpha yang sama (0.3f). Tapi INMP441 sangat responsif terhadap ambient
    // noise (kipas, obrolan, mesin lain) -- fluktuasi 0.049-0.094 itu NORMAL
    // untuk mikrofon real, bukan error. Kalau alpha tetap 0.3, audio masih
    // spike-spike setiap saat noise ambient berubah. Solusi: turunkan alpha
    // audio jadi 0.1 -- jauh lebih smooth, tapi tetap responsif kalau ada
    // masalah getaran bearing (frekuensi berubah drastis). Getaran tetap 0.3
    // supaya responsif terhadap shock/unbalance yang tiba-tiba.
    smoothedRoughness = AUDIO_SMOOTH_ALPHA * merged.rms_suara + (1-AUDIO_SMOOTH_ALPHA) * smoothedRoughness;

    float currentFeatures[3] = {
        smoothedRms, smoothedRoughness, getSmoothedTempRate(merged.suhu)
    };

    float baselineMean[3];
    float sigmaInverse[3][3];
    getCurrentBaseline(baselineMean, sigmaInverse);

    // BARU (26 Agustus 2026): DEBUG — print baseline yang SEBENARNYA sedang digunakan
    // Print SETIAP SIKLUS supaya konsisten lihat perubahannya
    float featureStdDev[3];
    getCurrentStdDev(featureStdDev);

    Serial.printf("[BASELINE] mean=%.6f,%.6f,%.6f | std=%.6f,%.6f,%.6f\n",
        baselineMean[0], baselineMean[1], baselineMean[2],
        featureStdDev[0], featureStdDev[1], featureStdDev[2]);

    // GANTI: getFeatureStdDev() (statis) -> getCurrentStdDev() (adaptif, SUDAH ADA
    // di AdaptiveBaselineLearner.cpp dari awal, cuma belum dipakai di sini)
    // (featureStdDev sudah di-load di atas)

    float currentFeaturesStd[3];
    float zeroMean[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; i++) {
        currentFeaturesStd[i] = (currentFeatures[i] - baselineMean[i]) / featureStdDev[i];
    }

    // BARU (26 Agustus 2026): SIMPAN z-scores RAW sebelum Huber clipping
    float z_scores_raw[3];
    for (int i = 0; i < 3; i++) {
        z_scores_raw[i] = currentFeaturesStd[i];
    }

    // =========================================================================
    // BARU (25 Agustus 2026, ROBUST CLIPPING -- METODE HUBER M-ESTIMATOR):
    // =========================================================================
    // TUJUAN: satu sensor (biasanya audio/mikrofon, paling gampang kena
    // noise lingkungan -- kipas, obrolan, mesin lain) TIDAK BOLEH sendirian
    // "menyeret" D^2 jadi meledak ratusan, padahal 2 sensor lain normal.
    // Bukti nyata kasus ini: sesi 25 Agustus 21:13, rms_a menyimpang 16.8
    // standar deviasi dari baseline (bukan motor rusak, tapi noise ambient
    // beda dari saat kalibrasi) -- KONTRIBUSI SENDIRIAN ke D^2 = 337 dari
    // total 360, padahal getaran & suhu normal.
    //
    // METODE: Huber's M-estimator (Huber, P.J., 1964, "Robust Estimation of
    // a Location Parameter", Annals of Mathematical Statistics). Fungsi
    // pengaruh (influence function) Huber:
    //
    //     psi_k(u) = u                  , kalau |u| <= k   (dipakai apa adanya)
    //     psi_k(u) = k * sign(u)        , kalau |u| >  k   (DIPOTONG di k)
    //
    // Ini PERSIS rumus yang sama dipakai di textbook/paper robust statistics
    // (lihat juga Rousseeuw & Hubert, 2018, "Anomaly Detection by Robust
    // Statistics", WIREs Data Mining and Knowledge Discovery) untuk mencegah
    // SATU observasi ekstrem mendominasi hasil akhir. Kita terapkan psi_k()
    // ke tiap z-score (currentFeaturesStd[i]) SEBELUM dimasukkan ke rumus
    // Mahalanobis D^2 = z^T x SigmaInverse x z.
    //
    // KENAPA k BUKAN 1.345 (konstanta standar Huber di textbook):
    // k=1.345 itu dipilih SPESIFIK untuk kasus ESTIMASI LOKASI/RATA-RATA
    // robust (target: 95% efisiensi statistik dibanding rata-rata biasa
    // saat data benar-benar normal) -- KONTEKS BEDA dari kita di sini.
    // Konteks kita: SKOR DETEKSI ANOMALI real-time, bukan estimasi
    // parameter. Kalau k=1.345 dipakai di sini, HAMPIR SEMUA pembacaan
    // (bahkan motor yang beneran rusak parah) ikut kepotong di 1.345,
    // sehingga status "Bahaya" nyaris TIDAK PERNAH bisa tercapai lagi --
    // sensitivitas deteksi kerusakan sungguhan ikut hilang, bukan cuma
    // false alarm-nya. Nilai k di bawah ini DITURUNKAN EMPIRIS dari data
    // kalibrasi VIBRIS sendiri (sama semangatnya dengan threshold chi-square
    // 7.815/11.345 yang sudah dipakai -- berbasis data nyata, bukan tebakan),
    // per sensor beda toleransinya:
    //   - Getaran (index 0): k=5.0 -- motor besar/beban berubah wajar bikin
    //     getaran menyimpang cukup jauh dari baseline, kasih ruang lebih.
    //   - Audio   (index 1): k=3.0 -- PALING RAWAN noise lingkungan (lihat
    //     bukti di atas), dibatasi PALING KETAT supaya tidak bisa
    //     mendominasi sendirian seperti kasus 21:13 tadi.
    //   - Suhu    (index 2): k=4.0 -- laju perubahan suhu biasanya lambat &
    //     jelas kalau memang ada masalah, di tengah-tengah.
    // Satu sensor MASIH BISA memicu status Bahaya SENDIRIAN kalau memang
    // ekstrem (nilai k di atas cukup besar untuk itu) -- yang dicegah cuma
    // LONJAKAN LIAR TANPA BATAS seperti z=16.8 kemarin, bukan sensitivitas
    // deteksinya secara keseluruhan.
    //
    // PENTING -- SCOPE clipping ini SENGAJA TERBATAS:
    // HANYA memengaruhi currentFeaturesStd[] di bawah ini, yang HANYA
    // dipakai untuk hitung D^2 (severity: Normal/Waspada/Bahaya). TIDAK
    // menyentuh e_bpfo/e_bpfi/e_unbalance/e_misalign (diagnosis jenis
    // kerusakan, dihitung terpisah di DiagnosisClassifier.cpp dari
    // bandEnergies mentah) -- diagnosis tetap apa adanya, tidak di-clip.
    static const float HUBER_K[3] = {5.0f, 3.0f, 4.0f};   // {getaran, audio, suhu}
    for (int i = 0; i < 3; i++) {
        float u = currentFeaturesStd[i];
        float k = HUBER_K[i];
        if (u > k)       currentFeaturesStd[i] = k;    // psi_k(u) = k
        else if (u < -k) currentFeaturesStd[i] = -k;   // psi_k(u) = -k
        // else: |u| <= k, currentFeaturesStd[i] dipakai apa adanya (psi_k(u) = u)
    }

    // BARU (26 Agustus 2026): SIMPAN z-scores CLIPPED sesudah Huber clipping
    float z_scores_clipped[3];
    for (int i = 0; i < 3; i++) {
        z_scores_clipped[i] = currentFeaturesStd[i];
    }

    // BARU (26 Agustus 2026): DEBUG print SETIAP SIKLUS supaya konsisten lihat D² dan kontribusi
    debugPrintD2Contribution(z_scores_raw, z_scores_clipped, sigmaInverse);

    float d2 = computeMahalanobisQuadraticForm(currentFeaturesStd, zeroMean, sigmaInverse);
    float currentRpm = Scheduler_GetLatestRPM();

    // FIX (25 Agustus 2026): RPM<=0 itu ADA 2 KEMUNGKINAN BEDA --
    // (1) motor BENERAN diam/mati (getaran juga rendah), atau
    // (2) motor JALAN tapi RPM Estimator gagal baca (SNR jelek/sensor kendor),
    //     padahal getarannya jelas ada (rms_getaran tinggi).
    // Kode LAMA nge-treat keduanya sama-sama "Diam" -- padahal D2 di atas ini
    // SUDAH DIHITUNG TANPA BUTUH RPM SAMA SEKALI (cuma dari getaran+suara+suhu),
    // jadi kasus (2) kehilangan status Normal/Waspada/Bahaya yang sebenarnya
    // valid, cuma gara-gara RPM Estimator-nya doang yang gagal.
    // Bedakan pakai VIBRATION_ABSOLUTE_FLOOR yang SUDAH ada & sudah tervalidasi
    // dari data asli (lihat config.h) -- bukan angka baru yang dikarang.
    bool trulyIdle = (currentRpm <= 0.0f) && (merged.rms_getaran <= VIBRATION_ABSOLUTE_FLOOR);
    const char* rawLabel = trulyIdle ? "Diam" : classifyStatusFromD2(d2);
    const char* label = getDebounceStatus(rawLabel);
    // FIX (25 Agustus 2026): selama masih dalam jendela settle abis ganti
    // baseline (lihat catatan panjang di atas fungsi ini), JANGAN percaya
    // Normal/Waspada/Bahaya dari D^2 yang belum stabil -- paksa "Settling".
    // isNormal ikut dipaksa false supaya lonjakan sesaat ini juga TIDAK
    // ikut "diajarkan" balik ke baseline lewat updateBaselineIfNormal().
    if (stillSettling) {
        label = "Settling";
    }
    bool isNormal = (strcmp(label, "Normal") == 0) && !stillSettling;

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
    // FIX (25 Agustus 2026): dulu ada syarat "result.rpm_estimated > 0.0f" di sini.
    // Efeknya: diagnosis Unbalance/Misalign/BPFO/BPFI ikut MATI setiap kali RPM
    // Estimator gagal baca (SNR jelek) -- padahal bandEnergies di bawah ini
    // dihitung dari FFT getaran mentah, BUKAN dari hasil RPM Estimator.
    // Jadi syarat RPM di sini nggak relevan & cuma bikin diagnosis ikut hilang
    // bareng status "Diam" yang salah tadi. Sekarang cukup pakai diagBaselineReady
    // (baseline band-nya sudah settle/terkalibrasi).
    if (diagBaselineReady) {
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