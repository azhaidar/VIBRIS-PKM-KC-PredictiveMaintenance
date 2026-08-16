#include "RPMEstimator.h"
#include <math.h>
#include <Arduino.h>
#include <algorithm>

// Batas rentang RPM motor kecil sesuai proposal (300-3000 RPM = 5-50 Hz)
// Di luar rentang ini diabaikan supaya nggak salah tangkap noise frekuensi
// rendah (getaran lingkungan, goyangan meja) atau harmonik tinggi yang
// bukan representasi RPM asli.
#define FR_MIN_HZ 5.0
#define FR_MAX_HZ 50.0

static float g_snrCalibBuffer[200];
static int   g_snrCalibCount = 0;
static float g_runtimeSNRThreshold = 3.0f;  // fallback awal -- disesuaikan dari data SNR
                                              // real motor uji (konsisten 2.1-4.9 di 2 sesi
                                              // logging terakhir), BUKAN tebakan. Nilai lama
                                              // (6.0) gak pernah kelewatan oleh SNR asli motor
                                              // ini, jadi RPM SELALU 0 sepanjang fase kalibrasi
                                              // -> baseline band-energy selalu dilatih dari
                                              // data nol. Threshold final tetap dihitung ulang
                                              // otomatis dari data kalibrasi setelah fase ini
                                              // selesai (lihat computeSNRThresholdFromCalibration).
void resetSNRCalibration() { g_snrCalibCount = 0; }

void addSNRCalibrationSample(float snr) {
    if (g_snrCalibCount < 200) g_snrCalibBuffer[g_snrCalibCount++] = snr;
}

float computeSNRThresholdFromCalibration() {
    if (g_snrCalibCount < 10) return g_runtimeSNRThreshold; // data kurang, pakai fallback

    float sum = 0, sumSq = 0;
    for (int i = 0; i < g_snrCalibCount; i++) sum += g_snrCalibBuffer[i];
    float mean = sum / g_snrCalibCount;
    for (int i = 0; i < g_snrCalibCount; i++) sumSq += (g_snrCalibBuffer[i]-mean)*(g_snrCalibBuffer[i]-mean);
    float stdDev = sqrt(sumSq / g_snrCalibCount);

    // Threshold = mean SNR saat mesin normal, dikurangi margin 2*std,
    // tapi tidak boleh di bawah 3.0 (batas minimal biar tetap ada jarak dari noise murni)
    float threshold = mean - 2.0f * stdDev;
    return (threshold < 3.0f) ? 3.0f : threshold;
}

void setRuntimeSNRThreshold(float threshold) { g_runtimeSNRThreshold = threshold; }
bool RPM_IsSignalReliable(double *magnitude, int n, float sampleRate, float *snrOut) {
    float freqResolution = sampleRate / n;
    int binMin = (int)(FR_MIN_HZ / freqResolution);
    int binMax = (int)(FR_MAX_HZ / freqResolution);

    float peakAmp = 0;
    for (int i = binMin; i <= binMax && i < n / 2; i++) {
        if (magnitude[i] > peakAmp) peakAmp = magnitude[i];
    }

    // PERBAIKAN: median, bukan rata-rata. Harmonik motor kuat (2x/3x RPM,
    // blade-pass) yang jatuh di luar 5-50Hz menaikkan rata-rata sehingga SNR
    // malah jatuh saat sinyal rotasi asli kuat. Median tahan terhadap outlier itu.
    static double noiseSamples[256];
    int noiseCount = 0;
    for (int i = 1; i < binMin && noiseCount < 256; i++) noiseSamples[noiseCount++] = magnitude[i];
    for (int i = binMax + 1; i < n / 2 && noiseCount < 256; i++) noiseSamples[noiseCount++] = magnitude[i];

    float noiseFloor = 1e-6f;
    if (noiseCount > 0) {
        std::sort(noiseSamples, noiseSamples + noiseCount);
        noiseFloor = (float)noiseSamples[noiseCount / 2];
        if (noiseFloor < 1e-6f) noiseFloor = 1e-6f;
    }


    float snr = peakAmp / noiseFloor;
    if (snrOut != NULL) *snrOut = snr;

    // Threshold empiris: puncak harus minimal 3x lebih tinggi dari noise floor
    // supaya dianggap sinyal putaran nyata, bukan kebetulan noise tertinggi.
    // WAJIB dikalibrasi ulang pakai data motor nyata kalian — angka 3.0 ini
    // starting point, bukan angka final. Uji: motor mati vs motor nyala,
    // print SNR keduanya, tentukan threshold yang memisahkan dua kondisi jelas.

    extern float g_runtimeSNRThreshold; // atau lewat getter, sesuaikan struktur kalian
    return snr >= g_runtimeSNRThreshold;
}
float RPM_Estimate(double *magnitude, int n, float sampleRate) {
    // Resolusi frekuensi per bin FFT = sampleRate / jumlah sample.
    // Ini nentuin seberapa presisi kita bisa bedain 1 frekuensi dari frekuensi
    // di sebelahnya. Semakin banyak sample (n), semakin presisi, tapi juga
    // semakin lambat responnya (window waktu lebih panjang).
    float freqResolution = sampleRate / n;

    // Konversi batas Hz ke index bin FFT, karena kita nyari di array bin
    // bukan langsung di domain Hz.
    int binMin = (int)(FR_MIN_HZ / freqResolution);
    int binMax = (int)(FR_MAX_HZ / freqResolution);

    // Cari bin dengan amplitudo tertinggi HANYA di rentang bin yang masuk akal.
    // Ini asumsi fisika: motor manapun, walau idealnya balanced, selalu ada
    // sedikit unbalance alami, sehingga puncak amplitudo dominan biasanya
    // muncul tepat di frekuensi putar motornya sendiri (1x RPM).
    float maxAmplitude = 0;
    int maxBinIndex = binMin;

    for (int i = binMin; i <= binMax && i < n / 2; i++) {
        // n/2 karena spektrum FFT simetris, cuma separuh pertama yang punya
        // informasi frekuensi unik (Nyquist).
        if (magnitude[i] > maxAmplitude) {
            maxAmplitude = magnitude[i];
            maxBinIndex = i;
        }
    }
    int halfBinIndex = maxBinIndex / 2;
    if (halfBinIndex >= binMin) {
        float halfAmplitude = (float)magnitude[halfBinIndex];
        // Kalau puncak di setengah frekuensi itu masih cukup besar (misal >40% dari puncak utama),
        // curigai puncak utama itu harmonik 2x -- pindah pegangan ke yang lebih rendah
        if (halfAmplitude > 0.4f * maxAmplitude) {
            maxBinIndex = halfBinIndex;
            maxAmplitude = halfAmplitude;
        }
    }
    // Konversi index bin balik ke frekuensi (Hz), lalu ke RPM (x60)
    float refinedBin = (float)maxBinIndex;
        if (maxBinIndex > 0 && maxBinIndex < (n / 2) - 1) {
            float alpha = (float)magnitude[maxBinIndex - 1];
            float beta  = (float)magnitude[maxBinIndex];
            float gamma = (float)magnitude[maxBinIndex + 1];
            float denom = (alpha - 2.0f * beta + gamma);
            if (fabsf(denom) > 1e-9f) {
                float p = 0.5f * (alpha - gamma) / denom;
                refinedBin = (float)maxBinIndex + p;
            }
        }
        float fr_hz = refinedBin * freqResolution;
        return fr_hz * 60.0;
}
float RPM_ComputeBPFO(float fr_hz, int n_balls, float d_ball, float D_pitch, float phi_deg) {
    float phi_rad = phi_deg * (PI / 180.0f);
    return (n_balls / 2.0f) * fr_hz * (1.0f - (d_ball / D_pitch) * cos(phi_rad));
}

float RPM_ComputeBPFI(float fr_hz, int n_balls, float d_ball, float D_pitch, float phi_deg) {
    float phi_rad = phi_deg * (PI / 180.0f);
    return (n_balls / 2.0f) * fr_hz * (1.0f + (d_ball / D_pitch) * cos(phi_rad));
}

float RPM_ComputeBSF(float fr_hz, int n_balls, float d_ball,
                     float D_pitch, float phi_deg) {
    // Rumus dari gambar bearing yang kamu upload:
    // BSF = (Pd/2Bd) * fr * [1 - (Bd/Pd * cos(phi))^2]
    float phi_rad = phi_deg * (PI / 180.0f);
    float ratio   = (d_ball / D_pitch) * cosf(phi_rad);
    return (D_pitch / (2.0f * d_ball)) * fr_hz * (1.0f - ratio * ratio);
}

float RPM_ComputeFTF(float fr_hz, float d_ball,
                     float D_pitch, float phi_deg) {
    // Rumus dari gambar bearing yang kamu upload:
    // FTF = (fr/2) * [1 - (Bd/Pd * cos(phi))]
    float phi_rad = phi_deg * (PI / 180.0f);
    return (fr_hz / 2.0f) * (1.0f - (d_ball / D_pitch) * cosf(phi_rad));
}