// FFTProcessor.cpp
#include "FFTProcessor.h"
#include <arduinoFFT.h>
#include <math.h>
#include "RPMEstimator.h"
#include "config.h"

#ifndef M_PI
  #define M_PI 3.14159265358979323846f
#endif

#define SAMPLE_RATE VIBRATION_SAMPLE_RATE_HZ
// FIX (31 Agustus 2026, putaran ke-9): samain dengan RPMEstimator.cpp --
// lihat komentar FR_MIN_HZ di file itu buat alasan lengkapnya.
#define FR_MIN_HZ 15.0
#define FR_MAX_HZ 50.0

typedef struct {
    float frequency;
    float amplitude;
} FreqPeak;

typedef struct {
    FreqPeak peaks[10];
    uint8_t count;
    uint16_t bin_indices[10];
    float freq_resolution;
} TopFreqPeaks;
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE);

BearingSpec currentBearingSpec = BEARING_TABLE[BEARING_DEFAULT_INDEX];

static bool hasRollingBearing = true;

void setBearingType(bool rollingBearing) {
    hasRollingBearing = rollingBearing;
}
void setBearingCluster(int clusterIndex) {
    if (clusterIndex < 0 || clusterIndex >= (int)BEARING_TABLE_SIZE) {
        Serial.printf("[FFTProcessor] ERROR: indeks klaster %d di luar jangkauan (0-%d).\n",
                      clusterIndex, (int)BEARING_TABLE_SIZE - 1);
        return;
    }
    currentBearingSpec = BEARING_TABLE[clusterIndex];
    Serial.printf("[FFTProcessor] Klaster bearing diganti ke: %s\n", currentBearingSpec.label);
}

static float stableRPM = 0.0f;
static int reliableStreak = 0;
static int unreliableStreak = 0;
#define UNRELIABLE_CONFIRM_STREAK 3

#define SPECTRAL_AVG_COUNT 24
static double avgMagnitude[FFT_SAMPLES / 2] = {0};
static int avgAccumCount = 0;
void FFTProcessor_Init() {}

float bandEnergy(double *magnitude, float freqResolution, float f_low, float f_high, int n) {
    int binLow = (int)(f_low / freqResolution);
    int binHigh = (int)(f_high / freqResolution);
    float energy = 0;
    for (int i = binLow; i <= binHigh && i < n/2; i++) {
        energy += magnitude[i] * magnitude[i];
    }
    return energy;
}

// FIX (31 Agustus 2026, putaran ke-4): dulu accel->velocity convert pakai
// SATU frekuensi (dominan/1x RPM) buat SELURUH sinyal broadband -- itu
// overestimate karena konten di frekuensi lain (noise, harmonik 2x/3x,
// BPFO/BPFI) ikut dibagi pakai frekuensi yang gak sesuai buat mereka.
//
// FIX (31 Agustus 2026, putaran ke-5): percobaan pertama pakai rata-rata
// terbobot energi dari SELURUH band 5-200Hz -- ternyata rentan ketarik ke
// frekuensi rendah kalau ada noise/goyangan dudukan di sekitar 5-10Hz.
// Frekuensi rendah = pembagi (2*pi*f) kecil = hasil MELEDAK, bukan
// mengecil. Sekarang dipersempit: cuma hitung energi di JENDELA SEMPIT
// (+-30%) di sekitar 1x RPM dan 2x RPM yang SUDAH DIKETAHUI dari RPM
// estimator (refFreqHz, dari stableRPM cycle sebelumnya) -- bukan rata-
// rata seluruh spektrum. Ini jauh lebih tahan terhadap noise di frekuensi
// yang gak relevan sama sekali dengan putaran motor yang sebenarnya.
static float computeVelocityRMS(double accelRmsG, double *magnitude, float freqResolution, int n, float refFreqHz) {
    if (refFreqHz < 1.0f) refFreqHz = VIB_DEFAULT_DOMINANT_FREQ_HZ;

    double weightedFreqSum = 0.0, weightSum = 0.0;
    float centers[2] = { refFreqHz, refFreqHz * 2.0f };  // 1x RPM (unbalance) & 2x RPM (misalignment)
    const float windowPercent = 0.3f;

    for (int c = 0; c < 2; c++) {
        float loHz = centers[c] * (1.0f - windowPercent);
        float hiHz = centers[c] * (1.0f + windowPercent);
        int binLo = (int)(loHz / freqResolution);
        int binHi = (int)(hiHz / freqResolution);
        if (binLo < 1) binLo = 1;   // skip bin 0 (DC) -- freq=0 bikin pembagian meledak
        if (binHi >= n / 2) binHi = n / 2 - 1;
        for (int i = binLo; i <= binHi; i++) {
            double w = magnitude[i] * magnitude[i];
            weightedFreqSum += w * (double)(i * freqResolution);
            weightSum += w;
        }
    }

    float fEff = (weightSum > 1e-9) ? (float)(weightedFreqSum / weightSum) : refFreqHz;
    if (fEff < 1.0f) fEff = refFreqHz;

    float factor = 9.81f * 1000.0f / (2.0f * (float)M_PI * fEff);
    return (float)(accelRmsG * factor);
}

static TopFreqPeaks latestTopPeaks = {0};
static uint16_t latestPeakBins[10] = {0};
static float latestFreqRes = 0.0f;

void FFTProcessor_GetTopPeaks(TopFreqPeaks* dest) {
    memcpy(dest, &latestTopPeaks, sizeof(TopFreqPeaks));
}

void FFTProcessor_GetPeakBins(uint16_t* bins, float* freqRes) {
    for (int i = 0; i < 10; i++) bins[i] = latestPeakBins[i];
    *freqRes = latestFreqRes;
}

void FFTProcessor_ExtractTopPeaks(double* fftMagnitude, uint16_t fftSize, TopFreqPeaks* outPeaks, float sampleRate) {
    outPeaks->count = 0;
    float freqRes = sampleRate / fftSize;
    outPeaks->freq_resolution = freqRes;

    for (int i = 0; i < fftSize; i++) {
        float freq = (float)i * freqRes;
        float amp = (float)fftMagnitude[i];

        if (outPeaks->count < 10 || amp > outPeaks->peaks[9].amplitude) {
            int insertPos = outPeaks->count;
            for (int j = 0; j < outPeaks->count && j < 10; j++) {
                if (amp > outPeaks->peaks[j].amplitude) {
                    insertPos = j;
                    break;
                }
            }

            for (int j = (outPeaks->count < 10 ? outPeaks->count : 9); j > insertPos; j--) {
                outPeaks->peaks[j] = outPeaks->peaks[j-1];
                outPeaks->bin_indices[j] = outPeaks->bin_indices[j-1];
            }

            outPeaks->peaks[insertPos].frequency = freq;
            outPeaks->peaks[insertPos].amplitude = amp;
            outPeaks->bin_indices[insertPos] = i;

            if (outPeaks->count < 10) {
                outPeaks->count++;
            }
        }
    }
}
void FFTProcessor_Process(VibrationBuffer *input, SensorFeatures *features,
                            float *rpm_out, float *bandEnergies_out, float *snr_out) {
    double mean = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) mean += input->samples[i];
    mean /= FFT_SAMPLES;

    double sum2 = 0, sum4 = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) {
        double d = input->samples[i] - mean;
        double d2 = d * d;
        sum2 += d2;
        sum4 += d2 * d2;
    }
    double variance = sum2 / FFT_SAMPLES;
    float kurtosis = (variance > 1e-9) ? (float)((sum4 / FFT_SAMPLES) / (variance * variance)) : 0.0f;
    features->kurtosis = kurtosis;

    // FIX (31 Agustus 2026, putaran ke-4): input->samples[i] sekarang
    // akselerasi MENTAH dalam G (DriverGetaran.cpp gak convert ke mm/s
    // lagi di titik sampling). RMS akselerasi ini dihitung dulu di sini,
    // konversi ke velocity (mm/s) menyusul di bawah SETELAH spektrum FFT
    // tersedia (computeVelocityRMS butuh spektrum buat cari frekuensi
    // efektif).
    double sumSquareAccel = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) sumSquareAccel += input->samples[i] * input->samples[i];
    double accelRmsG = sqrt(sumSquareAccel / FFT_SAMPLES);

    float effectiveSampleRate = (input->actual_rate_hz > 1.0f) ?
        input->actual_rate_hz : SAMPLE_RATE;
    float freqResolution = effectiveSampleRate / FFT_SAMPLES;

    // FIX (31 Agustus 2026, putaran ke-5): frekuensi acuan buat konversi
    // velocity -- pakai stableRPM (hasil cycle SEBELUMNYA, static di file
    // ini) kalau ada, biar computeVelocityRMS gak perlu nebak dari
    // spektrum mentah yang belum diproses.
    float refFreqHz = (stableRPM > 60.0f) ? (stableRPM / 60.0f) : VIB_DEFAULT_DOMINANT_FREQ_HZ;

    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = input->samples[i] - mean;
        vImag[i] = 0;
    }

    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    // FIX (31 Agustus 2026, putaran ke-4): rms_getaran (mm/s) sekarang
    // dihitung PER-BIN FREKUENSI pakai spektrum single-cycle ini (belum
    // rata-rata SPECTRAL_AVG_COUNT cycle -- itu baru siap di bawah, di
    // fase akumulasi ini pakai versi single-cycle dulu biar tetap ada
    // angka yang wajar buat ditampilkan selama fase warm-up).
    features->rms_getaran = computeVelocityRMS(accelRmsG, vReal, freqResolution, FFT_SAMPLES, refFreqHz);

    for (int i = 0; i < FFT_SAMPLES / 2; i++) {
        avgMagnitude[i] += vReal[i];
    }
    avgAccumCount++;

    if (avgAccumCount < SPECTRAL_AVG_COUNT) {
        FFTProcessor_ExtractTopPeaks(vReal, FFT_SAMPLES, &latestTopPeaks, effectiveSampleRate);

        // Store bin indices even during accumulation phase
        for (int i = 0; i < 10; i++) {
            latestPeakBins[i] = latestTopPeaks.bin_indices[i];
        }
        latestFreqRes = latestTopPeaks.freq_resolution;

        *rpm_out = stableRPM;
        if (snr_out) *snr_out = 0.0f;
        for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
        return;
    }

    for (int i = 0; i < FFT_SAMPLES / 2; i++) {
        vReal[i] = avgMagnitude[i] / SPECTRAL_AVG_COUNT;
        avgMagnitude[i] = 0.0;
    }
    avgAccumCount = 0;

    // FIX (31 Agustus 2026, putaran ke-4): sekarang spektrumnya sudah versi
    // rata-rata (lebih stabil/gak terlalu noisy dibanding single-cycle di
    // atas) -- recompute rms_getaran pakai spektrum yang lebih bagus ini.
    features->rms_getaran = computeVelocityRMS(accelRmsG, vReal, freqResolution, FFT_SAMPLES, refFreqHz);

    // FIX (31 Agustus 2026, putaran ke-6, SEMENTARA buat diagnosa): print
    // ini nunjukin angka MENTAH di tiap tahap perhitungan velocity, biar
    // ketahuan PERSIS di bagian mana angkanya meledak (bukan tebak-tebakan
    // lagi). Hapus/comment blok ini lagi kalau masalahnya udah ketemu.
    Serial.printf("[VIB-DEBUG] accelRmsG=%.5f G | refFreqHz=%.2f Hz | rms_getaran=%.3f mm/s\n",
                  accelRmsG, refFreqHz, features->rms_getaran);

    FFTProcessor_ExtractTopPeaks(vReal, FFT_SAMPLES, &latestTopPeaks, effectiveSampleRate);

    // FIX (31 Agustus 2026): blok "Store dominant frequency kembali ke
    // input buffer" yang lama di sini gak pernah beneran nulis apa-apa
    // (cuma komentar kosong) -- dominant_freq_hz akhirnya SELALU 0 dan
    // konversi akselerasi->kecepatan di DriverGetaran.cpp SELALU jatuh ke
    // default 25Hz, gak peduli RPM motor yang sebenarnya. Itu penyebab
    // rms_v (mm/s) sering kebaca 2-3x lebih tinggi dari standar ISO 10816.
    // Perbaikannya dipindah ke DualCoreTaskScheduler.cpp (bukan di sini),
    // karena FFTProcessor_Process dan DriverGetaran jalan di task/core
    // berbeda yang cuma tukeran data lewat salinan Queue -- nulis ke
    // "input->dominant_freq_hz" di sini gak akan pernah nyampe balik ke
    // DriverGetaran. Sekarang dipakai variabel volatile + fungsi accessor
    // Scheduler_GetLatestDominantFreqHz(), pola yang sama seperti yang
    // sudah dipakai untuk RPM/SNR/axis-RMS di file yang sama.

    // Store bin indices dan freq resolution untuk diakses Transmitter
    for (int i = 0; i < 10; i++) {
        latestPeakBins[i] = latestTopPeaks.bin_indices[i];
    }
    latestFreqRes = latestTopPeaks.freq_resolution;

    float snr = 0.0f;
    bool snrReliable = RPM_IsSignalReliable(vReal, FFT_SAMPLES, effectiveSampleRate, &snr);
    if (snr_out) *snr_out = snr;

    bool reliable = snrReliable && (features->rms_getaran > VIBRATION_ABSOLUTE_FLOOR);
    float freqResDiag = effectiveSampleRate / FFT_SAMPLES;
    int binMinDiag = (int)(FR_MIN_HZ / freqResDiag);
    if (binMinDiag < 1) binMinDiag = 1;   // FIX (31 Agustus 2026, putaran ke-7): sama, jangan izinkan bin DC
    int binMaxDiag = (int)(FR_MAX_HZ / freqResDiag);
    float top3Amp[3] = {0.0f, 0.0f, 0.0f};
    int top3Bin[3] = {binMinDiag, binMinDiag, binMinDiag};
    for (int i = binMinDiag; i <= binMaxDiag && i < FFT_SAMPLES / 2; i++) {
        float amp = (float)vReal[i];
        if (amp > top3Amp[0]) {
            top3Amp[2] = top3Amp[1]; top3Bin[2] = top3Bin[1];
            top3Amp[1] = top3Amp[0]; top3Bin[1] = top3Bin[0];
            top3Amp[0] = amp; top3Bin[0] = i;
        } else if (amp > top3Amp[1]) {
            top3Amp[2] = top3Amp[1]; top3Bin[2] = top3Bin[1];
            top3Amp[1] = amp; top3Bin[1] = i;
        } else if (amp > top3Amp[2]) {
            top3Amp[2] = amp; top3Bin[2] = i;
        }
    }
    #if DEBUG_VERBOSE
        Serial.printf("[FFT-DIAG] top3: #1=%.2fHz(~%.0fRPM,amp=%.1f) #2=%.2fHz(~%.0fRPM,amp=%.1f) #3=%.2fHz(~%.0fRPM,amp=%.1f) | snr=%.2f snrOK=%d | rms=%.4f\n",
            top3Bin[0]*freqResDiag, top3Bin[0]*freqResDiag*60.0f, top3Amp[0],
            top3Bin[1]*freqResDiag, top3Bin[1]*freqResDiag*60.0f, top3Amp[1],
            top3Bin[2]*freqResDiag, top3Bin[2]*freqResDiag*60.0f, top3Amp[2],
            snr, snrReliable, features->rms_getaran);
    #endif
    if (!reliable) {
        unreliableStreak++;
        if (unreliableStreak >= UNRELIABLE_CONFIRM_STREAK) {
            reliableStreak = 0;
            stableRPM = 0.0f;
            *rpm_out = 0.0f;
            for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
            features->valid = false;
            return;
        } else {
            *rpm_out = stableRPM;
            for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
            features->valid = true;
            return;
        }
    }
    unreliableStreak = 0;

    float fr_rpm = RPM_Estimate(vReal, FFT_SAMPLES, effectiveSampleRate);

    if (stableRPM > 0.0f) {
        float relativeChange = fabsf(fr_rpm - stableRPM) / stableRPM;
        if (relativeChange > 0.6f) {
            fr_rpm = stableRPM;
        }
    }

    reliableStreak++;
    // FIX (31 Agustus 2026, putaran ke-11): dulu cukup 2x pembacaan
    // "reliable" beruntun buat langsung PERCAYA itu RPM valid & dikunci
    // ke stableRPM -- kebukti kelewat gampang, noise sesaat yang
    // kebetulan nembus ambang SNR 2x bisa bikin RPM palsu nyangkut
    // (RPM=1222 muncul padahal sensor didiemin, SNR sempat 0.00 barusan).
    // Dinaikkan ke 5x biar butuh sinyal yang KONSISTEN lebih lama sebelum
    // dipercaya, jauh lebih tahan noise sesaat.
    if (reliableStreak >= 5) stableRPM = fr_rpm;
    *rpm_out = stableRPM;

    float fr_hz = fr_rpm / 60.0;
    float freqRes = effectiveSampleRate / FFT_SAMPLES;

    #if 1
        bandEnergies_out[0] = bandEnergy(vReal, freqRes,
            (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.oneX_hz,
            (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.oneX_hz, FFT_SAMPLES);
        bandEnergies_out[1] = bandEnergy(vReal, freqRes,
            (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.twoX_hz,
            (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.twoX_hz, FFT_SAMPLES);

        if (hasRollingBearing) {
            bandEnergies_out[2] = bandEnergy(vReal, freqRes,
                (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.bpfo_hz,
                (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.bpfo_hz, FFT_SAMPLES);
            bandEnergies_out[3] = bandEnergy(vReal, freqRes,
                (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.bpfi_hz,
                (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.bpfi_hz, FFT_SAMPLES);
        } else {
            bandEnergies_out[2] = 0.0f;
            bandEnergies_out[3] = 0.0f;
        }

    #else
        for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
    #endif
    features->valid = true;
    
}
