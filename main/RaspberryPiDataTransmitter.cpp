// RaspberryPiDataTransmitter.cpp
#include "RaspberryPiDataTransmitter.h"
#include <Arduino.h>
#include "DualCoreTaskScheduler.h"
#include "DriverArus.h"
#include "DriverSuhu.h"
#include "MultiSensorFeatureMerger.h"
#include "FFTProcessor.h"

// BARU: typedef TopFreqPeaks (mirror dari FFTProcessor.cpp untuk kompatibilitas)
typedef struct {
    float frequency;
    float amplitude;
} FreqPeak;

typedef struct {
    FreqPeak peaks[10];  // Top 10
    uint8_t count;       // Berapa yang terdeteksi (max 10)
} TopFreqPeaks;

// Forward declare FFTProcessor functions
extern void FFTProcessor_GetTopPeaks(TopFreqPeaks* dest);
extern void FFTProcessor_GetPeakBins(uint16_t* bins, float* freqRes);

void Transmitter_Init(long baudRate) {
    // Tidak perlu Serial1.begin() — Serial (USB) sudah di-init di setup() main.ino
    // dengan baudRate yang sama (115200). Fungsi ini sengaja jadi no-op,
    // dipertahankan agar kontrak fungsi tetap konsisten kalau nanti pindah
    // ke UART1/GPIO terpisah (produk final).
    (void)baudRate;
    Serial.println(F("[Transmitter] Mode USB — data dikirim lewat port Serial yang sama dengan debug."));
}
void Transmitter_SendResult(SensorFeatures features, DetectionResult result, const char* groundTruthLabel) {
    float rmsX = 0.0f, rmsY = 0.0f, rmsZ = 0.0f;
    Scheduler_GetLatestAxisRMS(&rmsX, &rmsY, &rmsZ);

    float bandEnergies[4];
    Scheduler_GetLatestBandEnergies(bandEnergies);

    float audioBandEnergies[AUDIO_BAND_COUNT];
    Scheduler_GetLatestAudioBandEnergies(audioBandEnergies);

    // BARU: ambil top 10 frekuensi dari FFT processor
    TopFreqPeaks topPeaks = {0};
    FFTProcessor_GetTopPeaks(&topPeaks);

    // Format string untuk top_freqs dan top_amps (10 puncak)
    char topFreqsStr[120] = "";
    char topAmpsStr[120] = "";
    for (uint8_t i = 0; i < topPeaks.count && i < 10; i++) {
        if (i > 0) {
            strcat(topFreqsStr, ",");
            strcat(topAmpsStr, ",");
        }
        char buf[20];
        snprintf(buf, sizeof(buf), "%.1f", topPeaks.peaks[i].frequency);
        strcat(topFreqsStr, buf);
        snprintf(buf, sizeof(buf), "%.1f", topPeaks.peaks[i].amplitude);
        strcat(topAmpsStr, buf);
    }

    // BARU: ambil bin indices dan freq resolution untuk tracking
    uint16_t peakBins[10] = {0};
    float freqResolution = 0.0f;
    FFTProcessor_GetPeakBins(peakBins, &freqResolution);

    // Build string untuk FFT bin indices
    char peakBinsStr[80] = "";
    for (int i = 0; i < 10; i++) {
        if (i > 0) strcat(peakBinsStr, ",");
        char buf[10];
        snprintf(buf, sizeof(buf), "%u", peakBins[i]);
        strcat(peakBinsStr, buf);
    }

    #if ENABLE_ARUS_SENSOR
        float arusValue = getLatestArusForTinyML();   // ambil dari jalur baru, bukan features.arus
        float arusRawADC = DriverArus_GetLastRawADC();
    #else
        float arusValue = 0.0f;
        float arusRawADC = 0.0f;
    #endif


    Serial.printf(
        "{"
        "\"rms_v\":%.4f,\"rms_x\":%.4f,\"rms_y\":%.4f,\"rms_z\":%.4f,"
        "\"rms_a\":%.6f,\"rms_suara_db\":%.2f,\"cur\":%.4f,\"cur_raw_adc\":%.2f,"
        "\"temp\":%.2f,\"temp_raw\":%.3f,\"temp_rate\":%.3f,"
        "\"rpm\":%.2f,\"snr\":%.2f,\"d2\":%.3f,\"status\":\"%s\",\"kurtosis\":%.3f,"
        "\"e_unbalance\":%.4f,\"e_misalign\":%.4f,\"e_bpfo\":%.4f,\"e_bpfi\":%.4f,"
        "\"diagnosis\":\"%s\",\"diag_conf\":%.2f,"
        "\"e_audio_low\":%.4f,\"e_audio_mid\":%.4f,\"e_audio_high\":%.4f,"
        "\"audio_diagnosis\":\"%s\",\"audio_diag_conf\":%.2f,"
        "\"roughness\":%.6f,\"brightness\":%.6f,"
        "\"health_score\":%.1f,\"trend\":\"%s\","
        "\"servis_estimasi\":\"%s\","
        "\"ml_label\":\"%s\",\"ml_conf\":%.2f,"
        "\"diag_flags\":%d,"
        "\"top_freqs\":\"%s\",\"top_amps\":\"%s\","
        "\"fft_peak_bins\":\"%s\",\"fft_freq_res\":%.4f,"
        "\"ground_truth\":\"%s\""
        "}\n",

        features.rms_getaran, rmsX, rmsY, rmsZ,
        features.rms_suara, features.rms_suara_db, arusValue, arusRawADC,
        features.suhu, DriverSuhu_GetLastRawTemp(), getSmoothedTempRate(features.suhu),
        result.rpm_estimated, Scheduler_GetLatestSNR(), result.mahalanobis_D2, result.status_label, features.kurtosis,
        bandEnergies[0], bandEnergies[1], bandEnergies[2], bandEnergies[3],
        result.diagnosis_label, result.diagnosis_confidence,
        audioBandEnergies[0], audioBandEnergies[1], audioBandEnergies[2],
        result.audio_diagnosis_label, result.audio_diagnosis_confidence,
        Scheduler_GetLatestRoughness(), Scheduler_GetLatestBrightness(),
        result.health_score, result.trend,
        result.servis_estimasi,
        result.ml_label, result.ml_confidence,
        result.diagnosis_flags,
        topFreqsStr, topAmpsStr,
        peakBinsStr, freqResolution,
        groundTruthLabel
    );
}
void Transmitter_SendSessionSummary(CheckSessionSummary s) {
    Serial.printf(
        "{\"type\":\"session_summary\",\"slot\":%d,\"dominant\":\"%s\","
        "\"n_normal\":%d,\"n_waspada\":%d,\"n_bahaya\":%d,\"n_diam\":%d,"
        "\"n_unbalance\":%d,\"n_misalign\":%d,\"n_bpfo\":%d,\"n_bpfi\":%d,"
        "\"avg_health\":%.1f,\"temp_start\":%.2f,\"temp_end\":%.2f,\"temp_delta\":%.2f,"
        "\"duration_ms\":%lu,\"total_samples\":%d}\n",
        s.slot, s.dominant_status, s.count_normal, s.count_waspada, s.count_bahaya, s.count_diam,
        s.count_diagnosis_unbalance, s.count_diagnosis_misalign, s.count_diagnosis_bpfo, s.count_diagnosis_bpfi,
        s.avg_health_score, s.temp_start, s.temp_end, s.temp_delta,
        s.duration_ms, s.total_samples
    );
}