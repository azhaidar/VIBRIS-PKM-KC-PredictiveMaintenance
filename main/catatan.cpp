/*

#1 — PALING FATAL: Geometri bearing (n_balls, d_ball, D_pitch)
File: CodeVibris/FFTProcessor.cpp, baris:
cppfloat bpfo_hz = RPM_ComputeBPFO(fr_hz, 8, 3.5f, 22.0f, 0.0f);
float bpfi_hz = RPM_ComputeBPFI(fr_hz, 8, 3.5f, 22.0f, 0.0f);
Angka 8 (jumlah bola bearing), 3.5f (diameter bola mm), 22.0f (diameter pitch mm) itu spesifik satu jenis bearing. Kalian sendiri sudah tulis peringatan ini di RPMEstimator.h:

"Parameter yang dibutuhkan dari spesifikasi bearing... tetap harus diketahui dari datasheet bearing motor uji kalian — ini bukan sesuatu yang bisa diestimasi otomatis, harus dicatat manual saat pengujian."

Ini lebih parah dari SNR. SNR yang salah cuma bikin device kurang sensitif (miss detection). Geometri bearing yang salah bikin bpfo_hz/bpfi_hz yang dihitung itu frekuensi yang salah total — device bisa nyari kerusakan bearing di frekuensi yang gak ada hubungannya sama bearing asli yang terpasang. Kalau device dipindah dari motor A (bearing 8 bola) ke motor B (bearing 6 bola beda ukuran), deteksi BPFO/BPFI-nya langsung ngaco tanpa ada warning apapun.
Ini gak bisa di-self-baseline otomatis (gak ada cara ngukur jumlah bola bearing dari sinyal getaran doang) — solusinya BUKAN kalibrasi otomatis, tapi input manual per mesin, disimpan di flash bareng baseline lain:
cpp// Tambah di SharedTypes.h atau config.h
struct BearingSpec {
    int   n_balls;
    float d_ball_mm;
    float D_pitch_mm;
    float phi_deg;
};

// Definisikan per jenis mesin di config.h, bukan hardcode di FFTProcessor.cpp
static BearingSpec currentBearingSpec = {8, 3.5f, 22.0f, 0.0f}; // default, GANTI manual tiap ganti motor uji
Ganti pemanggilan di FFTProcessor.cpp jadi pakai currentBearingSpec.n_balls, dll — minimal biar gampang diganti 1 tempat pas kalian pindah motor uji, bukan tersebar hardcoded di tengah fungsi.
Buat laporan PIMNAS: ini WAJIB disebut di limitasi. Klaim "self-baseline, gak perlu konfigurasi manual" itu betul buat mean/kovarians 4 sensor, TAPI TIDAK betul buat parameter bearing. Jangan sampai juri nemuin ini sendiri pas nanya detail — sebut duluan sebagai batasan yang disadari.
#2 — SNR threshold (sudah dibahas)
Sudah saya kasih solusinya kemarin — kalibrasi otomatis dari data 180 detik.
#3 — Slew limiter RPM (RPM_MAX_DELTA_PER_CYCLE = 300.0f)
Ini juga saya yang nambahin kemarin — dan sama masalahnya kayak SNR. Motor kecil yang akselerasinya lambat vs motor gede yang bisa berubah RPM cepat, butuh angka beda.
Fix lebih robust: pakai persentase, bukan angka absolut, dihitung relatif dari RPM baseline hasil kalibrasi:
cpp// Bukan RPM_MAX_DELTA_PER_CYCLE tetap 300, tapi persentase dari RPM baseline
#define RPM_MAX_DELTA_PERCENT 0.20f  // toleransi 20% per siklus

float maxDelta = lastValidRPM * RPM_MAX_DELTA_PERCENT;
if (maxDelta < 50.0f) maxDelta = 50.0f;  // batas minimum biar gak terlalu ketat di RPM rendah

if (rpmResult > 0.0f && lastValidRPM > 0.0f &&
    fabsf(rpmResult - lastValidRPM) > maxDelta) {
    rpmResult = lastValidRPM + (rpmResult > lastValidRPM ? maxDelta : -maxDelta);
}
Ini otomatis nyesuain skala tanpa perlu kalibrasi terpisah — cukup ganti dari angka absolut ke persentase relatif.
#4 — Rentang pencarian RPM (FR_MIN_HZ=5, FR_MAX_HZ=50)
Ini bukan bug, ini keputusan desain sesuai spek target di proposal kalian (motor 300-3000 RPM). Tapi harus didisclose eksplisit sebagai batasan produk — kalau ada mesin target yang RPM-nya di luar rentang ini, device kalian gak akan bisa estimasi RPM-nya sama sekali (bakal selalu nyangkut di rentang 5-50Hz walau puncak sebenarnya ada di luar itu). Gak perlu diubah, tapi tulis di laporan sebagai "scope produk: motor kecil 300-3000 RPM", bukan klaim universal.
#5 — Lebar jendela pencarian band energy (±10% di sekitar 1x/2x RPM, BPFO, BPFI)
File: FFTProcessor.cpp:
cppbandEnergies_out[0] = bandEnergy(vReal, freqRes, 0.9f * fr_hz, 1.1f * fr_hz, FFT_SAMPLES);
±10% ini asumsi RPM motor stabil selama 1 window FFT. Kalau motor kalian goyang RPM-nya (yang justru kejadian di data log kalian kemarin — lompat 2088→2388), window ±10% bisa kelewat sempit, energi band jadi keitung salah. Prioritas rendah — baru relevan setelah #1-3 kelar, karena RPM yang stabil dulu (dari fix kemarin) baru bikin asumsi ±10% ini valid.


*/