// InitialBaselineCalibrator.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Initial Baseline Calibrator

Menangani proses kalibrasi awal sistem, yaitu fase pengambilan
data kondisi normal mesin selama kurang lebih 60 detik pertama
setelah device dinyalakan, sesuai prosedur pengujian yang tertulis
di proposal BAB 3.3.

TUJUAN FASE INI:
Membentuk profil normal awal mesin, yaitu mean dan matriks
kovarians dari empat fitur sensor (RMS getaran, RMS suara, arus,
suhu), tanpa memerlukan dataset berlabel dari luar. Profil inilah
yang jadi acuan pertama sebelum sistem masuk mode deteksi
real-time.

BEDA DENGAN AdaptiveBaselineLearner:
Modul ini hanya berjalan sekali di awal, one-time calibration,
menghasilkan nilai mu dan Sigma pertama. Setelah itu, tugas
memperbarui baseline secara berkelanjutan diambil alih oleh
AdaptiveBaselineLearner. Pemisahan ini disengaja supaya proses
kalibrasi awal, yang harus dilakukan dengan hati-hati memastikan
mesin benar-benar dalam kondisi normal saat direkam, tidak
tercampur dengan logic pembaruan berkelanjutan yang berjalan
otomatis di background selama device beroperasi.

PENYIMPANAN HASIL KALIBRASI:
Hasil kalibrasi, yaitu mu dan Sigma^-1, disimpan ke flash memory
(Preferences/NVS ESP32) supaya tidak hilang kalau device restart
atau baterai diganti. Tanpa penyimpanan ini, device harus
mengulang kalibrasi 60 detik setiap kali dinyalakan ulang, yang
tidak praktis untuk penggunaan sehari-hari oleh UMKM.
*/

void startCalibrationPhase();
bool addCalibrationSample(SensorFeatures sample);
void computeInitialBaseline(float meanOutput[3], float sigmaInverseOutput[3][3]);
void getFeatureStdDev(float stdDevOutput[3]);
void setFeatureStdDev(float stdDev[3]);
void saveBaselineToFlash(int slot, float mean[3], float sigmaInverse[3][3], float stdDev[3], int regime = 0);
bool loadBaselineFromFlash(int slot, float meanOutput[3], float sigmaInverseOutput[3][3], float stdDevOutput[3], int regime = 0);

// TAMBAHKAN 4 baris ini (dekat prototype band getaran yang udah ada):
bool addAudioBandEnergyCalibrationSample(float audioBandEnergies[AUDIO_BAND_COUNT]);
void computeAudioBandBaseline(float meanOutput[AUDIO_BAND_COUNT], float stdOutput[AUDIO_BAND_COUNT]);
void saveAudioBandBaselineToFlash(int slot, float mean[AUDIO_BAND_COUNT], float std[AUDIO_BAND_COUNT], int regime = 0);
bool loadAudioBandBaselineFromFlash(int slot, float meanOutput[AUDIO_BAND_COUNT], float stdOutput[AUDIO_BAND_COUNT], int regime = 0);

bool isLastCalibrationValid();
bool addBandEnergyCalibrationSample(float bandEnergies[4]);
void computeBandEnergyBaseline(float meanOutput[4], float stdOutput[4]);
void saveBandBaselineToFlash(int slot, float mean[4], float std[4], int regime = 0);
bool loadBandBaselineFromFlash(int slot, float meanOutput[4], float stdOutput[4], int regime = 0);

// BARU (20 Agustus 2026): parameter "regime" -- ID kondisi operasi (0-9)
// DALAM 1 mesin yang sama (misal: 0=pulley kecil, 1=pulley besar, 2=tanpa
// beban). Ini SUMBU TERPISAH dari "slot" (slot = identitas mesin). Default
// 0 supaya semua kode lama yang manggil fungsi-fungsi ini tanpa parameter
// regime tetap kekompile & jalan SAMA PERSIS seperti sebelumnya --
// slot X regime 0 itu namespace flash yang SAMA dengan "baseline lama"
// sebelum perubahan ini ada, jadi baseline yang udah kalian kalibrasi &
// flash sebelumnya TIDAK HILANG.
void deleteBaselineFromFlash(int slot, int regime = 0);