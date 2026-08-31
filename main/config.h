// config.h
#pragma once

#define PIN_SCT_ADC         4
#define PIN_LIS3DH_SDA      10
#define PIN_LIS3DH_SCL      9
#define PIN_INM_I2S_SD      16
#define PIN_INM_I2S_WS      17
#define PIN_INM_I2S_SCK     18
#define PIN_MLX_SCL         6
#define PIN_MLX_SDA         7

#define CORE_DSP_HIGH_SPEED  0
#define CORE_SYSTEM_SLOW_IO  1

#define PRIO_TASK_INM        3
#define PRIO_TASK_FFT        3
#define PRIO_TASK_ARUS       2
#define PRIO_TASK_SUHU       1
#define PRIO_TASK_VIB        3

#define STACK_TASK_INM       6144
#define STACK_TASK_FFT       8192
#define STACK_TASK_ARUS      3072
#define STACK_TASK_SUHU      3072

#define FEATURE_STALENESS_MS  2000
#define TICK_DELAY_SUHU      750
#define SUHU_MAX_DELTA       1.5000
#define SUHU_DEFAULT_VALID   27.0

#define TICK_DELAY_ARUS      100
#define ARUS_ADC_OFFSET      2048
#define ARUS_CAL_FACTOR      0.0242f
#define ARUS_NOISE_GATE      0.5f

#define FFT_SAMPLES          512
#define TICK_DELAY_REPORT    100
#define VIBRATION_SAMPLE_RATE_HZ 3600U
#define VIBRATION_SAMPLE_PERIOD_US (1000000UL / VIBRATION_SAMPLE_RATE_HZ)

#define AUDIO_FFT_SAMPLES     1024
#define AUDIO_SAMPLE_RATE_HZ  16000U
#define AUDIO_BAND_COUNT      3

#define RPM_MAX_DELTA_PERCENT   0.20f
#define RPM_MAX_DELTA_MIN       50.0f
#define PRIO_TASK_AUDIO_FFT     1
#define STACK_TASK_AUDIO_FFT    4096
#define VIBRATION_ABSOLUTE_FLOOR 0.05f

// FIX (30 Agustus 2026): Conversion acceleration (G) ke velocity (mm/s)
// Formula: V(mm/s) = A(m/s²) / (2π × f_Hz) × 1000
// Karena tidak semua frekuensi diketahui saat capture, gunakan estimasi dominan
// dari FFT. Default frekuensi untuk small motor ~25Hz (unbalance 1X di 1500 RPM)
#define VIB_DEFAULT_DOMINANT_FREQ_HZ 25.0f
#define ACCEL_TO_VEL_FACTOR (1000.0f / (2.0f * M_PI * VIB_DEFAULT_DOMINANT_FREQ_HZ))
// Jika default 25Hz: factor ≈ 6.37 (berarti 1G accel ≈ 6.37 mm/s velocity)

#define FIXED_BPFO_HZ  69.6f
#define FIXED_BPFI_HZ  117.1f
#define ENABLE_RPM_DIAGNOSIS 1
#define BAND_WINDOW_PERCENT 0.10f
#define DEBUG_VERBOSE 0
#define CHECK_SESSION_DURATION_MS 60000UL
#define ENABLE_ARUS_SENSOR 0
#define ENABLE_ONLINE_BASELINE_LEARNING 0
