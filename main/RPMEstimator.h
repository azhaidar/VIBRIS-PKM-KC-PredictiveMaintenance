// RPMEstimator.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: RPM Estimator

Mengestimasi RPM motor tanpa sensor tachometer fisik, murni dari
pola spektrum getaran hasil FFT.

MASALAH YANG DISELESAIKAN:
Rumus BPFO/BPFI di proposal BAB 2.3 butuh nilai fr (frekuensi
putar motor). Device ini tidak punya sensor RPM fisik, dan target
pengguna (UMKM non-teknis) tidak realistis diminta input manual
RPM motor mereka sendiri. Tanpa modul ini, seluruh perhitungan
bearing fault detection yang dijanjikan proposal tidak punya dasar
sama sekali — ini prasyarat, bukan pelengkap.

PRINSIP FISIKA YANG DIPAKAI:
Setiap motor yang berputar, seideal apapun kualitas manufakturnya,
selalu memiliki sedikit ketidakseimbangan massa rotor yang tidak
bisa dihindari sepenuhnya. Ketidakseimbangan ini menghasilkan gaya
sentrifugal periodik yang muncul sebagai komponen getaran dominan
tepat di frekuensi putar motor itu sendiri, dikenal sebagai
komponen 1x RPM. Ini prinsip dasar yang sudah mapan di literatur
vibration analysis, bukan asumsi yang dibuat sendiri untuk proyek
ini.

CARA KERJA:
Setelah sinyal getaran diubah ke domain frekuensi lewat FFT,
hasilnya berupa deretan nilai amplitudo di tiap titik frekuensi
(bin). Modul ini menyisir bin-bin itu hanya di rentang frekuensi
yang masuk akal untuk motor kecil target proposal, yaitu 5 Hz
sampai 50 Hz, setara 300 sampai 3000 RPM sesuai spesifikasi di
BAB 2.1. Di rentang itu dicari bin dengan amplitudo tertinggi.
Berdasarkan prinsip fisika di atas, bin dengan amplitudo tertinggi
kemungkinan besar adalah frekuensi putar motor sebenarnya. Nilai
frekuensi itu dikonversi ke RPM dengan dikalikan 60.

KENAPA DIBATASI KE RENTANG 5-50 HZ:
Kalau tidak dibatasi, modul bisa salah tangkap noise frekuensi
rendah (getaran lingkungan, meja goyang) atau harmonik tinggi lain
yang bukan representasi RPM asli motor. Pembatasan ini membuat
hasil lebih reliable.

KETERBATASAN YANG HARUS DIAKUI:
Kalau ketidakseimbangan motor sangat kecil (motor hampir sempurna
balanced), puncak 1x RPM bisa lemah dan tertutup noise sensor
lain, sehingga estimasi bisa meleset. Mitigasi yang disarankan:
cross-check dengan pola ripple sinyal arus dari sensor SCT, karena
motor AC/DC juga punya ripple arus yang berkorelasi dengan RPM.
Kalau dua estimasi independen ini konvergen, confidence naik.

VALIDASI WAJIB SEBELUM DIKLAIM KE JURI:
Bandingkan hasil fungsi ini dengan tachometer optik asli (pinjam
lab elektro), uji di 3-5 kondisi RPM berbeda, hitung persentase
error rata-rata. Tanpa data pembanding ini, klaim auto-detect RPM
cuma teori di atas kertas dan akan langsung dipatahkan kalau juri
PIMNAS tanya angka akurasinya secara spesifik.
*/

float RPM_Estimate(double *magnitude, int n, float sampleRate);
/*
FUNGSI: RPM_ComputeBPFO / RPM_ComputeBPFI
Menghitung frekuensi karakteristik kerusakan bearing di bagian
outer race (BPFO) dan inner race (BPFI), memakai rumus yang sudah
tertulis di proposal BAB 2.3, tapi sekarang fr-nya diisi otomatis
dari hasil RPM_Estimate, bukan input manual operator.

Parameter yang dibutuhkan dari spesifikasi bearing (jumlah bola,
diameter bola, diameter pitch, contact angle) tetap harus diketahui
dari datasheet bearing motor uji kalian — ini bukan sesuatu yang
bisa diestimasi otomatis, harus dicatat manual saat pengujian.
*/
float RPM_ComputeBPFO(float fr_hz, int n_balls, float d_ball, float D_pitch, float phi_deg);
float RPM_ComputeBPFI(float fr_hz, int n_balls, float d_ball, float D_pitch, float phi_deg);
// BSF — Ball Spin Frequency: cacat pada bola bearing
// Sumber: paper IJERT 2013 (ball-bearing-fault-detection) persamaan (5)
float RPM_ComputeBSF(float fr_hz, int n_balls, float d_ball,
                     float D_pitch, float phi_deg);

// FTF — Fundamental Train Frequency: cacat pada sangkar/cage bearing
// Sumber: paper IJERT 2013 persamaan (3)
float RPM_ComputeFTF(float fr_hz, float d_ball,
                     float D_pitch, float phi_deg);
bool RPM_IsSignalReliable(double *magnitude, int n, float sampleRate, float *snrOut);
void resetSNRCalibration();
void addSNRCalibrationSample(float snr);
float computeSNRThresholdFromCalibration();
void setRuntimeSNRThreshold(float threshold);