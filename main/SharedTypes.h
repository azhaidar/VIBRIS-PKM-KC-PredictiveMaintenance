// SharedTypes.h
#pragma once

#include <stdint.h>
#include "config.h" // Mengunci FFT_SAMPLES agar sinkron secara arsitektur
#define BEARING_TABLE_SIZE (sizeof(BEARING_TABLE)/sizeof(BEARING_TABLE[0]))
#define BEARING_DEFAULT_INDEX 0


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
    volatile float kurtosis;   // BARU
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
    // ... field yang udah ada ...
    uint8_t diagnosis_flags;   // BARU: bit0=unbalance, bit1=misalignment, bit2=BPFO, bit3=BPFI
};
// Spek bearing per klaster mesin.
// Klaster ditentukan dari kemiripan hasil hitung BPFO/BPFI (RPM x geometri
// bearing) -- bukan dari kategori daya/ukuran fisik. Toleransi window
// deteksi band di FFTProcessor.cpp itu +-10%, jadi klaster beda kalau beda
// RPM/geometri > 10%. Lihat diskusi klaster A/B, [tanggal hari ini].
// Tbearign abangku
struct BearingSpec {
    int   n_balls;
    float d_ball_mm;
    float D_pitch_mm;
    float phi_deg;
    const char* label;

    float oneX_hz;    // unbalance band center
    float twoX_hz;    // misalignment band center
    float bpfo_hz;     // outer race band center
    float bpfi_hz;     // inner race band center
};

// Sumber angka: datasheet katalog standar seri 62xx (SKF/NSK/NTN), BUKAN
// hasil ukur jangka sorong langsung -- verifikasi manual kalau butuh akurasi
// tinggi untuk laporan resmi.
static const BearingSpec BEARING_TABLE[] = {
    // n_balls, Bd(mm), Pd(mm), phi(deg), label
    {8, 6.35f, 25.0f, 0.0f, "Klaster A: ~1400RPM (6202)", 23.33f, 46.67f, 69.63f, 117.09f},
    {8, 6.75f, 28.5f, 0.0f, "Klaster A: ~1400RPM (6203)", 23.33f, 46.67f, 71.23f, 115.44f},
};

// PENTING: bukan 'static' -- ini DIDEKLARASIKAN di sini, tapi
// DIDEFINISIKAN cuma sekali di FFTProcessor.cpp (lihat FIX 2).
// Supaya semua file (main.ino, FFTProcessor.cpp) pegang variabel YANG SAMA.
extern BearingSpec currentBearingSpec;
enum FeatureIndex { FEAT_VIBRATION = 0, FEAT_AUDIO = 1, FEAT_TEMP = 2, FEAT_KURTOSIS = 3, FEAT_COUNT = 4 };

struct CheckSessionSummary {
    int  slot;
    char dominant_status[16];
    int  count_normal, count_waspada, count_bahaya, count_diam;
    float avg_health_score;
    float temp_start, temp_end, temp_delta;
    unsigned long duration_ms;
    int  total_samples;
    int  count_diagnosis_unbalance, count_diagnosis_misalign, count_diagnosis_bpfo, count_diagnosis_bpfi;
};