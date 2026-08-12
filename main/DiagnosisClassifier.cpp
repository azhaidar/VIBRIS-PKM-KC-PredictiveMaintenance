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
                        float *confidenceOutput) 
{
    // Indeks pita frekuensi berdasarkan arti fisik (cetak biru proposal)
    // Index 0: 1x RPM -> Unbalance
    // Index 1: 2x RPM -> Misalignment
    // Index 2: BPFO   -> Outer Race Bearing Fault
    // Index 3: BPFI   -> Inner Race Bearing Fault

    float zScores[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float maxZScore = -999.0f;
    int maxIndex = 0;

    // 1. Hitung Z-Score untuk tiap band secara independen
    for (int i = 0; i < 4; i++) {
        // Proteksi pembagian dengan nol jika standar deviasi baseline tidak valid
        float stdDev = (bandBaselineStd[i] > 0.0001f) ? bandBaselineStd[i] : 1.0f;
        
        zScores[i] = (bandEnergies[i] - bandBaselineMean[i]) / stdDev;

        // 2. Cari nilai penyimpangan (Z-Score) tertinggi
        if (zScores[i] > maxZScore) {
            maxZScore = zScores[i];
            maxIndex = i;
        }
    }

    // 3. Evaluasi ambang batas berdasarkan parameter Z-Score di BAB 2.6
    const float Z_SCORE_THRESHOLD = 2.0f;

    if (maxZScore < Z_SCORE_THRESHOLD) {
        strcpy(labelOutput, "NORMAL");
        *confidenceOutput = 0.0f; // Tidak ada anomali terdeteksi
    } else {
        // 4. Rule-Based Heuristik: Petakan pita frekuensi dominan ke label kerusakan fisik
        switch (maxIndex) {
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
        // Keluarkan nilai Z-Score tertinggi sebagai metrik severity/confidence
        *confidenceOutput = maxZScore; 
    }
}
