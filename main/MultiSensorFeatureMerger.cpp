#include "MultiSensorFeatureMerger.h"
#include "config.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ===================================================================
// STATE INTERNAL — SATU-SATUNYA PEMILIK DATA GABUNGAN
// Driver TIDAK BOLEH lagi menulis dataMesinGlobal secara langsung.
// Semua penulisan wajib lewat fungsi updateXFeature() di bawah ini.
// ===================================================================

static SemaphoreHandle_t mergerMutex = NULL;

static float latestFeatures[FEAT_COUNT] = {0.0f, 0.0f, SUHU_DEFAULT_VALID};
static uint32_t lastUpdateTimestamp[FEAT_COUNT] = {0, 0, 0};

// Lazy-init mutex: aman dipanggil dari task manapun yang start duluan
static void ensureMutexInitialized() {
    if (mergerMutex == NULL) {
        mergerMutex = xSemaphoreCreateMutex();
    }
}

static void writeFeature(FeatureIndex idx, float value) {
    ensureMutexInitialized();
    if (xSemaphoreTake(mergerMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        latestFeatures[idx] = value;
        lastUpdateTimestamp[idx] = millis();
        xSemaphoreGive(mergerMutex);
    }
    // Kalau mutex gagal diambil dalam 50ms, update dilewati siklus ini.
    // Lebih aman drop satu sample daripada block task sensor selamanya.
}
void updateKurtosisFeature(float value) {
    writeFeature(FEAT_KURTOSIS, value);
}

// ===================================================================
// API PUBLIK — dipanggil oleh masing-masing Driver
// ===================================================================

void updateVibrationFeature(float rmsValue) {
    writeFeature(FEAT_VIBRATION, rmsValue);
}

void updateAudioFeature(float rmsValue) {
    writeFeature(FEAT_AUDIO, rmsValue);
}

// void updateCurrentFeature(float rmsValue) {
//     (void)rmsValue;  // Sensor arus nonaktif sementara (lihat ENABLE_ARUS_SENSOR di config.h).
//                       // Tidak lagi ditulis ke vektor fitur Mahalanobis (sekarang 3 dimensi).
// }

// BARU: arus AKTIF LAGI, tapi jalurnya TERPISAH -- HANYA buat TinyML,
// SENGAJA TIDAK ditulis ke FEAT_COUNT/writeFeature() supaya TIDAK ikut
// masuk ke vektor Mahalanobis (yang tetap 3 dimensi: getaran, suara, suhu).
static volatile float latestArusForTinyML = 0.0f;

void updateCurrentFeature(float rmsValue) {
    latestArusForTinyML = rmsValue;
}

float getLatestArusForTinyML() {
    return latestArusForTinyML;
}

void updateTemperatureFeature(float value) {
    writeFeature(FEAT_TEMP, value);
}

// ===================================================================
// PEMBACAAN GABUNGAN — dipanggil oleh MahalanobisDetector, dll.
// ===================================================================
// ===================================================================
// LAJU PERUBAHAN SUHU (dT/dt) -- fitur ke-3 Mahalanobis, gantikan suhu absolut.
// Alasan: suhu absolut naik wajar saat motor manasin (bukan anomali),
// bikin D2 meledak. Yang menandai bahaya itu suhu MELONJAK MENDADAK
// (dT besar), bukan suhu tinggi. EMA smoothing meredam noise dT.
// ===================================================================
static float lastTempForRate = -999.0f;   // -999 = belum ada bacaan sebelumnya
static uint32_t lastTempRateTime = 0;
static float smoothedTempRate = 0.0f;
#define TEMP_RATE_EMA_ALPHA 0.2f   // 0-1: makin kecil makin halus (tuning di sini)
// FIX (25 Agustus 2026): batas waktu "dianggap diam" sebelum rate ditarik
// balik ke 0. Dipilih 2x TICK_DELAY_SUHU (750ms di config.h) = 1.5 detik --
// itu jarak wajar antar 2 pembacaan sensor suhu real, jadi kalau sampai 2x
// lipat itu suhu masih sama, kita cukup yakin laju sebenarnya sudah ~0.
#define TEMP_RATE_STALE_SECONDS 1.5f

float getSmoothedTempRate(float currentTemp) {
    uint32_t now = millis();

    if (lastTempForRate < -900.0f) {
        // bacaan pertama: belum bisa hitung laju, anggap 0
        lastTempForRate = currentTemp;
        lastTempRateTime = now;
        return 0.0f;
    }

    // FIX (20 Agustus 2026): sebelumnya dt dihitung dari "waktu sejak
    // PEMANGGILAN fungsi ini terakhir" -- padahal sensor suhu (MLX90614,
    // task terpisah TaskDriverSuhu di DriverSuhu.cpp) update jauh lebih
    // lambat daripada loop() utama manggil fungsi ini. Akibatnya: begitu
    // sensor akhirnya update, delta suhu asli (terkumpul selama SEKIAN
    // lama sejak update terakhir) dibagi dt yang cuma sependek 1 interval
    // loop() -- rate-nya jadi dipalsukan jauh lebih besar dari laju asli.
    // Parahnya lagi, seberapa parah pemalsuan ini beda-beda tergantung
    // SECEPAT APA loop() jalan saat itu -- pas kalibrasi (loop ringan,
    // cuma nyimpen sample) vs pas monitoring (loop berat: diagnosis
    // audio+TinyML+trend+dst, otomatis lebih lambat) -- jadi baseline &
    // pembacaan real-time bisa punya skala yang gak konsisten walau
    // sensornya sama & mesinnya gak berubah kondisi. Data 20 Agustus 2026
    // nunjukin D2 nyangkut di ~65-90 terus-terusan pasca kalibrasi padahal
    // getaran & suara udah cocok sama baseline -- fitur suhu ini kandidat
    // kuat penyebabnya. Fix: kalau suhu BELUM berubah dari pembacaan
    // terakhir, jangan hitung rate baru sama sekali (dt terus menumpuk
    // sampai suhu beneran berubah, gak lagi ke-reset tiap panggilan).
    // FIX (25 Agustus 2026): baris "if belum berubah, return apa adanya" di
    // atas ini nyimpen BUG BARU yang ketauan dari data motor NORMAL asli
    // (sesi 08:12): sensor suhu MLX90614 cuma update tiap 750ms
    // (TICK_DELAY_SUHU di config.h), dan tiap kali update dia LONCAT dalam
    // langkah diskrit (mis. 39.39 -> 39.45 -> 39.73 -> 40.09), BUKAN naik
    // halus kontinu. Satu loncatan 0.3-0.4 derajat dalam waktu singkat itu
    // menghasilkan rawRate SESAAT yang gede (bisa >0.5 derajat/detik),
    // padahal laju pemanasan motor SEBENARNYA cuma ~0.01 derajat/detik
    // kalau dirata-rata semenit. Masalahnya: begitu smoothedTempRate
    // "kena" nilai loncatan gede itu, dia DIBEKUKAN di situ selama suhu
    // belum berubah lagi (early return di atas) -- bisa 1-2 detik ke depan
    // nilainya nyangkut tinggi terus walau nggak ada apa-apa yang terjadi.
    // Makin sering pola ini kejadian, makin lama D2 nyangkut Bahaya padahal
    // motor sehat-sehat saja. Fix-nya: kalau suhu BENERAN belum berubah utk
    // waktu yang cukup lama, itu justru bukti laju sebenarnya SUDAH turun
    // ke 0 -- jadi tarik smoothedTempRate pelan-pelan ke 0 lewat EMA yang
    // sama, bukan dibekukan di nilai lompatan terakhir.
    if (currentTemp == lastTempForRate) {
        float dtSinceLastChange = (now - lastTempRateTime) / 1000.0f;
        if (dtSinceLastChange > TEMP_RATE_STALE_SECONDS) {
            smoothedTempRate = TEMP_RATE_EMA_ALPHA * 0.0f + (1.0f - TEMP_RATE_EMA_ALPHA) * smoothedTempRate;
        }
        return smoothedTempRate;
    }

    float dtSeconds = (now - lastTempRateTime) / 1000.0f;
    if (dtSeconds < 0.001f) return smoothedTempRate;   // hindari bagi nol

    float rawRate = (currentTemp - lastTempForRate) / dtSeconds;   // derajat/detik
    // EMA smoothing: rate baru = alpha*rawRate + (1-alpha)*rate lama
    smoothedTempRate = TEMP_RATE_EMA_ALPHA * rawRate + (1.0f - TEMP_RATE_EMA_ALPHA) * smoothedTempRate;

    lastTempForRate = currentTemp;
    lastTempRateTime = now;
    return smoothedTempRate;
}
bool getMergedFeatures(SensorFeatures *output) {
    ensureMutexInitialized();

    if (xSemaphoreTake(mergerMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false; // Gagal ambil lock, jangan kasih data setengah-update
    }

    uint32_t now = millis();
    bool allFresh = true;

    for (int i = 0; i < FEAT_COUNT; i++) {
        // Overflow-safe: millis() wrap-around tetap benar karena unsigned subtraction
        if ((now - lastUpdateTimestamp[i]) > FEATURE_STALENESS_MS) {
            allFresh = false;
        }
    }

    output->rms_getaran = latestFeatures[FEAT_VIBRATION];
    output->rms_suara   = latestFeatures[FEAT_AUDIO];
    output->arus        = 0.0f;
    output->suhu        = latestFeatures[FEAT_TEMP];
    output->kurtosis    = latestFeatures[FEAT_KURTOSIS];
    output->valid       = allFresh;

    xSemaphoreGive(mergerMutex);
    return allFresh;
}
