// FactoryPresets.h
#pragma once
#include "SharedTypes.h"
#include "config.h"

/*
MODUL: Factory Presets (Baseline Preset Pabrikan)

TUJUAN:
Supaya Mesin 1 / Mesin 2 bisa langsung dipakai (Normal/Waspada/Bahaya
kebaca dari detik pertama) TANPA operator UMKM harus kalibrasi manual
180 detik -- ide ini datang dari kekhawatiran "UMKM gak akan bisa
ngelakuin kalibrasi sendiri karena kerumitannya".

CARA ISI FILE INI (WAJIB DIIKUTI URUTANNYA):
1. Kalibrasi mesin secara BERSIH -- artinya motor sudah dijalankan dan
   RPM/suhunya sudah STABIL (gak naik-turun lagi) SEBELUM kamu pencet
   mulai Kalibrasi. Kalibrasi yang dilakukan saat motor belum stabil
   akan menghasilkan baseline rusak (lihat bukti di kondisiNormal 18
   Agustus 2026 -- D2 median 13.35 padahal harusnya Normal).
2. Begitu Serial Monitor nampilin "[SYSTEM] Kalibrasi VALID", cari 1
   blok tepat di bawahnya yang diawali "[EXPORT PRESET]" -- isinya kode
   C++ siap-copas (lihat fungsi printFactoryPresetExport() di main.ino).
3. Copas blok itu, GANTI nama variabel "preset_..." jadi
   "presetMesin1_..." (atau "presetMesin2_...", sesuai mesin yang barusan
   dikalibrasi), TIMPA array placeholder di bawah ini.
4. Ganti flag FACTORY_PRESET_MESINx_READY dari 0 jadi 1 -- HANYA setelah
   array-nya beneran diisi data asli, BUKAN sebelum itu.

PERINGATAN PENTING -- JANGAN DILANGGAR:
Jangan pernah ganti flag *_READY jadi 1 kalau array di bawahnya masih
placeholder (nol semua / identitas). Kalau itu terjadi, alat bakal
"pura-pura" langsung siap deteksi padahal baseline-nya kosong/ngawur --
diam-diam gagal mendeteksi apapun, tanpa ada tanda kesalahan yang
kelihatan. Lebih aman biarin flag di 0 (device jatuh ke kalibrasi manual
seperti biasa, sama seperti sebelum fitur ini ada) daripada preset yang
belum tervalidasi dipaksa aktif.

VALIDASI SEBELUM DIPERCAYA PENUH:
Setelah preset ini diisi & di-flash, uji dengan mesin KEDUA yang identik
(merek/tipe/torsi sama) tapi BEDA UNIT FISIK dari yang dipakai kalibrasi
tadi: mesin sehat harus tetap kebaca Normal, mesin yang sengaja dirusak
harus kebaca Waspada/Bahaya. Kalau salah satu gagal, presetnya BELUM
valid dipakai lintas unit -- lihat diskusi soal ini sebelum file ini
dibuat.
*/

// ============================================================
// MESIN 1 (slot 0, regime 0 / default)
// ============================================================
#define FACTORY_PRESET_MESIN1_READY 1   // <-- DIPERBARUI 22 Agustus 2026 pagi (06:43:39). Lihat catatan di bawah.

// UPDATE (22 Agustus 2026, 06:43:39) -- preset SEBELUMNYA di slot ini (05:59:36,
// mean 2.348) BELUM SEMPAT dites -- device belum pernah di-flash ulang sejak
// preset itu ditulis, jadi nggak pernah benar-benar dicoba. Preset di bawah
// ini dari kalibrasi live TERBARU (06:40:39-06:43:39, log baris 5331-6219,
// `vibris_20260822_0640_kondisiNormal.csv`), sinyalnya PALING BAGUS dari
// semua kalibrasi pagi ini:
//   - RPM stabil SANGAT sempit 1480-1490 RPM sepanjang kalibrasi (nameplate
//     motor 1400 RPM).
//   - SNR rata-rata 8.72, minimum 4.83 (ambang cuma 3.0) -- margin paling
//     lebar dari semua kalibrasi pagi ini (lawan: 05:59:36 cuma rata-rata 7.5).
//   - Self-test 4 menit sesudahnya: 2 lonjakan D2 sempat kelihatan (779 di
//     detik pertama, dan turun bertahap 116->17 tepat pas kalibrasi baru
//     selesai) -- KEDUANYA transien 1 baris pas transisi mode, bukan tren
//     berkelanjutan (lihat MahalanobisDetector.cpp, debounce butuh beberapa
//     siklus baru mengunci status). Diagnosis fault flicker ~8.5% baris,
//     tersebar RATA tiap 5 detik sepanjang sesi (bukan menumpuk di satu
//     titik) -- pola noise biasa, bukan tren memburuk.
//
// CATATAN JUJUR PENTING (WAJIB dibaca sebelum percaya penuh ke preset ini):
// 1. INI JUGA baru lolos TES INTERNAL (dites pakai data dari sesi kalibrasi
//    yang sama, langsung sesudahnya) -- BELUM dites independen. Preset
//    22:41:59 (21 Agustus malam) kelihatan bersih di tes pertama, lalu gagal
//    total (100% Bahaya) 18 menit kemudian di sesi terpisah. Preset ini
//    punya risiko sama sampai device di-flash ulang dan dites TANPA
//    kalibrasi ulang di sesi terpisah.
// 2. Suhu Motor 1 TERUS NAIK sejak jam 05:44 tanpa melandai penuh (~29C di
//    awal -> ~53C jam 06:44, sekitar +1.1C/menit di jendela pendek) --
//    motor kemungkinan belum mencapai kestabilan termal penuh walau sudah
//    jalan ~1 jam nonstop. Fitur "laju-suhu" di baseline ini merekam
//    kondisi motor yang masih (sedikit) memanas. Masalah lama, belum
//    dibenerin di kode (lihat catatan §3.5).
static float presetMesin1_mean[3]        = {2.267076f, 0.032966f, 0.033400f};
static float presetMesin1_sigmaInv[3][3] = {{1.050181f, 0.031905f, -0.229077f},
                                             {0.031905f, 1.003936f, -0.061511f},
                                             {-0.229077f, -0.061511f, 1.052936f}};
static float presetMesin1_stdDev[3]      = {0.116037f, 0.002935f, 0.069400f};
static float presetMesin1_bandMean[4]    = {316.654602f, 157.565414f, 146.293381f, 20.519840f};
static float presetMesin1_bandStd[4]     = {1075.397583f, 535.652161f, 492.206696f, 73.218971f};
static float presetMesin1_audioMean[AUDIO_BAND_COUNT] = {0.413417f, 0.843636f, 0.069801f};
static float presetMesin1_audioStd[AUDIO_BAND_COUNT]  = {0.111821f, 0.128675f, 0.021493f};

// ============================================================
// MESIN 1 (slot 0, regime 1 / pulley kecil)
// ============================================================
// CATATAN JUJUR: preset ini dari SATU kali kalibrasi (20 Agustus 2026
// 19:00-19:03, 1727 sample), monitoring sesudahnya 97,5% Normal -- bagus,
// tapi belum lolos VALIDASI SEBELUM DIPERCAYA PENUH di atas (belum diuji
// di unit fisik KEDUA, dan belum diuji dengan mesin yang sengaja dirusak
// buat mastiin Waspada/Bahaya juga kebaca benar). Aman dipakai buat
// demo/uji PIMNAS, tapi kalau nanti dipakai di lapangan beneran, ulangi
// langkah VALIDASI itu dulu.
#define FACTORY_PRESET_MESIN1_REGIME1_READY 1   // diisi dari kalibrasi 20 Agustus 2026 19:00-19:03, 1727 sample, pulley kecil (post-kalibrasi: 97,5% Normal)

static float presetMesin1Regime1_mean[3]        = {4.219714f, 0.028647f, 0.013099f};
static float presetMesin1Regime1_sigmaInv[3][3] = {{1.066520f, 0.244843f, 0.090874f},
                                                    {0.244843f, 1.060012f, -0.040923f},
                                                    {0.090874f, -0.040923f, 1.011546f}};
static float presetMesin1Regime1_stdDev[3]      = {0.411763f, 0.004591f, 0.026817f};
static float presetMesin1Regime1_bandMean[4]    = {8.391292f, 0.825153f, 12.429925f, 52.176273f};
static float presetMesin1Regime1_bandStd[4]     = {82.924782f, 7.864876f, 116.685226f, 488.036621f};
static float presetMesin1Regime1_audioMean[AUDIO_BAND_COUNT] = {0.174290f, 0.158604f, 0.011322f};
static float presetMesin1Regime1_audioStd[AUDIO_BAND_COUNT]  = {0.107105f, 0.272416f, 0.023136f};

// ============================================================
// MESIN 1 (slot 0, regime 2 / pulley besar)
// ============================================================
// CATATAN JUJUR: preset ini dari SATU kali kalibrasi (20 Agustus 2026
// 20:02-20:05, 1814 sample). Ada masa "settling" ~46 detik TEPAT setelah
// kalibrasi selesai (20:05:48-20:06:34) di mana status sempat Waspada/
// Bahaya (97 dari 1153 baris) sebelum akhirnya stabil Normal 100% untuk
// sisa sesi (705 dari 705 baris terakhir). Kemungkinan besar ini motor/
// pulley besar butuh sedikit waktu menyetel diri secara mekanis pasca
// kalibrasi, BUKAN baseline yang rusak -- tapi ini DUGAAN, belum
// dikonfirmasi akar penyebabnya. Sama seperti regime 1, preset ini juga
// BELUM lolos VALIDASI SEBELUM DIPERCAYA PENUH di atas (belum diuji di
// unit fisik kedua / mesin yang sengaja dirusak). Aman untuk demo PIMNAS.
#define FACTORY_PRESET_MESIN1_REGIME2_READY 1   // diisi dari kalibrasi 20 Agustus 2026 20:02-20:05, 1814 sample, pulley besar (post-kalibrasi, di luar 46 detik settling awal: 100% Normal)

static float presetMesin1Regime2_mean[3]        = {4.664860f, 0.021957f, 0.016829f};
static float presetMesin1Regime2_sigmaInv[3][3] = {{1.068762f, -0.271341f, -0.006132f},
                                                    {-0.271341f, 1.071600f, 0.053699f},
                                                    {-0.006132f, 0.053699f, 1.002747f}};
static float presetMesin1Regime2_stdDev[3]      = {0.952705f, 0.004490f, 0.030249f};
static float presetMesin1Regime2_bandMean[4]    = {39.173122f, 48.802654f, 72.242104f, 293.449646f};
static float presetMesin1Regime2_bandStd[4]     = {158.217804f, 186.294876f, 288.472351f, 1032.014771f};
static float presetMesin1Regime2_audioMean[AUDIO_BAND_COUNT] = {0.447654f, 0.582625f, 0.047843f};
static float presetMesin1Regime2_audioStd[AUDIO_BAND_COUNT]  = {0.136804f, 0.214606f, 0.032128f};

// ============================================================
// MESIN 2 (slot 1, regime 0 / default)
// ============================================================
#define FACTORY_PRESET_MESIN2_READY 1   // <-- SEKARANG 1. Lihat catatan di bawah soal sumber
                                        // tiap array -- 2 sesi kalibrasi berbeda digabung
                                        // dengan sengaja, bukan asal comot.

// UPDATE (21 Agustus 2026, sesi 17:21) -- presetMesin2_mean/sigmaInv/stdDev
// dan presetMesin2_audioMean/audioStd diisi dari EXPORT PRESET live slot #1
// regime #0 (log vibris_system_log.txt baris 4884-4891, kalibrasi 180 detik
// VALID jam 17:21:46, motor Shimizu/Motor 2 fisik dikonfirmasi jalan).
// 3 fitur Mahalanobis (getaran/suara/laju-suhu) dan audio band TIDAK
// bergantung ke pengaturan klaster bearing sama sekali -- jadi aman dipakai
// dari sesi ini walau saat itu belum 100% terverifikasi klaster bearingnya
// ada di posisi mana (tidak ada baris "Klaster bearing diganti ke..." di
// log dekat jam 17:18, beda dari sesi sebelumnya yang jelas tercatat).
//
// presetMesin2_bandMean/bandStd (4 angka) SENGAJA TIDAK diambil dari sesi
// 17:21 yang sama -- ini SATU-SATUNYA bagian yang hasilnya beda tergantung
// klaster bearing mana yang aktif (band energy dihitung di jendela frekuensi
// oneX/twoX/bpfo/bpfi milik currentBearingSpec). Karena sesi 17:21 gak
// punya bukti log soal klaster, dipertahankan angka dari sesi SEBELUMNYA
// (vibris_20260821_1631_kondisiNormal.csv, 1768 sample "Calibrating", yang
// TERBUKTI di log jam 16:32:30 klasternya Klaster 2/6201) -- rumus
// dihitung ulang PERSIS sama seperti computeBandEnergyBaseline() di
// InitialBaselineCalibrator.cpp (mean/std sampel, pembagi n-1), dan sudah
// diverifikasi firmware sendiri ngumpulin band energy di SETIAP siklus
// loop() tanpa gerbang "fresh FFT" (main.ino baris ~338-340), jadi angka
// dari CSV itu PERSIS sama dengan yang bakal masuk buffer kalibrasi asli.
//
// CATATAN JUJUR buat laporan: preset ini campuran 2 sesi kalibrasi berbeda
// (bukan 1 sesi tunggal yang bersih dari awal sampai akhir) -- keputusan
// ini diambil karena masing-masing bagian yang dipakai SECARA INDEPENDEN
// sudah diverifikasi benar dari sumbernya, bukan karena kekurangan data
// dipaksa dianggap lengkap.
static float presetMesin2_mean[3]        = {5.446872f, 0.078705f, -0.010003f};
static float presetMesin2_sigmaInv[3][3] = {{1.036844f, -0.012422f, -0.191894f},
                                             {-0.012422f, 1.070715f, -0.272558f},
                                             {-0.191894f, -0.272558f, 1.106081f}};
static float presetMesin2_stdDev[3]      = {0.265181f, 0.011652f, 0.045484f};
static float presetMesin2_bandMean[4]    = {1713.467486f, 345.867762f, 47.226572f, 1295.289726f};
static float presetMesin2_bandStd[4]     = {5858.187269f, 1210.119453f, 164.965000f, 4514.493254f};
static float presetMesin2_audioMean[AUDIO_BAND_COUNT] = {0.359101f, 0.177897f, 0.017658f};
static float presetMesin2_audioStd[AUDIO_BAND_COUNT]  = {0.996105f, 0.454415f, 0.046166f};
