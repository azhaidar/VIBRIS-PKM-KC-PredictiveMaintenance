// DriverINM.cpp
#include "DriverINM.h"
#include "config.h"
#include <driver/i2s.h>
#include <math.h>
#include "MultiSensorFeatureMerger.h"
#include "DualCoreTaskScheduler.h"   // BARU

/**
 * @brief Eksekusi Pembacaan Stream Audio INMP441 via I2S DMA
 * @param pvParameters Pointer memori bersama (SensorFeatures*)
 */
void TaskDriverINM(void *pvParameters) {
    // Casting pointer parameter ke objek memori bersama
    (void)pvParameters;
     
    // Konfigurasi internal periferal hardware I2S ESP32-S3
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),// Master mode, hanya menerima data
        .sample_rate = 16000,                               // Frekuensi sampling audio 16 kHz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,       // INMP441 mengirimkan data dalam slot 32-bit
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,        // Mengambil data dari saluran tunggal (Mono)
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,  // Protokol standar I2S
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,           // Alokasi interupsi level 1 (Rendah/Aman)
        .dma_buf_count = 4,                                 // Jumlah ring-buffer DMA
        .dma_buf_len = 1024,                                // Ukuran masing-masing buffer DMA (1024 sampel)
        .use_apll = false                                   // Menggunakan clock PLL internal biasa
    };
    
    // Pemetaan pin fisik berdasarkan Gambar Konfigurasi Hardware Anda
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_INM_I2S_SCK,                      // Pin 18
        .ws_io_num = PIN_INM_I2S_WS,                        // Pin 17
        .data_out_num = I2S_PIN_NO_CHANGE,                  // Tidak digunakan untuk mode perekaman
        .data_in_num = PIN_INM_I2S_SD                       // Pin 16
    };

    // Pemasangan driver ke unit I2S_NUM_0
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    // FIX ABSOLUT: Menggunakan keyword static agar buffer dialokasikan di HEAP/BSS, 
    // bukan di dalam STACK task yang terbatas (Mitigasi Stack Canary Panic)
    static int32_t i2s_raw_buffer[1024]; 
    size_t bytes_read = 0;

    static AudioBuffer localAudioBuffer;   // BARU
    for (;;) {
        // Block-state: Task ditidurkan secara total oleh OS sampai buffer DMA hardware terisi penuh
        i2s_read(I2S_NUM_0, &i2s_raw_buffer, sizeof(i2s_raw_buffer), &bytes_read, portMAX_DELAY);
        
        int samples_read = bytes_read / 4; // 1 sampel = 4 byte (32-bit)
        
        if (samples_read > 0) {
            int64_t sumSquaredValues = 0;
            int valid_sample_count = 0;

            for (int i = 0; i < samples_read; i++) {
                // INMP441 menghasilkan data terjustifikasi kiri (Left-Justified) 24-bit didalam slot 32-bit.
                // Lakukan shift kanan sebanyak 14-bit untuk mendapatkan nilai integer bertanda yang valid.
                int32_t cleanSample = i2s_raw_buffer[i] >> 8;
                
                // Akumulasi kuadrat sinyal audio untuk kalkulasi daya suara (RMS)
                sumSquaredValues += (int64_t)cleanSample * (int64_t)cleanSample;
                valid_sample_count++;

                if (i < AUDIO_FFT_SAMPLES) {
                    localAudioBuffer.samples[i] = (float)cleanSample/8388608.0f;  
                }
            }
            
            if (valid_sample_count > 0) {
                float meanSquare = (float)sumSquaredValues / valid_sample_count;
                float rmsAudio = sqrtf(meanSquare)/8388608.0f;
                
                // Amankan penulisan nilai amplitudo suara rata-rata ke shared memory
                updateAudioFeature(rmsAudio);
                localAudioBuffer.timestamp = millis();
                QueueHandle_t aq = Scheduler_GetAudioQueue();
                if (aq != NULL) xQueueOverwrite(aq, &localAudioBuffer);
            }
        }
        
        // Jeda minimal untuk stabilitas context switching FreeRTOS di Core 0
    }
}
