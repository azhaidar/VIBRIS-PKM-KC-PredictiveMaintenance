// GenericThresholdClassifier.h
#pragma once
#include "SharedTypes.h"

// Klasifikasi status mesin memakai ambang batas TETAP (generik), TANPA proses
// kalibrasi/self-baseline apa pun. Cocok dipakai begitu perangkat baru menyala
// atau modul sensor dipindah ke mesin/lokasi lain, karena tidak butuh periode
// belajar (60 detik) sebelum status pertama muncul di dashboard.
//
// severityScoreOut (opsional, boleh NULL): rasio pembacaan sensor terhadap
// ambang batas Bahaya, dipakai dashboard sebagai pengganti tampilan "D2" agar
// tetap ada angka keparahan yang bisa dipantau/di-plot.
const char* classifyStatusFixedThreshold(const SensorFeatures &features, float* severityScoreOut);
