// AdaptiveBaselineLearner.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Adaptive Baseline Learner

Ini modul yang membuat klaim Self-Baseline Learning di judul
proposal benar-benar valid secara teknis, bukan sekadar istilah
marketing.

MASALAH YANG DISELESAIKAN:
Kalau baseline, yaitu mu dan Sigma, hanya dihitung sekali di awal
lalu dipakai selamanya tanpa berubah, sistem akan makin sering
salah deteksi, false alarm, seiring waktu. Alasannya, mesin rotasi
punya perubahan kondisi wajar yang bertahap dan bukan tanda
kerusakan, misalnya karakteristik bearing baru yang sedikit
berbeda dari bearing yang sudah dipakai dua bulan meski keduanya
masih sehat. Sistem dengan baseline statis akan menganggap
perubahan wajar semacam ini sebagai anomali padahal bukan.

METODE YANG DIPAKAI: Exponential Moving Average (EMA)
Setiap siklus deteksi yang hasilnya Normal, nilai mu dan Sigma
diperbarui sedikit demi sedikit mengikuti data terbaru, dengan
bobot kecil (learning rate) supaya perubahan berjalan pelan dan
tidak reaktif terhadap noise sesaat.

PERUBAHAN DARI VERSI SEBELUMNYA -- STD-DEV SEKARANG IKUT ADAPTIF:
Versi lama menstandardisasi fitur pakai featureStdDev STATIS dari
InitialBaselineCalibrator (dihitung sekali di kalibrasi awal 180
detik, lalu beku selamanya), padahal mean & kovarians di modul ini
terus bergerak lewat EMA. Itu inkonsisten: dua komponen yang
seharusnya bergerak bersama (skala vs korelasi antar-fitur) malah
satu beku satu berjalan. Sekarang updateBaselineIfNormal() menerima
fitur RAW (bukan yang sudah distandardisasi dari luar), dan modul
ini sendiri yang men-standardisasi pakai std-dev yang IKUT di-EMA,
lihat getCurrentStdDev(). featureStdDev dari InitialBaselineCalibrator
tetap dipakai, tapi HANYA sebagai nilai awal (seed) di
initializeBaselineLearner(), bukan dipakai terus-menerus di runtime.

ATURAN PALING PENTING DI MODUL INI (TIDAK BERUBAH):
Baseline hanya diperbarui kalau status deteksi saat itu adalah
Normal. Kalau sistem sedang mendeteksi kondisi Waspada atau
Bahaya, update baseline tidak dilakukan. Ini untuk mencegah
skenario di mana kerusakan yang berkembang bertahap justru
dipelajari sistem sebagai kondisi normal baru, yang akan membuat
sistem makin tidak sensitif terhadap masalah yang sebenarnya
sedang terjadi. Guard ini yang membuat mekanisme self-baseline
aman dipakai, bukan cuma mengikuti data secara membabi buta.

CATATAN PERFORMA:
Invers matriks Sigma tidak dihitung ulang di setiap siklus update
karena operasi ini tergolong berat secara komputasi. Invers hanya
dihitung ulang secara berkala, misalnya tiap beberapa puluh
sample, sementara mu dan Sigma mentah tetap diperbarui setiap
siklus.
*/
bool isBaselineLearnerReady();

// UBAH: tambah parameter initialStd[4] -- nilai awal std-dev per fitur
// (dari getFeatureStdDev() di InitialBaselineCalibrator), dipakai sebagai
// seed EMA, bukan dipakai statis selamanya.
void initializeBaselineLearner(float initialMean[3], float initialStd[3], float initialSigmaInverse[3][3]);

// UBAH: parameter sekarang fitur RAW (belum distandardisasi), bukan
// currentFeaturesStd seperti versi lama -- standardisasi terjadi DI DALAM
// fungsi ini memakai std-dev adaptif terkini.
void updateBaselineIfNormal(float currentRawFeatures[3], bool isCurrentStatusNormal);

void getCurrentBaseline(float meanOutput[3], float sigmaInverseOutput[3][3]);

// BARU: getter std-dev adaptif, dipanggil MahalanobisDetector.cpp untuk
// menstandardisasi fitur real-time -- gantikan getFeatureStdDev() statis.
void getCurrentStdDev(float stdOutput[3]);

// BARU: reset paksa learner ke status belum-siap. Dipanggil saat pindah ke
// slot motor yang BELUM punya baseline tersimpan -- mencegah device pakai
// baseline motor SEBELUMNYA untuk motor BARU yang karakternya beda.
void resetBaselineLearner();