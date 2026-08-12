// DiagnosisClassifier.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Diagnosis Classifier

Mengubah data energi per pita frekuensi menjadi label jenis
kerusakan spesifik yang bisa langsung dibaca pengguna non-teknis,
bukan sekadar status anomali generik.

MASALAH YANG DISELESAIKAN:
Kompetitor di tabel perbandingan proposal (Fluke 810, SKF CMVL
3600-IS) pakai threshold tetap berbasis standar ISO 10816. Device
mereka cuma bisa bilang ada anomali atau tidak ada anomali,
operator ahli yang harus interpretasi manual jenis kerusakannya.
Device ini ditargetkan untuk UMKM yang tidak punya kemampuan
interpretasi itu. Kalau device ini cuma memberi satu lampu alarm
generik tanpa bilang jenis kerusakannya, itu tidak beda fungsional
dari kompetitor, cuma beda harga, bukan beda kemampuan.

CARA KERJA:
Setelah RPM diketahui dari RPMEstimator, spektrum FFT dibagi jadi
beberapa pita frekuensi (band) yang masing-masing punya arti fisik
spesifik: band di sekitar 1x RPM (rentang kira-kira 0.9x sampai
1.1x dari RPM) mengindikasikan ketidakseimbangan massa (unbalance);
band di sekitar 2x RPM mengindikasikan ketidaksejajaran poros
(misalignment); band di sekitar frekuensi BPFO mengindikasikan
kerusakan bearing bagian outer race; band di sekitar frekuensi
BPFI mengindikasikan kerusakan bearing bagian inner race.

Untuk tiap band, dihitung total energi (jumlah kuadrat amplitudo
semua bin dalam rentang band itu). Energi tiap band dibandingkan
dengan baseline energi band yang sama dari fase kalibrasi normal,
dinormalisasi jadi Z-score per band (logika sama seperti Z-score
biasa, tapi diterapkan per band frekuensi, bukan per sensor
mentah). Band dengan Z-score tertinggi dijadikan output label
diagnosis.

HUBUNGAN DENGAN MAHALANOBIS DETECTOR:
MahalanobisDetector menjawab pertanyaan apakah kondisi mesin
sekarang menyimpang dari normal secara keseluruhan. Modul ini
menjawab pertanyaan lanjutannya: kalau menyimpang, menyimpang di
bagian mana secara spesifik. Dua modul ini saling melengkapi,
bukan saling menggantikan.

KETERBATASAN YANG HARUS DIAKUI KE JURI:
Klasifikasi ini berbasis satu band dominan saja (band dengan
penyimpangan tertinggi). Kerusakan nyata kadang muncul di lebih
dari satu band sekaligus, misal unbalance parah bisa memicu
harmonik yang tumpang tindih dengan band misalignment. Modul ini
tidak melakukan klasifikasi multi-label atau machine learning
sungguhan, ini rule-based sederhana berdasarkan band mana yang
paling menyimpang. Kalau ditanya juri, jujurkan ini sebagai
pendekatan heuristik awal, bukan diklaim sebagai AI diagnostik
penuh seperti yang diklaim Fluke 810 di tabel perbandingan kalian
sendiri.
*/

void Diagnosis_Classify(float bandEnergies[4], float bandBaselineMean[4],
                          float bandBaselineStd[4], char *labelOutput,
                          float *confidenceOutput);
