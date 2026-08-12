// DriverAudioDiagnosisClassifier.h
#pragma once
#include "SharedTypes.h"

/*
MODUL BARU: Audio Diagnosis Classifier

Rekan AudioFFTProcessor -- versi audio dari DiagnosisClassifier yang
sudah ada untuk getaran. Pola identik: hitung Z-score tiap band terhadap
baseline, cari band paling menyimpang, keluarkan label sesuai band itu.

KETERBATASAN -- LEBIH BESAR dari versi getaran, harus ditekankan ke juri:
DiagnosisClassifier (getaran) berbasis rumus fisik mapan (1x/2x RPM,
BPFO, BPFI dari literatur vibration analysis). Modul ini TIDAK -- batas
band di AudioFFTProcessor.cpp masih heuristik kasar "suara rendah =
kemungkinan rumble mekanis, suara tinggi = kemungkinan gesekan kering",
belum divalidasi dengan data suara kerusakan nyata. Kalau ditanya juri,
jujurkan ini sebagai prototipe struktural (buktikan bahwa arsitekturnya
SUDAH BISA membedakan karakter suara, bukan cuma volume), bukan sebagai
kemampuan diagnostik audio yang sudah tervalidasi.
*/
void DriverAudioDiagnosis_Classify(float bandEnergies[3], float bandBaselineMean[3],
                              float bandBaselineStd[3], char *labelOutput,
                              float *confidenceOutput);