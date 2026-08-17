// TinyMLClassifier.cpp
#include "TinyMLClassifier.h"
#include "DualCoreTaskScheduler.h"
#include <Arduino.h>
#include <string.h>
#include "MultiSensorFeatureMerger.h"

// ===================================================================
// LIBRARY HASIL EXPORT EDGE IMPULSE
// Wajib sudah di-install dulu ke Arduino IDE (Sketch > Include Library >
// Add .ZIP Library...) memakai file:
//   ei-radityaputraa-sketch-project-1-arduino-1_0_2-impulse-_2.zip
// sebelum baris #include di bawah ini bisa ditemukan compiler.
// ===================================================================
#include <radityaputraa-sketch-project-1_inferencing.h>

// Kalau library BELUM ter-install, error compile akan muncul di baris
// include di atas ("fatal error: ... No such file or directory"), BUKAN
// di file ini — lihat instruksi instalasi di README/panduan integrasi.

static const bool DEBUG_NN = false; // set true kalau mau lihat isi fitur mentah di Serial

// Buffer window: EI_CLASSIFIER_RAW_SAMPLE_COUNT (2) frame x
// EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME (8) axis = 16 nilai float.
static float mlBuffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
static int mlFrameIndex = 0;
static uint32_t mlLastSampleMs = 0;

static char mlLastLabel[16] = "N/A";
static float mlLastConfidence = 0.0f;
static bool mlHasNewResult = false;

void TinyML_Init() {
    memset(mlBuffer, 0, sizeof(mlBuffer));
    mlFrameIndex = 0;
    mlLastSampleMs = millis();
    strncpy(mlLastLabel, "N/A", sizeof(mlLastLabel) - 1);
    mlLastConfidence = 0.0f;
    mlHasNewResult = false;

    Serial.printf("[TinyML] Impulse '%s' siap. Axes=%s | frame=%d x %d | window=%dms\n",
                  EI_CLASSIFIER_PROJECT_NAME,
                  EI_CLASSIFIER_FUSION_AXES_STRING,
                  EI_CLASSIFIER_RAW_SAMPLE_COUNT,
                  EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME,
                  EI_CLASSIFIER_RAW_SAMPLE_COUNT * EI_CLASSIFIER_INTERVAL_MS);
}

static void runInferenceOnFullBuffer() {
    signal_t signal;
    int err = numpy::signal_from_buffer(mlBuffer, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
    if (err != 0) {
        Serial.printf("[TinyML][ERROR] signal_from_buffer gagal (%d)\n", err);
        return;
    }

    ei_impulse_result_t result = { 0 };
    err = run_classifier(&signal, &result, DEBUG_NN);
    if (err != EI_IMPULSE_OK) {
        Serial.printf("[TinyML][ERROR] run_classifier gagal (%d)\n", err);
        return;
    }

    // Ambil label dengan skor tertinggi (argmax) dari 3 kelas: hidup1/hidup2/mati1
    float bestScore = -1.0f;
    int bestIx = 0;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > bestScore) {
            bestScore = result.classification[ix].value;
            bestIx = (int)ix;
        }
    }
    strncpy(mlLastLabel, result.classification[bestIx].label, sizeof(mlLastLabel) - 1);
    mlLastLabel[sizeof(mlLastLabel) - 1] = '\0';
    // Sanitasi — buang karakter non-printable:
    for (int i = 0; mlLastLabel[i] != '\0'; i++) {
        if (mlLastLabel[i] < 32 || mlLastLabel[i] > 126) {
            mlLastLabel[i] = '_';
        }
    }
    mlLastLabel[sizeof(mlLastLabel) - 1] = '\0';
    mlLastConfidence = bestScore;
    mlHasNewResult = true;

    #if DEBUG_VERBOSE
        Serial.printf("[TinyML] Inference (DSP:%dms Klasifikasi:%dms) -> %s (%.3f)\n",
                      result.timing.dsp, result.timing.classification,
                      mlLastLabel, mlLastConfidence);
    #endif
}
void TinyML_Update(const SensorFeatures &features, float rpmEstimated) {
    mlHasNewResult = false; // reset dulu, cuma true di iterasi tepat setelah inferensi baru

    uint32_t now = millis();
    if ((now - mlLastSampleMs) < (uint32_t)EI_CLASSIFIER_INTERVAL_MS) {
        return; // belum waktunya sample berikutnya
    }
    mlLastSampleMs = now;

    float rmsX = 0.0f, rmsY = 0.0f, rmsZ = 0.0f;
    Scheduler_GetLatestAxisRMS(&rmsX, &rmsY, &rmsZ);

    // URUTAN INI WAJIB SAMA PERSIS DENGAN EI_CLASSIFIER_FUSION_AXES_STRING:
    // "rms_v + rms_x + rms_y + rms_z + rms_a + current + temp + rpm"
    int base = mlFrameIndex * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;
    mlBuffer[base + 0] = features.rms_getaran; // rms_v
    mlBuffer[base + 1] = rmsX;                 // rms_x
    mlBuffer[base + 2] = rmsY;                 // rms_y
    mlBuffer[base + 3] = rmsZ;                 // rms_z
    mlBuffer[base + 4] = features.rms_suara;   // rms_a
    //mlBuffer[base + 5] = getLatestArusForTinyML();   // current -- dari jalur khusus TinyML
    mlBuffer[base + 5] = features.suhu;        // temp
    //mlBuffer[base + 6] = rpmEstimated;         // rpm

    mlFrameIndex++;

    if (mlFrameIndex >= EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
        runInferenceOnFullBuffer();
        mlFrameIndex = 0; // window geser penuh (bukan sliding-overlap) — sederhana & cukup untuk 1Hz
    }
}

bool TinyML_HasNewResult() {
    return mlHasNewResult;
}

const char* TinyML_GetLabel() {
    return mlLastLabel;
}

float TinyML_GetConfidence() {
    return mlLastConfidence;
}