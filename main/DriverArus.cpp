// DriverArus.cpp
// ===================================================================
// CATATAN DERIVASI ARUS_CAL_FACTOR (WAJIB UPDATE SESUAI HARDWARE):
//
// Rumus: CAL_FACTOR = (CT_RATIO / BURDEN_OHM) / ADC_MAX
//
// Contoh untuk SCT-30A:
//   CT Ratio    : 1800:1 (cek datasheet modul spesifikmu)
//   Burden R    : 62Ω (nilai burden resistor yang terpasang)
//   ADC Range   : 4095 (12-bit)
//   Faktor      : (1800 / 62) / 4095 = 0.00709
//
// CARA KALIBRASI EMPIRIS (lebih akurat):
//   1. Clamp SCT ke kabel motor yang dialiri arus diketahui
//      (ukur pakai tang ampere sebagai referensi)
//   2. Print rmsADC ke Serial tanpa dikali CAL_FACTOR
//   3. CAL_FACTOR = arus_referensi_ampere / rmsADC_terbaca
//
// PERBARUI nilai ini setelah kalibrasi fisik, jangan pakai angka tebakan.
// ===================================================================

#include "DriverArus.h"
#include "config.h"
#include <math.h>

static volatile float g_lastRawADC = 0.0f;
float DriverArus_GetLastRawADC() {
    return g_lastRawADC; 
}

#define ARUS_RMS_SAMPLE_COUNT   600   // override ARUS_SAMPLE_COUNT di config.h khusus file ini
#include "MultiSensorFeatureMerger.h"
// Jumlah sample untuk fase warm-up HPF (filter harus converge ke baseline
// sebelum RMS mulai dihitung — spike transient awal dibuang di sini)
#define ARUS_WARMUP_SAMPLES     200

void TaskDriverArus(void *pvParameters) {
    (void)pvParameters;

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_SCT_ADC, ADC_11db); // range penuh 0–3.3V
    float dcEstimate = 0.0f;
    for (int i = 0; i < 32; i++) {
        dcEstimate += (float)analogRead(PIN_SCT_ADC);
        delayMicroseconds(100);
    }
    dcEstimate /= 32.0f;

    float dcBaseline = dcEstimate; // mulai dari estimasi 32-sample awal, sama seperti sebelumnya
    const float dcTrackBeta = 3.2e-5f; // cutoff ~0.05Hz (time constant ~20 detik)
    for (int i = 0; i < ARUS_WARMUP_SAMPLES; i++) {
        float rawSample = (float)analogRead(PIN_SCT_ADC);
        dcBaseline += dcTrackBeta * (rawSample - dcBaseline);
        delayMicroseconds(100);
    }

    for (;;) {
            double sumSquared = 0.0;
            uint32_t t0 = micros();

            for (int i = 0; i < ARUS_RMS_SAMPLE_COUNT; i++) {
                float rawSample = (float)analogRead(PIN_SCT_ADC);
                dcBaseline += dcTrackBeta * (rawSample - dcBaseline);
                float acSample = rawSample - dcBaseline;
                sumSquared += (double)(acSample * acSample);

                if (i % 100 == 99) {
                    vTaskDelay(1);
                } else {
                    delayMicroseconds(100);
                }
            }

            uint32_t elapsedUs = micros() - t0;
            float actualHz = (float)ARUS_RMS_SAMPLE_COUNT * 1000000.0f / (float)elapsedUs;
            if (fabsf(actualHz - 10000.0f) > 200.0f) {
                Serial.printf("[WARNING][DriverArus] Target 10000Hz TIDAK tercapai! Aktual=%.1fHz\n", actualHz);
            }

            float meanSquare = (float)(sumSquared / ARUS_RMS_SAMPLE_COUNT);
            float rmsADC = sqrtf(meanSquare);
            g_lastRawADC = rmsADC;
            // FIX (21 Agustus 2026): print ini sebelumnya TANPA syarat -- jalan
            // ~10x/detik di task terpisah (TaskDriverArus), gak nurut ke flag
            // DEBUG_VERBOSE yang sudah ada di config.h ("0 = sesi ambil data
            // resmi/JSON bersih"). Karena ESP32 dual-core, print ini bisa
            // nyelip di tengah baris JSON yang lagi dikirim loop utama, motong
            // baris itu jadi rusak -- itu penyebab "[LOGGER] Baris JSON rusak,
            // dilewati." yang berulang-ulang di layar. Device TIDAK hang, cuma
            // Serial-nya kebanjiran print debug ini. Sekarang cuma nyala kalau
            // DEBUG_VERBOSE=1 (mode debug manual), mati total pas sesi resmi.
            #if DEBUG_VERBOSE
            Serial.printf("[ARUS-DIAG] dcBaseline=%.2f rmsADC=%.4f\n", dcBaseline, rmsADC);
            #endif

            float calculatedCurrent = rmsADC * ARUS_CAL_FACTOR;
            if (calculatedCurrent < ARUS_NOISE_GATE) {
                calculatedCurrent = 0.0f;
            }
            updateCurrentFeature(calculatedCurrent);

            vTaskDelay(pdMS_TO_TICKS(TICK_DELAY_ARUS));
        }
}
