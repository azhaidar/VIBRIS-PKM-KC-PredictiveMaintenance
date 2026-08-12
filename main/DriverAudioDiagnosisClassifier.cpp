// AudioDiagnosisClassifier.cpp
#include "DriverAudioDiagnosisClassifier.h"
#include <string.h>

// Ambang sama seperti DiagnosisClassifier getaran (Z-score 2.0), untuk
// konsistensi lintas modul -- lihat BAB 2.6 proposal.
#define AUDIO_Z_SCORE_THRESHOLD 2.0f

void DriverAudioDiagnosis_Classify(float bandEnergies[3], float bandBaselineMean[3],
                              float bandBaselineStd[3], char *labelOutput,
                              float *confidenceOutput) {
    // Index 0: LOW  -> rumble mekanis frekuensi rendah
    // Index 1: MID  -> baseline dengungan motor normal
    // Index 2: HIGH -> gesekan/decitan frekuensi tinggi
    static const char* labels[3] = {"LOW_FREQ_RUMBLE", "MID_FREQ_ANOMALY", "HIGH_FREQ_SQUEAL"};

    float zScores[3];
    float maxZ = -999.0f;
    int maxIdx = 0;

    for (int i = 0; i < 3; i++) {
        float sd = (bandBaselineStd[i] > 0.0001f) ? bandBaselineStd[i] : 1.0f;
        zScores[i] = (bandEnergies[i] - bandBaselineMean[i]) / sd;
        if (zScores[i] > maxZ) {
            maxZ = zScores[i];
            maxIdx = i;
        }
    }

    if (maxZ < AUDIO_Z_SCORE_THRESHOLD) {
        strcpy(labelOutput, "NORMAL");
        *confidenceOutput = 0.0f;
    } else {
        strcpy(labelOutput, labels[maxIdx]);
        *confidenceOutput = maxZ;
    }
}