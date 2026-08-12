// MultiSensorFeatureMerger.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Multi-Sensor Feature Merger

Menggabungkan hasil dari empat Driver sensor (DriverArus,
DriverGetaran, DriverINMP, DriverSuhu) menjadi satu vektor fitur
terpadu, sebelum data ini dipakai oleh MahalanobisDetector maupun
DiagnosisClassifier.

KENAPA MODUL INI DIBUTUHKAN:
Keempat Driver berjalan sebagai task terpisah dengan kecepatan
pembacaan yang berbeda-beda. Getaran dibaca ribuan kali per
detik, sementara suhu dari sensor DS18B20 secara fisik hanya bisa
dibaca sekitar satu kali per detik karena keterbatasan sensornya
sendiri. Tanpa modul penggabung yang jelas, logic untuk menyatukan
keempat data ini akan ditulis dadakan di tempat lain, misalnya di
dalam main.ino, yang berisiko menggabungkan data dari waktu yang
tidak sinkron, misalnya data suhu yang sudah basi digabung dengan
data getaran yang baru saja terbaca.

MEKANISME PENANGANAN DATA BASI:
Setiap kali salah satu Driver memperbarui nilainya, modul ini
mencatat waktu pembaruan terakhir untuk sensor tersebut. Saat
fitur gabungan diminta oleh modul lain, sistem memeriksa apakah
semua empat sensor sudah diperbarui dalam rentang waktu wajar,
misal dua detik terakhir. Kalau ada satu sensor yang datanya
sudah lewat dari batas waktu itu, hasil gabungan ditandai tidak
valid, sehingga tidak dipakai untuk perhitungan Mahalanobis
Distance yang bisa menyesatkan.

KEAMANAN AKSES DATA LINTAS TASK:
Karena keempat Driver berjalan sebagai task FreeRTOS yang terpisah
dan bisa saja berjalan di core yang berbeda, akses ke data
gabungan ini dilindungi menggunakan mutex, supaya tidak terjadi
kondisi di mana satu task sedang menulis data sementara task lain
sedang membacanya secara bersamaan, atau yang biasa disebut race
condition.
*/

void updateVibrationFeature(float rmsValue);
void updateAudioFeature(float rmsValue);
void updateCurrentFeature(float rmsValue);
void updateTemperatureFeature(float value);
bool getMergedFeatures(SensorFeatures *output);
