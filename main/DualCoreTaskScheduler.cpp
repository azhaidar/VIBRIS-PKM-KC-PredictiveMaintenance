#include "DualCoreTaskScheduler.h"
#include "FFTProcessor.h"
#include "DriverAudioFFTProcessor.h"
#include "config.h"
#include "MultiSensorFeatureMerger.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t vibrationQueue = NULL;
static QueueHandle_t audioQueue = NULL;
static volatile float latestRPM = 0.0f;
static volatile float latestSNR = 0.0f;

static float lastValidRPM = 0.0f;
//#define RPM_MAX_DELTA_PER_CYCLE 300.0f
static float latestBandEnergies[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static float latestAudioBandEnergies[AUDIO_BAND_COUNT] = {0.0f, 0.0f, 0.0f};   // BARU

QueueHandle_t Scheduler_GetVibrationQueue() { return vibrationQueue; }
QueueHandle_t Scheduler_GetAudioQueue() { return audioQueue; }

static float latestRmsX = 0.0f;
static float latestRmsZ = 0.0f;
static float latestRmsY = 0.0f;

static volatile float latestRoughness = 0.0f;
static volatile float latestBrightness = 0.0f;

static void TaskFFTProcessor(void *pvParameters) {
    static VibrationBuffer incomingBuffer;
    float rpmResult = 0.0f;
    float snrResult = 0.0f;
    float bandEnergies[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    SensorFeatures fftLocalFeatures;

    FFTProcessor_Init();

    for (;;) {
        if (xQueueReceive(vibrationQueue, &incomingBuffer, portMAX_DELAY) == pdTRUE) {
            FFTProcessor_Process(&incomingBuffer, &fftLocalFeatures, &rpmResult, bandEnergies, &snrResult);
            latestSNR = snrResult;            
        if (rpmResult > 0.0f && lastValidRPM > 0.0f) {
            float maxDelta = lastValidRPM * RPM_MAX_DELTA_PERCENT;
            if (maxDelta < RPM_MAX_DELTA_MIN) maxDelta = RPM_MAX_DELTA_MIN;
            if (fabsf(rpmResult - lastValidRPM) > maxDelta) {
                rpmResult = lastValidRPM + (rpmResult > lastValidRPM ? maxDelta : -maxDelta);
            }
        }
  
            }
            if (rpmResult > 0.0f) lastValidRPM = rpmResult;
            latestRPM = rpmResult;
            latestSNR = snrResult;
            latestRmsX = incomingBuffer.rms_x_raw;   // 
            latestRmsZ = incomingBuffer.rms_z_raw;
            latestRmsY = incomingBuffer.rms_y_raw;   //
            for (int i = 0; i < 4; i++) latestBandEnergies[i] = bandEnergies[i];

            updateVibrationFeature(fftLocalFeatures.rms_getaran);
            updateKurtosisFeature(fftLocalFeatures.kurtosis);

            #if DEBUG_VERBOSE
                Serial.printf("[FFT] RPM=%.1f |  Y_RMS=%.4f | X_RMS=%.4f | Z_RMS=%.4f | Unbalance=%.2f | Misalign=%.2f\n",
                              rpmResult, latestRmsY, latestRmsX, latestRmsZ, bandEnergies[0], bandEnergies[1]);
            #endif
        }
    }
void Scheduler_GetLatestAxisRMS(float *xOut, float *yOut, float *zOut) {
    *xOut = latestRmsX;
    *yOut = latestRmsY;
    *zOut = latestRmsZ;
}

static void TaskAudioFFTProcessor(void *pvParameters) {
    static AudioBuffer incomingAudio;
    float bandEnergies[AUDIO_BAND_COUNT];

    DriverAudioFFTProcessor_Init();

    for (;;) {
        if (xQueueReceive(audioQueue, &incomingAudio, portMAX_DELAY) == pdTRUE) {
            float roughness = 0.0f, brightness = 0.0f;
            DriverAudioFFTProcessor_Process(&incomingAudio, bandEnergies, &roughness, &brightness);
            for (int i = 0; i < AUDIO_BAND_COUNT; i++) latestAudioBandEnergies[i] = bandEnergies[i];
            latestRoughness  = roughness;
            latestBrightness = brightness;
            }
    }
}
void Scheduler_InitTasks() {
    vibrationQueue = xQueueCreate(1, sizeof(VibrationBuffer));
    audioQueue = xQueueCreate(1, sizeof(AudioBuffer)); 

    xTaskCreatePinnedToCore(
        TaskFFTProcessor, "Task_FFT", STACK_TASK_FFT, NULL,
        PRIO_TASK_FFT, NULL, CORE_SYSTEM_SLOW_IO
    );

    xTaskCreatePinnedToCore(   // BARU
        TaskAudioFFTProcessor, "Task_AudioFFT", STACK_TASK_AUDIO_FFT, NULL,
        PRIO_TASK_AUDIO_FFT, NULL, CORE_SYSTEM_SLOW_IO
    );
}

float Scheduler_GetLatestRPM() {
    return latestRPM;
}
float Scheduler_GetLatestSNR() {
    return latestSNR;
}
void Scheduler_GetLatestBandEnergies(float *dest) {
    for (int i = 0; i < 4; i++) dest[i] = latestBandEnergies[i];
}
void Scheduler_GetLatestAudioBandEnergies(float *dest) {  
    for (int i = 0; i < AUDIO_BAND_COUNT; i++) dest[i] = latestAudioBandEnergies[i];
}
float Scheduler_GetLatestRoughness()  { return latestRoughness; }
float Scheduler_GetLatestBrightness() { return latestBrightness; }