// AudioFFTProcessor.h
#pragma once
#include "SharedTypes.h"

/*

MASALAH YANG DISELESAIKAN:
Sebelum modul ini ada, DriverINM.cpp meringkas 1024 sample mentah mikrofon
jadi SATU angka RMS domain-waktu (rmsAudio), lalu buffer sample aslinya
dibuang. Itu setara mengukur "seberapa keras" suara mesin tanpa peduli
"berisik yang seperti apa" -- dua kondisi suara yang total energinya
kebetulan sama tapi karakter frekuensinya beda total (misal dengungan
motor normal vs decitan bearing kering) akan terlihat identik di mata
sistem. Modul ini menutup gap itu, meniru pola FFTProcessor.cpp yang
sudah ada untuk getaran, tapi dengan parameter sample rate & band yang
sesuai domain suara (16kHz, bukan 500Hz).

KENAPA JADI MODUL/TASK TERPISAH, BUKAN DITARUH DI DALAM TaskDriverINM:
Alasannya persis sama dengan kenapa FFTProcessor dipisah dari
DriverGetaran (lihat DualCoreTaskScheduler.h): TaskDriverINM jalan di
prioritas TERTINGGI (3) khusus supaya buffer I2S DMA tidak overflow --
kalau FFT (komputasi berat) digabung ke task yang sama, risikonya jitter
timing pembacaan I2S, yang berujung distorsi spektral. Jadi pola yang
ditiru: akuisisi (Driver) kirim buffer mentah lewat Queue ke task
komputasi terpisah (Processor), sama seperti jalur getaran.

TIDAK MENGHASILKAN RPM:
Beda dengan FFTProcessor getaran, modul ini TIDAK dipakai untuk estimasi
RPM -- RPM tetap murni dari getaran (RPMEstimator.cpp). Audio di sini
cuma dipakai untuk karakter suara (band energy), lihat
AudioDiagnosisClassifier.h untuk arti tiap band.

KETERBATASAN YANG HARUS DIAKUI KE JURI:
Batas frekuensi tiap band (didefinisikan di AudioFFTProcessor.cpp) adalah
HEURISTIK AWAL, BELUM divalidasi dengan rekaman suara motor rusak
sungguhan. Beda dengan band getaran yang berbasis rumus fisik BPFO/BPFI
dari literatur vibration analysis, band audio ini baru asumsi kasar.
WAJIB validasi empiris (rekam motor normal vs motor dengan kerusakan
diketahui, bandingkan spektrumnya) sebelum diklaim sebagai kemampuan
diagnostik nyata ke juri PIMNAS.
*/
void DriverAudioFFTProcessor_Init();

// Proses satu buffer audio mentah (AUDIO_FFT_SAMPLES sample) jadi
// AUDIO_BAND_COUNT nilai energi band. bandEnergies_out harus punya
// kapasitas minimal AUDIO_BAND_COUNT float.
void DriverAudioFFTProcessor_Process(AudioBuffer *input, float *bandEnergies_out,
                                      float *roughness_out, float *brightness_out);