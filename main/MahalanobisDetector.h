// MahalanobisDetector.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Mahalanobis Detector

Modul inti pengambilan keputusan status kondisi mesin, Normal,
Waspada, atau Bahaya, menggantikan pendekatan Z-score per sensor
yang lebih sederhana dengan pendekatan multivariat.

KENAPA MAHALANOBIS DISTANCE, BUKAN Z-SCORE PER SENSOR:
Pendekatan Z-score per sensor menghitung anomali tiap sensor
secara terpisah dan independen satu sama lain. Pendekatan ini
punya kelemahan mendasar, yaitu dua atau lebih sensor bisa saja
masing-masing masih dalam batas Normal kalau dilihat sendiri-
sendiri, padahal kombinasi perubahan kecil di beberapa sensor
sekaligus, misalnya getaran naik sedikit bersamaan dengan arus
naik sedikit, adalah pola dini yang lebih bermakna dibanding
perubahan satu sensor saja. Threshold tetap berbasis standar ISO
10816 yang dipakai kompetitor SKF CMVL 3600-IS, lihat Tabel 2.1
proposal, punya kelemahan yang sama persis.

Mahalanobis Distance mengatasi ini dengan memperhitungkan struktur
korelasi antar sensor, yaitu matriks kovarians Sigma, yang
dipelajari langsung dari data kondisi normal mesin itu sendiri,
bukan dari standar generik. Secara geometris, pendekatan Z-score
per sensor membentuk area threshold berbentuk kotak di ruang empat
dimensi, satu sumbu per sensor, saling independen, sementara
Mahalanobis membentuk area threshold berbentuk elips yang mengikuti
pola korelasi alami data normal, sehingga lebih sensitif terhadap
anomali yang muncul sebagai kombinasi perubahan di banyak sensor
sekaligus.

RUMUS INTI:
D^2 = (x - mu)^T * Sigma^-1 * (x - mu)
dengan x adalah vektor empat fitur sensor saat ini, mu adalah
rata-rata baseline, dan Sigma^-1 adalah invers matriks kovarians
baseline, keduanya disediakan oleh AdaptiveBaselineLearner.

AMBANG KLASIFIKASI:
Nilai D^2 dibandingkan dengan nilai kritis distribusi chi-square
dengan derajat bebas 4, sesuai jumlah fitur sensor, yaitu 9.49
untuk tingkat kepercayaan 95 persen dan 13.28 untuk 99 persen.
Nilai di bawah 9.49 diklasifikasikan Normal, antara 9.49 dan 13.28
diklasifikasikan Waspada, dan di atas 13.28 diklasifikasikan
Bahaya. Nilai kritis ini adalah nilai statistik standar, bukan
angka yang ditentukan sendiri secara sembarangan.

HUBUNGAN DENGAN MODUL LAIN:
Modul ini menjawab pertanyaan apakah kondisi mesin sekarang
menyimpang dari normal secara keseluruhan. Untuk mengetahui di
bagian mana penyimpangan itu terjadi secara spesifik, misalnya
indikasi kerusakan bearing atau unbalance, status dari modul ini
diteruskan ke DiagnosisClassifier yang bekerja di level band
frekuensi getaran.
*/

DetectionResult runDetectionCycle();
const char* classifyStatusFromD2(float d2Value);
void setDiagnosisBandBaseline(float bandMean[4], float bandStd[4]);
void setAudioBandBaseline(float mean[AUDIO_BAND_COUNT], float std[AUDIO_BAND_COUNT]);
float getChiSquare99();
