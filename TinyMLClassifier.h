// TinyMLClassifier.h
#pragma once
#include "SharedTypes.h"
/*
MODUL: TinyML Classifier (Edge Impulse)
Menjalankan model hasil training Edge Impulse (impulse:
"radityaputraa-sketch-project-1") langsung di ESP32, memakai skema
SENSOR FUSION yang sama persis dengan urutan fitur saat training:
    rms_v + rms_x + rms_y + rms_z + rms_a + current + temp + rpm
(EI_CLASSIFIER_FUSION_AXES_STRING) — 8 nilai per frame, 2 frame per
window (EI_CLASSIFIER_RAW_SAMPLE_COUNT = 2), diambil setiap 1 detik
(EI_CLASSIFIER_INTERVAL_MS = 1000). Jadi tiap 2 detik akan ada SATU
hasil klasifikasi baru: "hidup1" / "hidup2" / "mati1".
Modul ini TIDAK menggantikan GenericThresholdClassifier (status
Normal/Waspada/Bahaya tetap dari situ). Ini cuma menambah label
ML sebagai output terpisah, sesuai porsi tugas TinyML di tim.
CARA PAKAI (di main.ino):
    TinyML_Init();                          // sekali di setup()
    ...
    TinyML_Update(merged, result.rpm_estimated); // tiap iterasi loop()
    if (TinyML_HasNewResult()) {
        strncpy(result.ml_label, TinyML_GetLabel(), sizeof(result.ml_label)-1);
        result.ml_confidence = TinyML_GetConfidence();
    }
*/
void TinyML_Init();
// Dipanggil TIAP loop(). Non-blocking: fungsi ini sendiri yang mengatur
// kapan waktunya sampling (tiap 1 detik) dan kapan waktunya inferensi
// (tiap 2 sample terkumpul), jadi aman dipanggil sesering apa pun.
void TinyML_Update(const SensorFeatures &features, float rpmEstimated);
// true HANYA pada satu iterasi loop() tepat setelah inferensi baru selesai
// dihitung (jadi transmitter tahu kapan harus mengirim label baru, bukan
// label lama yang belum berubah).
bool TinyML_HasNewResult();
// Label & confidence hasil klasifikasi TERAKHIR (tetap tersimpan sampai
// ada hasil baru, aman dipanggil kapan saja meski TinyML_HasNewResult()
// == false).
const char* TinyML_GetLabel();
float TinyML_GetConfidence();