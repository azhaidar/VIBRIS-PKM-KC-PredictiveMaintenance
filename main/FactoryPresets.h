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
#define FACTORY_PRESET_MESIN1_READY 1   // <-- DIPERBARUI 21 Agustus 2026. Lihat catatan di bawah.

// UPDATE (21 Agustus 2026, sesi 21:31-21:34) -- preset LAMA (20 Agustus,
// mean getaran 2.26) TERBUKTI SUDAH GAK COCOK lagi sama Motor 1 fisik
// hari ini: dites langsung (`vibris_20260821_2055_kondisiNormal.csv`,
// ground_truth NORMAL murni), getaran asli motor jalan di kisaran 4,6-7,2,
// jauh di atas 2.26 -- D2 langsung melonjak ke 40-70 (ambang Bahaya cuma
// 11.345) padahal motor SEHAT, gak ada fault. Kemungkinan besar preset
// lama itu direkam dalam kondisi mounting/beban yang beda dari sekarang.
//
// Kalibrasi ulang PERTAMA (21:18:35) GAGAL ("varians terlalu rendah") --
// BUKAN karena motor mati (rms_v waktu itu 6,6-7,3, jelas jalan), tapi
// karena motor jalan TERLALU stabil/konsisten buat lolos ambang
// MIN_ACCEPTABLE_VARIANCE di InitialBaselineCalibrator.cpp. Percobaan
// KEDUA (kalibrasi ulang 21:31:28, VALID jam 21:34:27, log baris
// 19801-20689) akhirnya lolos. Diverifikasi post-kalibrasi (60 detik
// sisa sesi `vibris_20260821_2130_kondisiNormal.csv`): 565 Normal, 10
// Diam, NOL Waspada/Bahaya -- baseline baru ini representasi Motor 1
// yang akurat untuk kondisi fisik SEKARANG.
//
// CATATAN JUJUR: preset ini baru 1x kalibrasi bersih, BELUM lolos
// "VALIDASI SEBELUM DIPERCAYA PENUH" (lihat catatan di atas file ini) --
// belum diuji ke unit fisik Motor 1 KEDUA, dan belum diuji dengan Motor
// 1 yang sengaja dirusak (unbalance dkk) buat mastiin Waspada/Bahaya
// tetap kebaca benar. Aman dipakai lanjut uji unbalance Motor 1
// sekarang, tapi kalau nanti mounting/motor diganti lagi, ulangi
// kalibrasi -- preset ini TERBUKTI bisa jadi stale kalau kondisi fisik
// berubah.
static float presetMesin1_mean[3]        = {7.953321f, 0.062564f, 0.018011f};
static float presetMesin1_sigmaInv[3][3] = {{1.065750f, -0.264302f, 0.012827f},
                                             {-0.264302f, 1.065606f, 0.004578f},
                                             {0.012827f, 0.004578f, 1.000215f}};
static float presetMesin1_stdDev[3]      = {0.242014f, 0.008488f, 0.026551f};
static float presetMesin1_bandMean[4]    = {146.257294f, 84.819695f, 66.021545f, 93.130043f};
static float presetMesin1_bandStd[4]     = {837.925964f, 489.407318f, 382.725464f, 532.225098f};
static float presetMesin1_audioMean[AUDIO_BAND_COUNT] = {0.807315f, 0.742594f, 0.072317f};
static float presetMesin1_audioStd[AUDIO_BAND_COUNT]  = {0.895213f, 0.894945f, 0.098766f};

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
