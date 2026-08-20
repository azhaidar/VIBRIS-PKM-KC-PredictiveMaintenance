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
#define FACTORY_PRESET_MESIN1_READY 1   // diisi dari kalibrasi 20 Agustus 2026 18:07-18:10, 1727 sample, SETELAH fix audio+suhu+LIS3DH (post-kalibrasi: 99,2% Normal)

static float presetMesin1_mean[3]        = {2.262604f, 0.016278f, 0.009049f};
static float presetMesin1_sigmaInv[3][3] = {{1.226146f, -0.516852f, -0.101165f},
                                             {-0.516852f, 1.217867f, 0.043434f},
                                             {-0.101165f, 0.043434f, 1.008347f}};
static float presetMesin1_stdDev[3]      = {0.135123f, 0.002420f, 0.027976f};
static float presetMesin1_bandMean[4]    = {120.793381f, 39.833988f, 5.161519f, 11.128891f};
static float presetMesin1_bandStd[4]     = {464.662079f, 155.336899f, 20.592306f, 42.669979f};
static float presetMesin1_audioMean[AUDIO_BAND_COUNT] = {0.299372f, 0.319766f, 0.015961f};
static float presetMesin1_audioStd[AUDIO_BAND_COUNT]  = {0.126847f, 0.145272f, 0.007153f};

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
#define FACTORY_PRESET_MESIN2_READY 0   // <-- ganti ke 1 SETELAH array di bawah diisi data asli

static float presetMesin2_mean[3]        = {0.0f, 0.0f, 0.0f};
static float presetMesin2_sigmaInv[3][3] = {{1.0f, 0.0f, 0.0f},
                                             {0.0f, 1.0f, 0.0f},
                                             {0.0f, 0.0f, 1.0f}};
static float presetMesin2_stdDev[3]      = {1.0f, 1.0f, 1.0f};
static float presetMesin2_bandMean[4]    = {0.0f, 0.0f, 0.0f, 0.0f};
static float presetMesin2_bandStd[4]     = {1.0f, 1.0f, 1.0f, 1.0f};
static float presetMesin2_audioMean[AUDIO_BAND_COUNT] = {0.0f, 0.0f, 0.0f};
static float presetMesin2_audioStd[AUDIO_BAND_COUNT]  = {1.0f, 1.0f, 1.0f};
