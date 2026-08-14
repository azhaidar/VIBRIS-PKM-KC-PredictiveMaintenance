// config.h
#pragma once

// ===================================================================
// 1. HARDWARE MAPPING (GPIO DEFINITIONS) - STRICTLY REFERENCING SCHEMATIC
// ===================================================================
// Sensor Arus (SCT 30A) - Connected to Analog-to-Digital Converter 1
#define PIN_SCT_ADC         4   // ADC1_CH3
// Sensor Getaran (LIS3DH) - Connected to Dedicated I2C Bus
#define PIN_LIS3DH_SDA      7  // I2C Serial Data
#define PIN_LIS3DH_SCL      6   // I2C Serial Clock
// Sensor Akustik / Mikrofon (INMP441) - Connected to Hardware I2S0
#define PIN_INM_I2S_SD      16  // I2S Serial Data
#define PIN_INM_I2S_WS      17  // I2S Word Select / Left-Right Clock
#define PIN_INM_I2S_SCK     18  // I2S Continuous Serial Clock
// Sensor Suhu (DS18H / DS18B20) - Connected to OneWire Bus
//#define PIN_DS18B20_DATA    5   // OneWire Digital I/O
#define PIN_MLX_SCL         9   // I2C Clock GY-906 (MLX90614)
#define PIN_MLX_SDA         10   // I2C Data GY-906 (MLX90614)
// ===================================================================
// 2. FREERTOS TASK ARCHITECTURE (CORES, PRIORITIES, & STACKS)
// ===================================================================
// Core Assignments
#define CORE_DSP_HIGH_SPEED  0   // Core untuk pemrosesan sinyal digital cepat (INM, LIS3DH, SCT)
#define CORE_SYSTEM_SLOW_IO  1   // Core untuk interaksi lambat & pelaporan data (DS18H, Serial)
// Task Priorities (Higher number = Higher priority)
#define PRIO_TASK_INM        3   // Prioritas tertinggi: Mencegah I2S DMA buffer overflow
#define PRIO_TASK_FFT        3   // Prioritas tertinggi: Komputasi berat getaran berkejaran waktu
#define PRIO_TASK_ARUS       2   // Prioritas menengah: Sampling RMS berkala
#define PRIO_TASK_SUHU       1   // Prioritas terendah: Sensor lambat & toleran terhadap jitter
#define PRIO_TASK_VIB        3
// Task Stack Sizes (Allocated in Bytes for ESP32-S3)
#define STACK_TASK_INM       6144
#define STACK_TASK_FFT       8192  // Stack besar untuk kalkulasi array matematika float
#define STACK_TASK_ARUS      3072
#define STACK_TASK_SUHU      3072
// ===================================================================
// 3. DSP & SENSOR OPERATIONAL PARAMETERS (MATH & TIMING)
// ===================================================================
#define FEATURE_STALENESS_MS  2000
// Sensor Suhu (DS18H) Configuration
#define TICK_DELAY_SUHU      750     // Waktu tunggu konversi sensor suhu (ms)
#define SUHU_MAX_DELTA       1.5000  // Ambang batas Slew Rate Limiter suhu
#define SUHU_DEFAULT_VALID   27.0    // Nilai fallback jika sensor error
// Sensor Arus (SCT) Configuration
//burden internakl, 10uF kapasitor, 10k 2x resisrot
#define TICK_DELAY_ARUS      100     // Jeda eksekusi antar perhitungan RMS (ms)
#define ARUS_ADC_OFFSET      2048    // Titik tengah ADC 12-bit (VCC / 2)
#define ARUS_CAL_FACTOR      0.0242f   // Koefisien pengali kalibrasi nilai ADC ke Ampere
#define ARUS_NOISE_GATE      0.5f
// Digital Signal Processing Configuration
#define FFT_SAMPLES          256     // Jumlah sampel FFT getaran (Kunci lintas file)
// Sistem Monitoring Configuration
#define TICK_DELAY_REPORT    100   // Interval Serial Print pelaporan data (ms)
#define VIBRATION_SAMPLE_RATE_HZ 1230U
#define VIBRATION_SAMPLE_PERIOD_US \
    (1000000UL / VIBRATION_SAMPLE_RATE_HZ)

#define AUDIO_FFT_SAMPLES     1024
#define AUDIO_SAMPLE_RATE_HZ  16000U

#define AUDIO_BAND_COUNT      3   // BARU

#define RPM_MAX_DELTA_PERCENT   0.20f   // BARU
#define RPM_MAX_DELTA_MIN       50.0f   // BARU
#define PRIO_TASK_AUDIO_FFT     1       // BARU
#define STACK_TASK_AUDIO_FFT    4096    // BARU
#define ENABLE_RPM_DIAGNOSIS 0   // 0 = diagnosis berbasis RPM/BPFO/BPFI nonaktif
                                   // (sesuai arahan: fokus murni domain frekuensi,
                                   // tanpa RPM). 1 = aktifkan lagi kalau nanti dibutuhkan.
#define DEBUG_VERBOSE 0   // 0 = sesi ambil data resmi (JSON bersih), 1 = debug manual
