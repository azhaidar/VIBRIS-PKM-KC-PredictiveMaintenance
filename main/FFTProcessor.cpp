// FFTProcessor.cpp
#include "FFTProcessor.h"
#include <arduinoFFT.h>
#include <math.h>
#include "RPMEstimator.h"
#include "config.h"

#define SAMPLE_RATE VIBRATION_SAMPLE_RATE_HZ
#define FR_MIN_HZ 5.0
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


    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = input->samples[i] - mean;
        vImag[i] = 0;
    }

    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    for (int i = 0; i < FFT_SAMPLES / 2; i++) {
        avgMagnitude[i] += vReal[i];
    }
    avgAccumCount++;

    float sumSquare = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) sumSquare += input->samples[i] * input->samples[i];
    features->rms_getaran = sqrt(sumSquare / FFT_SAMPLES);

    float effectiveSampleRate = (input->actual_rate_hz > 1.0f) ?
        input->actual_rate_hz : SAMPLE_RATE;

    if (avgAccumCount < SPECTRAL_AVG_COUNT) {
        FFTProcessor_ExtractTopPeaks(vReal, FFT_SAMPLES / 2, &latestTopPeaks, effectiveSampleRate);

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

    FFTProcessor_ExtractTopPeaks(vReal, FFT_SAMPLES / 2, &latestTopPeaks, effectiveSampleRate);

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
    if (reliableStreak >= 2) stableRPM = fr_rpm;
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