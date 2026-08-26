// FFTProcessor.cpp
#include "FFTProcessor.h"
#include <arduinoFFT.h>
#include <math.h>
#include "RPMEstimator.h"
#include "config.h"

#define SAMPLE_RATE VIBRATION_SAMPLE_RATE_HZ
#define FR_MIN_HZ 5.0
#define FR_MAX_HZ 50.0
// FIX (21 Agustus 2026): sebelumnya 60.0 di file ini vs 50.0 di
// RPMEstimator.cpp -- dua salinan angka yang sama tapi beda nilai.
// Yang beneran nentuin RPM reliable/tidak itu RPMEstimator.cpp (5-50Hz),
// jadi angka DIAGNOSTIK di file ini (dipakai buat print top3 puncak,
// lihat FFTProcessor_Process di bawah) disamain ke 50.0 juga -- biar
// yang kamu lihat di Serial Monitor sama persis dengan jendela yang
// beneran dipakai buat keputusan reliable.

double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE);

//Definisi Tunggal current bearing spec
BearingSpec currentBearingSpec = BEARING_TABLE[BEARING_DEFAULT_INDEX];

static bool hasRollingBearing = true;

void setBearingType(bool rollingBearing) {
    hasRollingBearing = rollingBearing;
}
void setBearingCluster(int clusterIndex) {
    if (clusterIndex < 0 || clusterIndex >= (int)BEARING_TABLE_SIZE) {
        Serial.printf("[FFTProcessor] ERROR: indeks klaster %d di luar jangkauan (0-%d).\n",
                      clusterIndex, (int)BEARING_TABLE_SIZE - 1);
        return;
    }
    currentBearingSpec = BEARING_TABLE[clusterIndex];
    Serial.printf("[FFTProcessor] Klaster bearing diganti ke: %s\n", currentBearingSpec.label);
}

static float stableRPM = 0.0f;
static int reliableStreak = 0;
static int unreliableStreak = 0;   // BARU: hitung berapa kali BERTURUT sinyal gagal reliable
#define UNRELIABLE_CONFIRM_STREAK 3   // butuh 3x berturut baru dianggap "beneran diam", bukan 1x dip sesaat

#define SPECTRAL_AVG_COUNT 12   // rata-rata 6 siklus FFT sebelum cari puncak/SNR
static double avgMagnitude[FFT_SAMPLES / 2] = {0};
static int avgAccumCount = 0;
void FFTProcessor_Init() {}

float bandEnergy(double *magnitude, float freqResolution, float f_low, float f_high, int n) {
    int binLow = (int)(f_low / freqResolution);
    int binHigh = (int)(f_high / freqResolution);
    float energy = 0;
    for (int i = binLow; i <= binHigh && i < n/2; i++) {
        energy += magnitude[i] * magnitude[i];
    }
    return energy;
}

void FFTProcessor_Process(VibrationBuffer *input, SensorFeatures *features,
                            float *rpm_out, float *bandEnergies_out, float *snr_out) {
    double mean = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) mean += input->samples[i];
    mean /= FFT_SAMPLES;
    
    // BARU: Kurtosis -- ukur "seberapa spike" sinyal getaran, sensitif ke
    // benturan mikro logam-ke-logam akibat cacat bearing. Sehat ~3.0,
    // naik signifikan (>4-7) kalau ada cacat. Sumber: literature review VIBRIS.
    double sum2 = 0, sum4 = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) {
        double d = input->samples[i] - mean;
        double d2 = d * d;
        sum2 += d2;
        sum4 += d2 * d2;
    }
    double variance = sum2 / FFT_SAMPLES;
    float kurtosis = (variance > 1e-9) ? (float)((sum4 / FFT_SAMPLES) / (variance * variance)) : 0.0f;
    features->kurtosis = kurtosis;   // lihat langkah 2 di bawah -- perlu tambah field ini


    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = input->samples[i] - mean;
        vImag[i] = 0;
    }

    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();


    // BARU: akumulasi spektrum mentah dulu sebelum dipakai cari puncak/SNR.
    // Noise acak saling membatalkan kalau dirata-rata, puncak motor yang
    // konsisten tetap kuat -- SNR naik ~sqrt(SPECTRAL_AVG_COUNT) kali lipat.
    for (int i = 0; i < FFT_SAMPLES / 2; i++) {
        avgMagnitude[i] += vReal[i];
    }
    avgAccumCount++;

    float sumSquare = 0;

    for (int i = 0; i < FFT_SAMPLES; i++) sumSquare += input->samples[i] * input->samples[i];
    features->rms_getaran = sqrt(sumSquare / FFT_SAMPLES);

    // Belum cukup siklus terakumulasi -- pertahankan RPM stabil sebelumnya,
    // jangan proses puncak/SNR dulu (datanya belum "matang").
    if (avgAccumCount < SPECTRAL_AVG_COUNT) {
        *rpm_out = stableRPM;
        if (snr_out) *snr_out = 0.0f;
        for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
        return;
    }

    // Cukup akumulasi -- rata-ratakan spektrumnya, timpa vReal[] dengan hasil
    // rata-rata (bandEnergy/RPM_Estimate di bawah cuma pernah baca vReal[0..n/2],
    // jadi aman ditimpa segini), lalu reset akumulator buat siklus berikutnya.
    for (int i = 0; i < FFT_SAMPLES / 2; i++) {
        vReal[i] = avgMagnitude[i] / SPECTRAL_AVG_COUNT;
        avgMagnitude[i] = 0.0;
    }
    avgAccumCount = 0;


    float effectiveSampleRate = (input->actual_rate_hz > 1.0f) ?
        input->actual_rate_hz : SAMPLE_RATE;


    float snr = 0.0f;
    bool snrReliable = RPM_IsSignalReliable(vReal, FFT_SAMPLES, effectiveSampleRate, &snr);
    if (snr_out) *snr_out = snr;

    // FIX LAMA: gerbang RMS-floor ADAPTIF (ambientRmsEMA) DIHAPUS -- variabel
    // itu niatnya belajar getaran "saat motor diam" sendiri lewat EMA, tapi
    // protokol pengujian kita mengharuskan motor SUDAH jalan sebelum device
    // di-reset, jadi variabel itu tidak pernah punya data "diam" untuk
    // dipelajari dan malah mengejar levelnya sendiri sampai tidak pernah bisa
    // terpenuhi. SNR check sendirian ternyata TIDAK cukup -- dibuktikan data
    // snapshot_BAHAYA 19 Agustus 2026: banyak baris rms_v cuma ~0.09-0.12
    // (getaran nyaris gak ada) tapi tetap kebaca reliable dengan RPM 338-384,
    // dan D2 sampai >100 (harusnya ini "Diam", bukan "Bahaya").
    //
    // FIX BARU (20 Agustus 2026): tambah syarat KEDUA yang FIXED (bukan
    // belajar sendiri lagi) -- VIBRATION_ABSOLUTE_FLOOR di config.h, angka
    // ditentukan dari celah kosong nyata di data rms_v (lihat komentar di
    // config.h). Sinyal cuma dianggap reliable kalau SNR-nya bagus DAN
    // amplitudonya (features->rms_getaran) beneran di atas ambang ini.
    bool reliable = snrReliable && (features->rms_getaran > VIBRATION_ABSOLUTE_FLOOR);

    // Diagnostik: cari & CETAK puncak spektrum di rentang 5-50Hz SELALU --
    // tidak digerbang oleh "reliable". Supaya kamu bisa lihat langsung di
    // Serial Monitor frekuensi apa yang sebenarnya dilihat FFT.

    // BARU: cari 3 PUNCAK TERTINGGI (bukan cuma 1) di rentang 5-50Hz, buat
    // validasi manual terhadap tachometer/Phyphox. RPM_Estimate() di bawah
    // TETAP pakai puncak tertinggi tunggal seperti sebelumnya -- ini murni
    // tambahan untuk membantu kamu mengecek, tidak mengubah cara sistem
    // memutuskan RPM.
    float freqResDiag = effectiveSampleRate / FFT_SAMPLES;
    int binMinDiag = (int)(FR_MIN_HZ / freqResDiag);
    int binMaxDiag = (int)(FR_MAX_HZ / freqResDiag);
    float top3Amp[3] = {0.0f, 0.0f, 0.0f};
    int top3Bin[3] = {binMinDiag, binMinDiag, binMinDiag};
    for (int i = binMinDiag; i <= binMaxDiag && i < FFT_SAMPLES / 2; i++) {
        float amp = (float)vReal[i];
        if (amp > top3Amp[0]) {
            top3Amp[2] = top3Amp[1]; top3Bin[2] = top3Bin[1];
            top3Amp[1] = top3Amp[0]; top3Bin[1] = top3Bin[0];
            top3Amp[0] = amp; top3Bin[0] = i;
        } else if (amp > top3Amp[1]) {
            top3Amp[2] = top3Amp[1]; top3Bin[2] = top3Bin[1];
            top3Amp[1] = amp; top3Bin[1] = i;
        } else if (amp > top3Amp[2]) {
            top3Amp[2] = amp; top3Bin[2] = i;
        }
    }
    #if DEBUG_VERBOSE
        Serial.printf("[FFT-DIAG] top3: #1=%.2fHz(~%.0fRPM,amp=%.1f) #2=%.2fHz(~%.0fRPM,amp=%.1f) #3=%.2fHz(~%.0fRPM,amp=%.1f) | snr=%.2f snrOK=%d | rms=%.4f\n",
            top3Bin[0]*freqResDiag, top3Bin[0]*freqResDiag*60.0f, top3Amp[0],
            top3Bin[1]*freqResDiag, top3Bin[1]*freqResDiag*60.0f, top3Amp[1],
            top3Bin[2]*freqResDiag, top3Bin[2]*freqResDiag*60.0f, top3Amp[2],
            snr, snrReliable, features->rms_getaran);
    #endif
    if (!reliable) {
        unreliableStreak++;
        if (unreliableStreak >= UNRELIABLE_CONFIRM_STREAK) {
            // Sudah 3x berturut gagal -- BARU dianggap sinyal beneran hilang
            reliableStreak = 0;
            stableRPM = 0.0f;
            *rpm_out = 0.0f;
            for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
            features->valid = false;
            return;
        } else {
            // Masih dalam toleransi -- 1-2 dip sesaat, PERTAHANKAN RPM lama,
            // jangan langsung nol. Band energy tetap di-nol-kan karena spektrum
            // siklus ini nggak reliable dipakai buat itu, tapi RPM/status nggak
            // ikut kepengaruh dip sesaat.
            *rpm_out = stableRPM;
            for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
            features->valid = true;
            return;
        }
    }
    unreliableStreak = 0;   // BARU: sinyal balik reliable, reset counter

    float fr_rpm = RPM_Estimate(vReal, FFT_SAMPLES, effectiveSampleRate);

    // BARU: tolak lompatan RPM yang terlalu drastis (>60% dari nilai sebelumnya)
    // dalam SATU siklus -- motor fisik nggak mungkin lompat kecepatan sedrastis
    // itu instan, jadi ini kemungkinan besar salah baca harmonik yang masih
    // lolos dari filter di RPMEstimator.cpp
    if (stableRPM > 0.0f) {
        float relativeChange = fabsf(fr_rpm - stableRPM) / stableRPM;
        if (relativeChange > 0.6f) {
            fr_rpm = stableRPM;   // tolak, pertahankan nilai lama
        }
    }

    // FIX (21 Agustus 2026): 4 baris ini sebelumnya keulang 2x (copy-paste)
    // -- efeknya syarat ">=2 siklus berturut baru dipercaya" tembus di
    // SIKLUS PERTAMA juga (karena counter-nya nambah 2x dalam 1x panggilan
    // fungsi ini), jadi debounce-nya nggak beneran nunggu 2 siklus konsisten
    // kayak niatnya. Sekarang cuma sekali, sesuai maksud aslinya.
    reliableStreak++;
    if (reliableStreak >= 2) stableRPM = fr_rpm;
    *rpm_out = stableRPM;

    float fr_hz = fr_rpm / 60.0;
    float freqRes = effectiveSampleRate / FFT_SAMPLES;
    #if 1 //ENABLE_RPM_DIAGNOSIS
        // bandEnergies_out[0] = bandEnergy(vReal, freqRes, 0.9f * fr_hz, 1.1f * fr_hz, FFT_SAMPLES);
        // bandEnergies_out[1] = bandEnergy(vReal, freqRes, 1.9f * fr_hz, 2.1f * fr_hz, FFT_SAMPLES);
        // if (hasRollingBearing) {
        //     float bpfo_hz = RPM_ComputeBPFO(fr_hz, currentBearingSpec.n_balls,
        //         currentBearingSpec.d_ball_mm, currentBearingSpec.D_pitch_mm,
        //         currentBearingSpec.phi_deg);
        //     float bpfi_hz = RPM_ComputeBPFI(fr_hz, currentBearingSpec.n_balls,
        //         currentBearingSpec.d_ball_mm, currentBearingSpec.D_pitch_mm,
        //         currentBearingSpec.phi_deg);

        //     // DITAMBAHKAN — dua frekuensi bearing yang sebelumnya belum ada
        //     float bsf_hz = RPM_ComputeBSF(fr_hz, currentBearingSpec.n_balls,
        //         currentBearingSpec.d_ball_mm, currentBearingSpec.D_pitch_mm,
        //         currentBearingSpec.phi_deg);
        //     float ftf_hz = RPM_ComputeFTF(fr_hz,
        //         currentBearingSpec.d_ball_mm, currentBearingSpec.D_pitch_mm,
        //         currentBearingSpec.phi_deg);

        //     bandEnergies_out[2] = bandEnergy(vReal, freqRes,
        //         0.9f * bpfo_hz, 1.1f * bpfo_hz, FFT_SAMPLES);
        //     bandEnergies_out[3] = bandEnergy(vReal, freqRes,
        //         0.9f * bpfi_hz, 1.1f * bpfi_hz, FFT_SAMPLES);

        //     //DITAMBAHKAN — print BSF dan FTF ke Serial untuk validasi
        //     // (belum masuk bandEnergies karena array hanya ukuran 4)
        //     Serial.printf("[FFT] BPFO=%.1fHz BPFI=%.1fHz BSF=%.1fHz FTF=%.1fHz\n",
        //                 bpfo_hz, bpfi_hz, bsf_hz, ftf_hz);

        //     // ✂️ DIHAPUS — baris ini salah secara sintaks C++, 
        //     // deklarasi fungsi tidak boleh di dalam blok if:
        //     // void setBearingType(bool rollingBearing);

        // } else {
        //     bandEnergies_out[2] = 0.0f;
        //     bandEnergies_out[3] = 0.0f;
        // }
            bandEnergies_out[0] = bandEnergy(vReal, freqRes,
                (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.oneX_hz,
                (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.oneX_hz, FFT_SAMPLES);
            bandEnergies_out[1] = bandEnergy(vReal, freqRes,
                (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.twoX_hz,
                (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.twoX_hz, FFT_SAMPLES);

            if (hasRollingBearing) {
                bandEnergies_out[2] = bandEnergy(vReal, freqRes,
                    (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.bpfo_hz,
                    (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.bpfo_hz, FFT_SAMPLES);
                bandEnergies_out[3] = bandEnergy(vReal, freqRes,
                    (1.0f - BAND_WINDOW_PERCENT) * currentBearingSpec.bpfi_hz,
                    (1.0f + BAND_WINDOW_PERCENT) * currentBearingSpec.bpfi_hz, FFT_SAMPLES);
            } else {
                bandEnergies_out[2] = 0.0f;
                bandEnergies_out[3] = 0.0f;
            }
        
    #else
        // ENABLE_RPM_DIAGNOSIS = 0: sistem murni domain frekuensi non-order-tracking
        // (lihat justifikasi lit review VIBRIS soal "tanpa RPM"). Nol-kan saja,
        // jangan buang siklus CPU ESP32 buat hitung sesuatu yang nggak dipakai.
        for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
    #endif
    features->valid = true;
    
}