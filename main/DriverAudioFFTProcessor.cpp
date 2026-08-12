// AudioFFTProcessor.cpp
#include "DriverAudioFFTProcessor.h"
#include <arduinoFFT.h>
#include <math.h>
#include "config.h"

#define AUDIO_SAMPLE_RATE AUDIO_SAMPLE_RATE_HZ

static double aReal[AUDIO_FFT_SAMPLES];
static double aImag[AUDIO_FFT_SAMPLES];
static ArduinoFFT<double> audioFFT = ArduinoFFT<double>(aReal, aImag, AUDIO_FFT_SAMPLES, AUDIO_SAMPLE_RATE);

// ===================================================================
// BATAS BAND -- HEURISTIK AWAL, BELUM DIVALIDASI EMPIRIS.
// Lihat catatan keterbatasan di AudioFFTProcessor.h. Ganti angka ini
// setelah kalian bandingkan spektrum rekaman motor NORMAL vs motor
// dengan kerusakan diketahui (bearing kering, dsb).
// ===================================================================
#define AUDIO_BAND_LOW_MIN_HZ    100.0f   // "rumble" mekanis frekuensi rendah
#define AUDIO_BAND_LOW_MAX_HZ    500.0f
#define AUDIO_BAND_MID_MIN_HZ    500.0f   // dengungan motor normal (baseline)
#define AUDIO_BAND_MID_MAX_HZ   2000.0f
#define AUDIO_BAND_HIGH_MIN_HZ  2000.0f   // gesekan/decitan frekuensi tinggi
#define AUDIO_BAND_HIGH_MAX_HZ  6000.0f

void DriverAudioFFTProcessor_Init() {}

static float audioBandEnergy(double *magnitude, float freqResolution,
                              float f_low, float f_high, int n) {
    int binLow  = (int)(f_low / freqResolution);
    int binHigh = (int)(f_high / freqResolution);
    float energy = 0;
    double energySum = 0;
    int binCount = 0;
    for (int i = binLow; i <= binHigh; i++) {
        energySum += magnitude[i];
        binCount++;
    }
    if (binCount == 0) return 0.0f;
    return (float)(energySum / binCount);
}
// Roughness — Ota & Unoki, IEEE Access 2023, persamaan (14)
// Mengukur fluktuasi amplitudo antar-sampel. Naik saat bearing rusak.
static float computeAudioRoughness(float *samples, int n) {
    if (n < 2) return 0.0f;
    float sumDiff = 0.0f;
    for (int i = 1; i < n; i++) {
        sumDiff += fabsf(samples[i] - samples[i-1]);
    }
    return sumDiff / (float)(n - 1);
}

// Brightness — Ota & Unoki, IEEE Access 2023, persamaan (8-9)
// Rasio energi >2kHz terhadap total. Naik saat ada gesekan/decitan.
static float computeAudioBrightness(double *magnitude, float freqRes, int n) {
    float energyHigh = 0.0f, energyTotal = 0.0f;
    for (int k = 0; k < n / 2; k++) {
        float freq = k * freqRes;
        energyTotal += (float)(magnitude[k]);
        if (freq >= 2000.0f) energyHigh += (float)(magnitude[k]);
    }
    return (energyTotal > 0.0f) ? energyHigh / energyTotal : 0.0f;
}

void DriverAudioFFTProcessor_Process(AudioBuffer *input, float *bandEnergies_out,
                                      float *roughness_out, float *brightness_out) {

    double mean = 0;
    for (int i = 0; i < AUDIO_FFT_SAMPLES; i++) mean += input->samples[i];
    mean /= AUDIO_FFT_SAMPLES;

    for (int i = 0; i < AUDIO_FFT_SAMPLES; i++) {
        aReal[i] = (double)input->samples[i] - mean;
        aImag[i] = 0;
    }

    audioFFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    audioFFT.compute(FFTDirection::Forward);
    audioFFT.complexToMagnitude();


    float freqRes = (float)AUDIO_SAMPLE_RATE / AUDIO_FFT_SAMPLES;

    bandEnergies_out[0] = audioBandEnergy(aReal, freqRes, AUDIO_BAND_LOW_MIN_HZ,  AUDIO_BAND_LOW_MAX_HZ,  AUDIO_FFT_SAMPLES);
    bandEnergies_out[1] = audioBandEnergy(aReal, freqRes, AUDIO_BAND_MID_MIN_HZ,  AUDIO_BAND_MID_MAX_HZ,  AUDIO_FFT_SAMPLES);
    bandEnergies_out[2] = audioBandEnergy(aReal, freqRes, AUDIO_BAND_HIGH_MIN_HZ, AUDIO_BAND_HIGH_MAX_HZ, AUDIO_FFT_SAMPLES);

    // Metric tambahan dari paper IEEE Access 2023 — untuk validasi dan presentasi
    float roughness  = computeAudioRoughness(input->samples, AUDIO_FFT_SAMPLES);
    float brightness = computeAudioBrightness(aReal, freqRes, AUDIO_FFT_SAMPLES);
    
    Serial.printf("[AUDIO] Roughness=%.4f Brightness=%.4f\n", roughness, brightness);
if (roughness_out)  *roughness_out  = roughness;
if (brightness_out) *brightness_out = brightness;
}