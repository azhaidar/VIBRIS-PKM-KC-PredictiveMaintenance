// RaspberryPiDataTransmitter.h
#pragma once
#include "SharedTypes.h"

/*
Mengirim hasil deteksi (fitur sensor, RPM, D², status, diagnosis)
dari ESP32-S3 ke Raspberry Pi 3B via UART (bukan WiFi/I2C), karena
kedua device menyatu dalam satu unit fisik sesuai desain proposal
BAB 3.2 — WiFi jadi overkill dan boros baterai Li-ion, I2C tidak
ideal untuk kirim struct data besar.

Format data: JSON string per baris, dikirim lewat Serial1 (UART
hardware kedua ESP32-S3), terpisah dari Serial (USB) yang dipakai
untuk debug — supaya data debug developer tidak tercampur dengan
data yang diparse Raspberry Pi.

Modul ini juga penyedia "kontrak data" untuk Thufail (dashboard):
begitu format JSON di sini disepakati, dia bisa mulai coding parser
dan UI di Raspberry Pi memakai dummy data generator, tanpa menunggu
ESP32 selesai 100%.
*/
void Transmitter_Init(long baudRate);
void Transmitter_SendResult(SensorFeatures features, DetectionResult result, const char* groundTruthLabel);
void Transmitter_SendSessionSummary(CheckSessionSummary s);
