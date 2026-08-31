// CovarianceMatrixSolver.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Covariance Matrix Solver

Menangani operasi matriks yang dibutuhkan sistem deteksi anomali
berbasis Mahalanobis Distance, khususnya perhitungan invers
matriks kovarians (Sigma^-1).

KENAPA MODUL INI DIPISAH SENDIRI:
Operasi invers matriks dibutuhkan di dua tempat berbeda dalam
sistem: saat kalibrasi awal (InitialBaselineCalibrator) dan saat
baseline diperbarui terus-menerus (AdaptiveBaselineLearner).
Daripada logic invers matriks ditulis dua kali di dua file
berbeda, yang berisiko salah satu lupa diperbarui kalau ada
perbaikan bug, logic ini disatukan di sini dan dipanggil oleh
kedua modul tersebut.

METODE YANG DIPAKAI: Gauss-Jordan Elimination
Dipilih karena implementasinya straightforward untuk matriks
berukuran tetap 4x4 (sesuai jumlah fitur sensor: getaran, suara,
arus, suhu), tidak butuh library eksternal berat, dan cukup cepat
dijalankan berulang kali di mikrokontroler ESP32-S3.

CATATAN PENTING SOAL SINGULAR MATRIX:
Kalau salah satu sensor punya variansi mendekati nol selama fase
kalibrasi, misal suhu ruangan yang sangat stabil dan nyaris tidak
berubah sama sekali, matriks kovarians bisa jadi near-singular dan
hasil invers bisa meledak jadi nilai yang tidak masuk akal.
Implementasi harus punya guard atau pengaman untuk kasus ini,
misal pivot minimum threshold, supaya sistem tidak crash atau
menghasilkan D^2 yang salah total.
*/

void solveMatrixInverse4x4(float inputMatrix[3][3], float outputInverse[3][3]);

/*
FUNGSI: computeMahalanobisQuadraticForm
Menghitung nilai D^2 = (x - mu)^T * Sigma^-1 * (x - mu), yaitu
inti rumus Mahalanobis Distance. Dipanggil oleh MahalanobisDetector
setiap siklus deteksi berjalan, memakai mu dan Sigma^-1 terkini
yang disediakan oleh AdaptiveBaselineLearner.
*/

float computeMahalanobisQuadraticForm(float currentFeatures[3], float baselineMean[3], float sigmaInverse[3][3]);
void applyShrinkageRegularization(float covariance[3][3]);
bool checkMatrixWellConditioned(float covariance[3][3]);
