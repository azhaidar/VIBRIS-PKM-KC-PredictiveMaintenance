// GenericThresholdClassifier.cpp - tidak dipakai
#include "GenericThresholdClassifier.h"
#include <string.h>

// ===================================================================
// AMBANG BATAS GENERIK — TIDAK BERGANTUNG PADA KALIBRASI PER-MESIN
// ===================================================================
// Angka-angka ini SENGAJA disamakan dengan ambang batas fallback yang
// sebelumnya sudah dipakai di sisi dashboard Python (lihat fungsi
// _evaluate_diagnosis di dbintegrasi.py), supaya firmware dan dashboard
// selalu konsisten satu sama lain, di mana pun modul sensor dipasang.
//
// Kalau nanti karakteristik mesin uji berbeda jauh (motor lebih besar/kecil,
// dudukan sensor beda, dll.), cukup ubah angka di sini — tidak perlu
// mengulang proses kalibrasi apa pun.
#define VIB_WASPADA   0.18f   // RMS getaran (satuan sesuai keluaran DriverGetaran)
#define VIB_BAHAYA    0.25f
#define TEMP_WASPADA  42.0f   // Celcius
#define TEMP_BAHAYA   50.0f   // Celcius
#define TEMP_BASELINE 28.0f
#define VIB_WEIGHT   0.7f   // BARU: getaran adalah fokus utama deteksi
#define TEMP_WEIGHT  0.3f   // BARU: suhu jadi faktor pendukung, bukan penentu utama

const char* classifyStatusFixedThreshold(const SensorFeatures &features, float* severityScoreOut) {
    // Skor keparahan = seberapa dekat pembacaan saat ini terhadap ambang
    // batas Bahaya (1.0 = tepat di ambang Bahaya). Dipakai dashboard sebagai
    // pengganti angka Mahalanobis D2 yang dulu perlu baseline.
    float vibScore  = features.rms_getaran / VIB_BAHAYA;

    // UBAH: skor suhu sekarang dihitung relatif terhadap KENAIKAN di atas
    // suhu ruangan normal, bukan dari 0. Ini mencegah suhu ruangan wajar
    // (28-30C) otomatis menghasilkan skor tinggi (~0.6) padahal tidak ada
    // anomali apa pun.
    float tempScore = (features.suhu - TEMP_BASELINE) / (TEMP_BAHAYA - TEMP_BASELINE);
    if (tempScore < 0.0f) tempScore = 0.0f;  // guard: suhu di bawah baseline tidak boleh jadi skor negatif

    // UBAH: severity sekarang weighted average (getaran lebih dominan),
    // BUKAN MAX() dari dua skor. MAX() sebelumnya membuat suhu selalu
    // "menang" karena skalanya secara alami lebih besar dari getaran normal.
    float severity = (VIB_WEIGHT * vibScore) + (TEMP_WEIGHT * tempScore);
    if (severityScoreOut) {
        *severityScoreOut = severity;
    }

    if (features.rms_getaran > VIB_BAHAYA || features.suhu > TEMP_BAHAYA) {
        return "Bahaya";
    }
    if (features.rms_getaran > VIB_WASPADA || features.suhu > TEMP_WASPADA) {
        return "Waspada";
    }
    return "Normal";
}
