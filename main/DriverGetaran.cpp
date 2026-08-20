// DriverGetaran.cpp
#include "DriverGetaran.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include "DualCoreTaskScheduler.h"

portMUX_TYPE getaranMux = portMUX_INITIALIZER_UNLOCKED;
static TwoWire I2CLis3dh = TwoWire(0);
static Adafruit_LIS3DH lis3dhInstance = Adafruit_LIS3DH(&I2CLis3dh);
void TaskDriverGetaran(void *pvParameters) {
    (void)pvParameters;

    I2CLis3dh.begin(PIN_LIS3DH_SDA, PIN_LIS3DH_SCL, 400000);

    // FIX (20 Agustus 2026): sebelumnya lis3dhInstance.begin(0x18) cuma
    // dicoba SEKALI -- gagal 1x di sini = macet PERMANEN sepanjang boot
    // itu (for(;;) di bawah gak ada jalan keluar), status jadi "SensorFault"
    // terus dan rms_v/rpm/d2 semua 0. Data lapangan 20 Agustus 2026 buktikan
    // sensornya sebenarnya kadang cuma belum "siap" tepat di momen ini
    // (race condition power-up I2C) -- terbukti begitu ESP32 di-reset ulang
    // (buka Serial Monitor Arduino IDE ATAU buka loggerserial.py, dua-duanya
    // memicu reset via DTR), kadang langsung kedeteksi normal tanpa apapun
    // disentuh fisik. Sekarang dikasih jeda + coba ulang terbatas DULU
    // sebelum bener-bener dianggap gagal -- kalau memang gagal terus
    // (kabel/solder beneran bermasalah), tetap masuk for(;;) yang sama
    // seperti sebelumnya, bukan disembunyikan.
    bool lis3dhFound = false;
    for (int attempt = 0; attempt < 10 && !lis3dhFound; attempt++) {
        if (lis3dhInstance.begin(0x18)) {
            lis3dhFound = true;
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));   // kasih waktu sensor selesai power-up sebelum coba lagi
        }
    }
    if (!lis3dhFound) {
        for (;;) {
            Serial.println(F("[ERROR] LIS3DH Tidak Terdeteksi! (sudah dicoba 10x -- cek sambungan SDA/SCL & solderan modul)"));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    lis3dhInstance.setRange(LIS3DH_RANGE_4_G);
    lis3dhInstance.setDataRate(LIS3DH_DATARATE_LOWPOWER_5KHZ);

    uint32_t nextSampleUs = micros();
    const float alpha = 0.98f;
    float filteredXOld = 0.0f, filteredYOld = 0.0f, filteredZOld = 0.0f;
    float rawXOld = 0.0f, rawYOld = 0.0f, rawZOld = 0.0f;
    static VibrationBuffer localVibBuffer;

    int stuckReadingStreak = 0;
    const int STUCK_WARNING_THRESHOLD = 50;
    const float SAMPLE_RATE_TOLERANCE = 0.05f;
    static float rawMagnitudeOld = 0.0f;

    for (;;) {
        sensors_event_t event;
        uint32_t batchStartUs = micros();
        int overrunCount = 0;
        double sumSqX = 0.0, sumSqY = 0.0, sumSqZ = 0.0;

        for (int i = 0; i < FFT_SAMPLES; i++) {
            bool readOk = lis3dhInstance.getEvent(&event);

            float ax = event.acceleration.x;
            float ay = event.acceleration.y;
            float az = event.acceleration.z;

            float rawMagnitude = sqrtf((ax * ax) + (ay * ay) + (az * az));
            if (!readOk || rawMagnitude == rawMagnitudeOld) {
                stuckReadingStreak++;
            } else {
                stuckReadingStreak = 0;
            }
            rawMagnitudeOld = rawMagnitude;

            if (stuckReadingStreak == STUCK_WARNING_THRESHOLD) {
                Serial.println(F("[WARNING] Sensor getaran (LIS3DH) kemungkinan MACET: "
                                  "bacaan I2C sama persis berkali-kali. Cek sambungan "
                                  "SDA/SCL & solderan modul sensor"));
            }

            float fx = alpha * (filteredXOld + ax - rawXOld);
            float fy = alpha * (filteredYOld + ay - rawYOld);
            float fz = alpha * (filteredZOld + az - rawZOld);
            filteredXOld = fx; rawXOld = ax;
            filteredYOld = fy; rawYOld = ay;
            filteredZOld = fz; rawZOld = az;

            float dynamicVibration = sqrtf(fx * fx + fy * fy + fz * fz);
            localVibBuffer.samples[i] = dynamicVibration;

            sumSqX += (double)(fx * fx);
            sumSqY += (double)(fy * fy);
            sumSqZ += (double)(fz * fz);

            nextSampleUs += VIBRATION_SAMPLE_PERIOD_US;
            if ((int32_t)(micros() - nextSampleUs) >= 0) {
                overrunCount++;
            }
            while ((int32_t)(micros() - nextSampleUs) < 0) {
                taskYIELD();
            }
        }

        localVibBuffer.rms_x_raw = sqrtf((float)(sumSqX / FFT_SAMPLES));
        localVibBuffer.rms_y_raw = sqrtf((float)(sumSqY / FFT_SAMPLES));
        localVibBuffer.rms_z_raw = sqrtf((float)(sumSqZ / FFT_SAMPLES));

        uint32_t batchElapsedUs = micros() - batchStartUs;
        float actualRateHz = (float)FFT_SAMPLES * 1000000.0f / (float)batchElapsedUs;
        float rateError = fabsf(actualRateHz - (float)VIBRATION_SAMPLE_RATE_HZ) / (float)VIBRATION_SAMPLE_RATE_HZ;
        localVibBuffer.actual_rate_hz = actualRateHz;
        if (rateError > SAMPLE_RATE_TOLERANCE || overrunCount > 0) {
            #if DEBUG_VERBOSE
                Serial.printf("[WARNING][DriverGetaran] Target %uHz TIDAK tercapai! Aktual=%.1fHz "
                          "(%d/%d sample overrun/telat). FFTProcessor tetap menghitung pakai "
                          "asumsi %uHz -> RPM & band energy BISA MELESET. Turunkan "
                          "VIBRATION_SAMPLE_RATE_HZ di config.h ke nilai yang tercapai, atau "
                          "optimasi I2C (naikkan clock/kurangi overhead driver).\n",
                          VIBRATION_SAMPLE_RATE_HZ, actualRateHz, overrunCount, FFT_SAMPLES,
                          VIBRATION_SAMPLE_RATE_HZ);
            #endif
        }

        QueueHandle_t q = Scheduler_GetVibrationQueue();
        if (q != NULL) {
            xQueueOverwrite(q, &localVibBuffer);
        }
    }
}