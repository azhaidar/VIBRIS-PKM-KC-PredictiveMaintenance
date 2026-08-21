// config.h
#pragma once

// ===================================================================
// 1. HARDWARE MAPPING (GPIO DEFINITIONS) - STRICTLY REFERENCING SCHEMATIC
// ===================================================================
// Sensor Arus (SCT 30A) - Connected to Analog-to-Digital Converter 1
#define PIN_SCT_ADC         4   // ADC1_CH3
// Sensor Getaran (LIS3DH) - Connected to Dedicated I2C Bus
#define PIN_LIS3DH_SDA      10  // I2C Serial Data
#define PIN_LIS3DH_SCL      9   // I2C Serial Clock
// Sensor Akustik / Mikrofon (INMP441) - Connected to Hardware I2S0
#define PIN_INM_I2S_SD      16  // I2S Serial Data
#define PIN_INM_I2S_WS      17  // I2S Word Select / Left-Right Clock
#define PIN_INM_I2S_SCK     18  // I2S Continuous Serial Clock
// Sensor Suhu (DS18H / DS18B20) - Connected to OneWire Bus
//#define PIN_DS18B20_DATA    5   // OneWire Digital I/O
#define PIN_MLX_SCL         6   // I2C Clock GY-906 (MLX90614)
#define PIN_MLX_SDA         7   // I2C Data GY-906 (MLX90614)
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
// FIX (21 Agustus 2026): dinaikkan dari 256 -> 512. Alasannya resolusi
// frekuensi = VIBRATION_SAMPLE_RATE_HZ / FFT_SAMPLES = 1230/256 = ~4,8Hz
// per "kotak" -- itu LEBIH LEBAR dari jendela toleransi ±10% band Unbalance
// (oneX_hz ~23,33Hz -> jendelanya cuma ~4,66Hz), jadi jendela itu nyaris
// cuma nangkep 1 kotak, gampang meleset kalau RPM motor geser dikit dari
// 1400 asumsi. Di 512, resolusi jadi ~2,4Hz/kotak -- band Unbalance dapet
// ~2 kotak, lebih ada toleransi.
// KONSEKUENSI yang harus kamu tahu: waktu 1 siklus FFT (FFT_SAMPLES /
// VIBRATION_SAMPLE_RATE_HZ) naik dari ~208ms jadi ~416ms. Karena
// SPECTRAL_AVG_COUNT (di FFTProcessor.cpp) masih 12 kali rata-rata SEBELUM
// keluar 1 hasil, total jeda per hasil naik dari ~2,5 detik jadi ~5 detik.
// Ini SENGAJA tidak aku kurangi SPECTRAL_AVG_COUNT-nya buat kompensasi --
// karena SNR kamu sekarang masih jadi masalah utama (lihat sesi 21:40),
// dan rata-rata 12 siklus itu yang justru lagi nolongin SNR kamu. Kalau
// nanti mounting udah beres dan SNR udah stabil tinggi, BOLEH pertimbangkan
// turunkan SPECTRAL_AVG_COUNT buat balikin kecepatan respons.
// CATATAN LAIN (belum jadi masalah di 512, tapi WAJIB dicek kalau nanti
// FFT_SAMPLES dinaikkan lagi lebih tinggi): RPMEstimator.cpp baris 59 punya
// array `noiseSamples[256]` yang UKURANNYA HARDCODE, bukan ikut FFT_SAMPLES.
// Di 512 masih aman (jumlah sampel noise yang dikumpulkan ~236, di bawah
// 256), tapi kalau FFT_SAMPLES naik lagi ke 1024 misalnya, larik itu bakal
// kepotong (guard `noiseCount < 256` mencegah crash, tapi noise floor-nya
// jadi dihitung dari data yang gak lengkap) -- perlu dinaikkan juga saat itu.
#define FFT_SAMPLES          512     // Jumlah sampel FFT getaran (Kunci lintas file)
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
#define VIBRATION_ABSOLUTE_FLOOR 1.2f

// FIX (20 Agustus 2026): ambang batas AMPLITUDO MUTLAK, bukan adaptif/belajar
// sendiri (percobaan sebelumnya pakai ambang belajar-sendiri "ambientRmsEMA"
// sudah DIHAPUS -- lihat catatan di FFTProcessor.cpp -- karena protokol uji
// kita gak pernah kasih dia data "motor diam" buat dipelajari, jadi gak
// pernah kepenuhi). Angka ini FIXED, ditentukan dari data ASLI 23 file
// snapshot_BAHAYA/WASPADA tanggal 19 Agustus 2026 (folder logs/): rms_v
// ngumpul jadi 2 kelompok jelas -- kelompok "noise/gak beneran muter"
// di 0.087-0.95, kelompok "beneran bergetar" di 1.69-5.01, dengan CELAH
// KOSONG di antaranya. 1.2 dipilih persis di tengah celah itu.
// DIPAKAI DI: FFTProcessor.cpp, sebagai syarat TAMBAHAN (bukan pengganti)
// dari cek SNR yang sudah ada -- supaya sinyal cuma dianggap "motor jalan"
// kalau SNR-nya bagus DAN amplitudonya beneran cukup besar.
// PENTING: kalau nanti sensor/mounting getaran diganti, atau motor baru
// yang jauh lebih kecil/besar dipakai, angka ini WAJIB dicek ulang dengan
// cara yang sama (kumpulin rms_v dari data nyata, cari celahnya) -- jangan
// diasumsikan otomatis masih benar.
#define VIBRATION_ABSOLUTE_FLOOR 1.2f


#define FIXED_BPFO_HZ  69.6f
#define FIXED_BPFI_HZ  117.1f
// BARU:
#define ENABLE_RPM_DIAGNOSIS 1   // AKTIF: band diagnosis pakai frekuensi FIXED 
                                   // per klaster (lihat BEARING_TABLE di SharedTypes.h),
                                   // BUKAN lagi RPM real-time. Lihat FFTProcessor.cpp.
#define BAND_WINDOW_PERCENT 0.10f  // toleransi ±10% di sekitar frekuensi fixed
#define DEBUG_VERBOSE 0   // 0 = sesi ambil data resmi (JSON bersih), 1 = debug manual
#define CHECK_SESSION_DURATION_MS 60000UL   // 1 menit nanti ubah terserah kalian dah ya
#define ENABLE_ARUS_SENSOR 1   // AKTIF arusnya

// FIX (21 Agustus 2026): MATIKAN pembelajaran baseline online (yang tadinya
// jalan otomatis di SETIAP siklus deteksi lewat updateBaselineIfNormal() /
// updateBandBaselineIfNormal() di MahalanobisDetector.cpp). Ditemukan dari
// data uji `vibris_20260821_1823_kondisiUnbalance.csv` (baut sengaja
// dipasang di shaft, ground_truth UNBALANCE 2926 baris): getaran mentah
// (rms_v) TIDAK turun sepanjang sesi (3.81 -> 4.67 -> 4.78 per ~42 detik),
// tapi status yang dibaca sistem melompat dari 52% Normal ke 98.6% Normal
// dalam waktu yang SAMA. Sebabnya: baseline "belajar" dari keputusannya
// SENDIRI tiap siklus (feedback loop tertutup) -- begitu 1 siklus kebaca
// Normal (walau keliru/borderline), baseline langsung digeser dikit ke arah
// bacaan itu, bikin siklus berikutnya makin gampang kebaca Normal juga,
// snowball sampai motor yang sengaja dirusak kebaca sehat.
//
// KENAPA DIMATIKAN TOTAL (bukan cuma dikurangi kecepatannya): VIBRIS ini
// alat PORTABLE (1 alat dipindah-pindah ke banyak mesin), bukan node yang
// nempel permanen di 1 mesin selama berbulan-bulan. Fitur ini awalnya
// dimaksudkan buat "belajar keausan bearing yang wajar, pelan-pelan selama
// berbulan-bulan" -- tapi kalau alatnya dipindah ke mesin lain sebelum itu
// kejadian, fitur ini gak akan pernah kepakai sesuai niatnya, dan yang ada
// cuma risiko fault ketutupan diam-diam kayak di data di atas. Referensi
// "sehat" sekarang HARUS berasal dari sumber eksplisit (preset pabrikan di
// FactoryPresets.h, atau kalibrasi manual tombol 'R') dan tetap DIAM selama
// sesi cek berlangsung -- baru boleh berubah kalau kamu eksplisit kalibrasi
// ulang lagi. Set ke 1 HANYA kalau nanti mau eksperimen ulang fitur belajar
// bertahap ini (Opsi B di diskusi) -- itu butuh desain "rem"/drift-cap dulu,
// belum ada di kode ini, JANGAN diaktifkan mentah-mentah tanpa itu.
#define ENABLE_ONLINE_BASELINE_LEARNING 0