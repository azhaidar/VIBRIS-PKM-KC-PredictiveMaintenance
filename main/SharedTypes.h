// SharedTypes.h
#pragma once

#include <stdint.h>
#include "config.h" // Mengunci FFT_SAMPLES agar sinkron secara arsitektur
enum FeatureIndex { FEAT_VIBRATION = 0, FEAT_AUDIO = 1, FEAT_TEMP = 2, FEAT_COUNT = 3 };//FEAT_CURRENT = 2
// ===================================================================
// 1. BUFFER DATA MENTAH (RAW DATA BUFFER - CORE 0 TO DSP)
// ===================================================================
struct VibrationBuffer {
    float samples[FFT_SAMPLES]; // Array float untuk menampung sampel getaran LIS3DH [cite: 2026-05-06]
    uint32_t timestamp;         // Waktu pengambilan sampel (millis/micros)
    float rms_x_raw;
    float rms_y_raw;
    float rms_z_raw;
    float actual_rate_hz;
};
struct AudioBuffer {
    float samples[AUDIO_FFT_SAMPLES];
    uint32_t timestamp;
};

// ===================================================================
// 2. WADAH FITUR SENSOR (FEATURE EXTRACTION - INTER-CORE DATA PASSING)
// ===================================================================
struct SensorFeatures {
    volatile float rms_getaran; // Hasil kalkulasi Root Mean Square dari LIS3DH [cite: 2026-05-06]
    volatile float rms_suara;   // Hasil konversi amplitudo/daya dari INMP441 [cite: 2026-05-06]
    volatile float arus;        // Hasil kalkulasi RMS dari sensor SCT [cite: 2026-04-10]
    volatile float suhu;        // Hasil pembacaan dari sensor suhu DS18H [cite: 2026-04-10]
    volatile bool valid;        // Flag integritas data untuk error handling / fail-safe mechanism
};

// ===================================================================
// 3. HASIL KEPUTUSAN DIAGNOSTIK (INFERENCE RESULT)
// ===================================================================
struct DetectionResult {
    float rpm_estimated;        // Estimasi kecepatan putar mesin hasil analisis spektrum
    float mahalanobis_D2;       // Nilai Jarak Mahalanobis untuk deteksi anomali [cite: 2026-05-06]
    char status_label[16];      // String status: "Normal", "Waspada", atau "Bahaya"
    char diagnosis_label[20];   // "UNBALANCE" / "MISALIGNMENT" / "BEARING_BPFO" / "BEARING_BPFI" / "NORMAL" / "N/A"
    float diagnosis_confidence; // Z-score band paling menyimpang
    char audio_diagnosis_label[20];      // BARU
    float audio_diagnosis_confidence;
    // Tambah setelah audio_diagnosis_confidence:
    float health_score;           // 0-100
    char  trend[16];              // "Memburuk" / "Stabil" / "Membaik"
    char  servis_estimasi[32];    // "30+ hari" / "SEGERA" dll
    char  ml_label[16];           // TinyML label
    float ml_confidence;          // TinyML confidence
};

// Tbearign abangku
struct BearingSpec {
    int   n_balls;
    float d_ball_mm;
    float D_pitch_mm;
    float phi_deg;
    const char* label;
};

// Tabel bearing umum (approksimasi Pd = rata-rata bore+OD, Bd dari referensi
// umum seri 62xx -- BUKAN data pabrikan presisi. Verifikasi manual pakai
// jangka sorong kalau butuh akurasi tinggi untuk laporan resmi).
// Spek bearing motor uji aktif: 6202 (drive end) & 6202/6201 (non-drive end),
// umum untuk dinamo 1-fasa 4-pole 1/4HP. Ganti angka ini kalau motor uji diganti.
static const BearingSpec ACTIVE_BEARING_SPEC = {8, 6.35f, 25.0f, 0.0f, "6202 (15x35x11)"};

// PENTING: bukan 'static' -- ini DIDEKLARASIKAN di sini, tapi
// DIDEFINISIKAN cuma sekali di FFTProcessor.cpp (lihat FIX 2).
// Supaya semua file (main.ino, FFTProcessor.cpp) pegang variabel YANG SAMA.
extern BearingSpec currentBearingSpec;

extern int ml_label;
extern float ml_confidence;