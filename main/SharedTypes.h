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
    volatile float rms_getaran; 
    volatile float rms_suara;   
    volatile float rms_suara_db; // BARU (26 Agustus 2026): Audio RMS dalam dB (relative to 10mV noise floor)
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

// FIX (21 Agustus 2026): sebelumnya index 0 = bearing 6202 dan index 1 =
// bearing 6203, keduanya masih dihitung pakai fr_hz 1400RPM (oneX_hz=23.33
// di kedua baris). Setelah dikonfirmasi ke pemilik alat: Klaster 1 (Motor 1)
// itu SEBENARNYA bearing 6203 di ~1400RPM, dan Klaster 2 (Motor 2) itu
// bearing 6201 di ~2800RPM -- bukan 6202 sama sekali. 6202 dibuang dari
// tabel karena bukan salah satu dari 2 motor uji kalian.
//
// Index 0 (6203): geometri (n_balls=8, d_ball=6.75mm, D_pitch=28.5mm) SAMA
// seperti sebelumnya -- itu memang datasheet 6203, cuma dulu salah taruh di
// slot yang salah. bpfo_hz/bpfi_hz DIHITUNG ULANG dengan fr_hz 1400RPM/60=
// 23.33Hz lewat rumus RPM_ComputeBPFO/BPFI (RPMEstimator.cpp baris 150-158):
// bpfo = (n/2)*fr*(1-(d/D)) = 71.23 Hz, bpfi = (n/2)*fr*(1+(d/D)) = 115.44 Hz.
//
// Index 1 (6201) -- UPDATE KEDUA (21 Agustus 2026): d_ball_mm diganti LAGI,
// dari 7.5mm (hasil ukur jangka sorong manual, ternyata KELIRU) jadi 5.953mm
// (dari sumber katalog/datasheet yang dikasih user belakangan). KENAPA yang
// 7.5mm dibuang, bukan yang dipertahankan padahal itu "hasil ukur langsung":
// 5.953mm itu = PERSIS 15/64 inci (15/64 x 25.4 = 5.953mm) -- bola bearing
// memang lazim pakai ukuran pecahan inci walau housingnya metrik. Ini
// terkonfirmasi silang: bola 6203 dari sumber yang sama (6.747mm) = PERSIS
// 17/64 inci, dan itu cocok 99.96% sama angka 6.75mm yang SUDAH lebih dulu
// ada & dipercaya di index 0. Dua kecocokan pola sekaligus ini jauh lebih
// meyakinkan dibanding satu angka ukur manual yang si penguji sendiri sudah
// bilang susah diukur gara-gara ketutup sangkar (cage) -- kemungkinan besar
// jangka sorongnya kesenggol pinggiran sangkar, bukan pas di badan bola.
// n_balls=7 TETAP dari hitungan fisik langsung (itu gak kena masalah cage,
// tetap dipercaya). D_pitch_mm=22.0 TETAP dihitung dari rata-rata bore+OD
// (12+32)/2 -- metode sama seperti sebelumnya, belum ada alasan berubah.
// bpfo_hz/bpfi_hz DIHITUNG ULANG pakai fr_hz 2800RPM/60=46.67Hz dan d_ball
// yang baru: bpfo = (7/2)*46.67*(1-5.953/22.0) = 119.14 Hz,
// bpfi = (7/2)*46.67*(1+5.953/22.0) = 207.53 Hz.
static const BearingSpec BEARING_TABLE[] = {
    // n_balls, Bd(mm), Pd(mm), phi(deg), label
    {8, 6.75f, 28.5f, 0.0f, "Klaster 1 (Motor 1 - Maestri): ~1400RPM (6203)", 23.33f, 46.67f, 71.23f, 115.44f},
    {7, 5.953f, 22.0f, 0.0f, "Klaster 2 (Motor 2 - Shimizu): ~2800RPM (6201)", 46.67f, 93.33f, 119.14f, 207.53f},
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