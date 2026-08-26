#include "DiagnosisClassifier.h"

// ============================================================
/*Catatan implementasi (belum ditulis kodenya, rangkuman apa yang
harus ada nanti saat mulai coding):
1. Untuk tiap 4 band, hitung Z-score band = (energi_sekarang -
   baseline_mean) / baseline_std.
2. Cari Z-score band tertinggi dari keempatnya.
3. Kalau Z-score tertinggi masih di bawah ambang (misal 2.0,
   konsisten dengan ambang Z-Score yang sudah dipakai di proposal
   BAB 2.6), output label "Normal".
4. Kalau di atas ambang, output label sesuai band mana yang
   trigger (unbalance/misalignment/BPFO/BPFI), dan nilai Z-score
   band itu dikeluarkan juga sebagai indikator confidence/severity.
 5. Fungsi ini TIDAK punya state internal — semua baseline (mean,
   std per band) di-supply dari luar (dari CalibrationManager atau
   BaselineUpdater), fungsi ini murni stateless classifier.
*/ 

// #include "DiagnosisClassifier.h"
// #include <string.h>
// #include <math.h>

// void Diagnosis_Classify(float bandEnergies[4], float bandBaselineMean[4],
//                         float bandBaselineStd[4], char *labelOutput,
//                         float *confidenceOutput, uint8_t *flagsOutput)  
// {
//     // Indeks pita frekuensi berdasarkan arti fisik (cetak biru proposal)
//     // Index 0: 1x RPM -> Unbalance
//     // Index 1: 2x RPM -> Misalignment
//     // Index 2: BPFO   -> Outer Race Bearing Fault
//     // Index 3: BPFI   -> Inner Race Bearing Fault
//     const float Z_SCORE_THRESHOLD = 2.0f;
//     float zScores[4] = {0.0f, 0.0f, 0.0f, 0.0f};
//     float maxZScore = -999.0f;
//     int maxIndex = 0;
//     // 1. Hitung Z-Score untuk tiap band secara independen
//     uint8_t flags = 0;
//     for (int i = 0; i < 4; i++) {
//         // Proteksi pembagian dengan nol jika standar deviasi baseline tidak valid
//         float stdDev = (bandBaselineStd[i] > 0.0001f) ? bandBaselineStd[i] : 1.0f;
        
//         zScores[i] = (bandEnergies[i] - bandBaselineMean[i]) / stdDev;
//         if (zScores[i] > Z_SCORE_THRESHOLD) {      // BARU: cek tiap band, bukan cuma yang max
//             flags |= (1 << i);                      // BARU: nyalakan bit sesuai index band
//         }
//         // 2. Cari nilai penyimpangan (Z-Score) tertinggi
//         if (zScores[i] > maxZScore) {
//             maxZScore = zScores[i];
//             maxIndex = i;
//         }
//     }
//     *flagsOutput = flags;   // BARU: keluarkan bitmask lengkap
//     // 3. Evaluasi ambang batas berdasarkan parameter Z-Score di BAB 2.6

//     if (maxZScore < Z_SCORE_THRESHOLD) {
//         strcpy(labelOutput, "NORMAL");
//         *confidenceOutput = 0.0f; // Tidak ada anomali terdeteksi
//     } else {
//         // 4. Rule-Based Heuristik: Petakan pita frekuensi dominan ke label kerusakan fisik
//         switch (maxIndex) {
//             case 0:
//                 strcpy(labelOutput, "UNBALANCE");
//                 break;
//             case 1:
//                 strcpy(labelOutput, "MISALIGNMENT");
//                 break;
//             case 2:
//                 strcpy(labelOutput, "BEARING_BPFO");
//                 break;
//             case 3:
//                 strcpy(labelOutput, "BEARING_BPFI");
//                 break;
//             default:
//                 strcpy(labelOutput, "UNKNOWN_ANOMALY");
//                 break;
//         }
//         // Keluarkan nilai Z-Score tertinggi sebagai metrik severity/confidence
//         *confidenceOutput = maxZScore; 
//     }
// }
#include "DiagnosisClassifier.h"

// ============================================================
/*Catatan implementasi (belum ditulis kodenya, rangkuman apa yang
harus ada nanti saat mulai coding):
1. Untuk tiap 4 band, hitung Z-score band = (energi_sekarang -
   baseline_mean) / baseline_std.
2. Cari Z-score band tertinggi dari keempatnya.
3. Kalau Z-score tertinggi masih di bawah ambang (misal 2.0,
   konsisten dengan ambang Z-Score yang sudah dipakai di proposal
   BAB 2.6), output label "Normal".
4. Kalau di atas ambang, output label sesuai band mana yang
   trigger (unbalance/misalignment/BPFO/BPFI), dan nilai Z-score
   band itu dikeluarkan juga sebagai indikator confidence/severity.
 5. Fungsi ini TIDAK punya state internal — semua baseline (mean,
   std per band) di-supply dari luar (dari CalibrationManager atau
   BaselineUpdater), fungsi ini murni stateless classifier.
*/ 

#include "DiagnosisClassifier.h"
#include <string.h>
#include <math.h>

void Diagnosis_Classify(float bandEnergies[4], float bandBaselineMean[4],
                        float bandBaselineStd[4], char *labelOutput,
                        float *confidenceOutput, uint8_t *flagsOutput)
{
    // Indeks pita frekuensi berdasarkan arti fisik (cetak biru proposal)
    // Index 0: 1x RPM -> Unbalance
    // Index 1: 2x RPM -> Misalignment
    // Index 2: BPFO   -> Outer Race Bearing Fault
    // Index 3: BPFI   -> Inner Race Bearing Fault
    const float Z_SCORE_THRESHOLD = 2.0f;
    float zScores[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float maxZScore = -999.0f;
    float maxEnergy = -999.0f;      // FIX (26 Agustus 2026): cari energy tertinggi juga
    int maxZScoreIndex = 0;
    int maxEnergyIndex = 0;         // FIX: index band dengan energy tertinggi

    // 1. Hitung Z-Score untuk tiap band secara independen
    uint8_t flags = 0;
    for (int i = 0; i < 4; i++) {
        // Proteksi pembagian dengan nol jika standar deviasi baseline tidak valid
        float stdDev = (bandBaselineStd[i] > 0.0001f) ? bandBaselineStd[i] : 1.0f;

        zScores[i] = (bandEnergies[i] - bandBaselineMean[i]) / stdDev;
        if (zScores[i] > Z_SCORE_THRESHOLD) {      // BARU: cek tiap band, bukan cuma yang max
            flags |= (1 << i);                      // BARU: nyalakan bit sesuai index band
        }

        // 2. Cari nilai penyimpangan (Z-Score) tertinggi -- TETAP pakai untuk threshold
        if (zScores[i] > maxZScore) {
            maxZScore = zScores[i];
            maxZScoreIndex = i;
        }

        // FIX (26 Agustus 2026): cari ENERGY tertinggi untuk diagnosis
        // Alasan: motor dengan unbalance punya energy unbalance paling besar,
        // diagnosis harus follow band dengan energy dominan, bukan Z-score
        // yang bisa bias terhadap baseline std yang berbeda-beda per band
        if (bandEnergies[i] > maxEnergy) {
            maxEnergy = bandEnergies[i];
            maxEnergyIndex = i;
        }
    }
    *flagsOutput = flags;   // BARU: keluarkan bitmask lengkap
    // 3. Evaluasi ambang batas berdasarkan parameter Z-Score di BAB 2.6

    if (maxZScore < Z_SCORE_THRESHOLD) {
        // Tidak ada band menyimpang > threshold -- diagnosis NORMAL
        strcpy(labelOutput, "NORMAL");
        *confidenceOutput = 0.0f; // Tidak ada anomali terdeteksi
    } else {
        // Ada band yang menyimpang -- gunakan ENERGY TERTINGGI untuk diagnosis
        // (bukan Z-score tertinggi, yang bisa bias terhadap std kecil)
        // 4. Rule-Based Heuristik: Petakan pita frekuensi dominan ke label kerusakan fisik
        switch (maxEnergyIndex) { 
            case 0:
                strcpy(labelOutput, "UNBALANCE");
                break;
            case 1:
                strcpy(labelOutput, "MISALIGNMENT");
                break;
            case 2:
                strcpy(labelOutput, "BEARING_BPFO");
                break;
            case 3:
                strcpy(labelOutput, "BEARING_BPFI");
                break;
            default:
                strcpy(labelOutput, "UNKNOWN_ANOMALY");
                break;
        }
        // Keluarkan nilai Z-Score (untuk confidence metric, tetap pakai Z-score)
        *confidenceOutput = maxZScore;     // Confidence = Z-score tertinggi
    }
}