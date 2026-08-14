#include "config.h"
#include "SharedTypes.h"
#include "DriverSuhu.h"
#include "DriverArus.h"
#include "DriverGetaran.h"
#include "DriverINM.h"
#include "DualCoreTaskScheduler.h"
#include "MultiSensorFeatureMerger.h"
#include "GenericThresholdClassifier.h"
#include "RaspberryPiDataTransmitter.h"
#include "DiagnosisClassifier.h"
#include "MahalanobisDetector.h"
#include "InitialBaselineCalibrator.h"   
#include "AdaptiveBaselineLearner.h" 
#include "RPMEstimator.h"
#include "FFTProcessor.h"
#include "TinyMLClassifier.h"

// Catatan perubahan (biar klean lain paham kenapa file ini beda
// dari versi sebelumnya):
// Versi lama menunggu fase KALIBRASI 60 detik (self-baseline + Mahalanobis)
// sebelum status Normal/Waspada/Bahaya bisa muncul. Di lapangan itu bikin
// dashboard "diam" 1 menit tiap kali device baru nyala/dipindah ke mesin
// lain, dan begitu kalibrasi selesai, seluruh status malah nyangkut di
// "Bahaya" terus (baseline hasil kalibrasi singkat gampang rusak/tidak stabil
// -> Mahalanobis D2 jadi meledak untuk data yang sebenarnya normal).
//
// Versi ini melewati proses kalibrasi itu sepenuhnya: begitu perangkat
// menyala, tiap sample sensor langsung diklasifikasi memakai ambang batas
// TETAP (lihat GenericThresholdClassifier.cpp), jadi dashboard langsung
// menampilkan grafik + status sejak detik pertama, di mesin/lokasi mana pun
// modul sensor dipasang.
//
// Tambahan: beberapa sensor (mic INMP441, buffer FFT getaran, dll.) butuh
// beberapa detik pertama untuk mengisi buffer sebelum datanya "fresh".
// Tanpa penanganan khusus, beberapa cycle pertama setelah boot akan
// dilaporkan sebagai "SensorFault" padahal sensor sebenarnya baik-baik saja,
// cuma belum sempat mengambil sample pertama. WARMUP_GRACE_MS memberi
// toleransi waktu itu supaya status yang tampil di dashboard saat baru
// dibuka adalah "Warming" (wajar), bukan "SensorFault" (menyesatkan seolah
// ada yang rusak). Kalau setelah masa toleransi ini data masih belum fresh
// juga, baru dianggap SensorFault sungguhan (sensor/kabel bermasalah).
#define WARMUP_GRACE_MS 8000
#define PLOTTER_MODE 0

static unsigned long bootMillis = 0;
static char groundTruthLabel[16] = "NORMAL";

static float bandBaselineMean[4] = {0.20f, 0.20f, 0.20f, 0.20f};
static float bandBaselineStd[4]  = {0.10f, 0.10f, 0.10f, 0.10f};
#define CALIBRATION_DURATION_MS 60000UL  
static unsigned long calibrationStartMillis = 0;
static int currentMachineSlot = -1;   // BARU: -1 = belum ada mesin dipilih
void setup() {
    TinyML_Init();
    setDiagnosisBandBaseline(bandBaselineMean, bandBaselineStd);
    Transmitter_Init(115200);
    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[SYSTEM] Booting Clean Modular Sensor Core..."));
    xTaskCreatePinnedToCore(TaskDriverINM, "Task_INM", STACK_TASK_INM, NULL, PRIO_TASK_INM, NULL, CORE_DSP_HIGH_SPEED);
    Scheduler_InitTasks();
    xTaskCreatePinnedToCore(TaskDriverGetaran, "Task_Vib", 3072, NULL, PRIO_TASK_VIB, NULL, CORE_DSP_HIGH_SPEED);
    #if ENABLE_ARUS_SENSOR
        xTaskCreatePinnedToCore(TaskDriverArus,"Task_Arus", STACK_TASK_ARUS,NULL, PRIO_TASK_ARUS, NULL, CORE_SYSTEM_SLOW_IO);
    #endif
    xTaskCreatePinnedToCore(TaskDriverSuhu, "Task_Suhu", STACK_TASK_SUHU, NULL, PRIO_TASK_SUHU, NULL, CORE_SYSTEM_SLOW_IO);
 
    bootMillis = millis();
    startCalibrationPhase();
    calibrationStartMillis = millis();
    Serial.println(F("[SYSTEM] Boot Complete. Memulai fase kalibrasi self-baseline (180 detik nyata)."));
}
void selectMachineBaselineSlot(int slot) {
    if (slot == currentMachineSlot) return;   // sudah di mesin ini, gak perlu ngapa-ngapain
    currentMachineSlot = slot;

    float mean[3], sigmaInv[3][3], stdDev[3];
    if (loadBaselineFromFlash(slot, mean, sigmaInv, stdDev)) {
        setFeatureStdDev(stdDev);
        initializeBaselineLearner(mean, stdDev, sigmaInv);

        float bandMean[4], bandStd[4];
        if (loadBandBaselineFromFlash(slot, bandMean, bandStd)) {
            setDiagnosisBandBaseline(bandMean, bandStd);
        }
        float audioMean[AUDIO_BAND_COUNT], audioStd[AUDIO_BAND_COUNT];
        if (loadAudioBandBaselineFromFlash(slot, audioMean, audioStd)) {
            setAudioBandBaseline(audioMean, audioStd);
        }
        Serial.printf("[SYSTEM] Baseline mesin #%d dimuat -- deteksi langsung aktif.\n", slot);
    } else {
        Serial.printf("[SYSTEM] Belum ada baseline utk mesin #%d. Mulai kalibrasi baru (180 detik)...\n", slot);
        startCalibrationPhase();
        calibrationStartMillis = millis();
    }
}
void loop() {
    SensorFeatures merged{};
    bool fresh = getMergedFeatures(&merged);
    bool stillWarmingUp = (millis() - bootMillis) < WARMUP_GRACE_MS;

    DetectionResult result{};
    result.rpm_estimated  = Scheduler_GetLatestRPM();
    result.mahalanobis_D2 = 0.0f;
    strncpy(result.diagnosis_label, "N/A", sizeof(result.diagnosis_label) - 1);
    result.diagnosis_label[sizeof(result.diagnosis_label) - 1] = '\0';
    result.diagnosis_confidence = 0.0f;

    strncpy(result.ml_label, "N/A", sizeof(result.ml_label)-1);
    result.ml_label[sizeof(result.ml_label)-1] = '\0';
    result.ml_confidence = 0.0f;
    strncpy(result.trend, "Mengumpulkan", sizeof(result.trend)-1);
    strncpy(result.servis_estimasi, "30+ hari", sizeof(result.servis_estimasi)-1);
    result.health_score = 100.0f;
    bool calibrationTimeUp = (millis() - calibrationStartMillis) >= CALIBRATION_DURATION_MS;

// Baca command sederhana dari Raspberry Pi/laptop: 1 karakter per command
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'B') {          // 'B' = mesin ini punya rolling bearing
            setBearingType(true);
            Serial.println(F("[CMD] Bearing type: ROLLING"));
        } else if (cmd == 'N') {   // 'N' = mesin ini bushing/no rolling bearing
            setBearingType(false);
            Serial.println(F("[CMD] Bearing type: BUSHING/NONE"));
        } else if (cmd == 'O') {   // 'O' = ground truth OK/normal (kondisi motor asli tanpa fault)
            strncpy(groundTruthLabel, "NORMAL", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: NORMAL"));
        } else if (cmd == 'U') {   // 'U' = ground truth unbalance sengaja dipasang
            strncpy(groundTruthLabel, "UNBALANCE", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: UNBALANCE"));
        } else if (cmd == 'M') {   // 'M' = ground truth misalignment sengaja dipasang
            strncpy(groundTruthLabel, "MISALIGNMENT", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: MISALIGNMENT"));
        } else if (cmd == 'F') {   // 'F' = ground truth bearing fault disimulasikan
            strncpy(groundTruthLabel, "BEARING_FAULT", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: BEARING_FAULT"));
        } else if (cmd == 'L') {   // 'L' = ground truth kurang oli (lubrication fault) sengaja dipasang
            strncpy(groundTruthLabel, "LUBRICATION", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: LUBRICATION"));
        } else if (cmd == 'D') {   // 'D' = ground truth motor DIAM/mati (kelas ke-6 TinyML)
            strncpy(groundTruthLabel, "MATI", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: MATI"));
        } else if (cmd == 'X') {   // 'X' = Raspi minta ESP32 REBOOT PENUH
            Serial.println(F("[CMD] Reboot ESP32 diminta dari Raspi..."));
            delay(150);  // beri waktu buffer Serial TX selesai terkirim SEBELUM restart
            ESP.restart();
        } else if (cmd >= '0' && cmd <= '9') {   // BARU: pilih slot baseline mesin (0-5)
            selectMachineBaselineSlot(cmd - '0');
        } else if (cmd == 'R') {   // 'R' = trigger kalibrasi ulang, TANPA reboot/putus koneksi
            Serial.println(F("[CMD] Kalibrasi ulang diminta dari Raspi/laptop..."));
            startCalibrationPhase();
            calibrationStartMillis = millis();
        }
    }

    if (!fresh && stillWarmingUp) {
        strncpy(result.status_label, "Warming", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else if (!fresh) {
        strncpy(result.status_label, "SensorFault", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else if (!isBaselineLearnerReady() && !calibrationTimeUp) {
        // FIX: gerbang kalibrasi berbasis WAKTU NYATA (millis()), bukan jumlah
        // sample -- rate loop() terbukti tidak konstan di lapangan. Semua
        // sample yang berhasil ditangkap dalam jendela 180 detik ini dipakai,
        // sebanyak apapun jumlahnya (tergantung rate riil setelah fix DriverArus).
        addCalibrationSample(merged);
        addSNRCalibrationSample(Scheduler_GetLatestSNR());

        float bandEnergies[4];
        Scheduler_GetLatestBandEnergies(bandEnergies);
        addBandEnergyCalibrationSample(bandEnergies);

        float audioBandEnergies[AUDIO_BAND_COUNT];
        Scheduler_GetLatestAudioBandEnergies(audioBandEnergies);
        addAudioBandEnergyCalibrationSample(audioBandEnergies);

        strncpy(result.status_label, "Calibrating", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else if (!isBaselineLearnerReady()) {
        float mean[3], stdDev[3], sigmaInv[3][3];
        computeInitialBaseline(mean, sigmaInv);

        if (isLastCalibrationValid()) {
            getFeatureStdDev(stdDev);
            initializeBaselineLearner(mean, stdDev, sigmaInv);
            saveBaselineToFlash(currentMachineSlot >= 0 ? currentMachineSlot : 0, mean, sigmaInv, stdDev);

            // TAMBAHAN: baseline band frekuensi sekarang dihitung dari data
            // kalibrasi NYATA, bukan placeholder 0.20/0.10 selamanya.
            float bandMean[4], bandStd[4];
            computeBandEnergyBaseline(bandMean, bandStd);
            setDiagnosisBandBaseline(bandMean, bandStd);
            saveBandBaselineToFlash(currentMachineSlot >= 0 ? currentMachineSlot : 0, bandMean, bandStd);

            float audioMean[AUDIO_BAND_COUNT], audioStd[AUDIO_BAND_COUNT];
            computeAudioBandBaseline(audioMean, audioStd);
            setAudioBandBaseline(audioMean, audioStd);
            saveAudioBandBaselineToFlash(currentMachineSlot >= 0 ? currentMachineSlot : 0, audioMean, audioStd);

            setRuntimeSNRThreshold(computeSNRThresholdFromCalibration());
            Serial.println(F("[SYSTEM] Kalibrasi VALID. Baseline mean/sigma dan band energy siap."));
        } else {
            Serial.println(F("[SYSTEM] Kalibrasi GAGAL (varians terlalu rendah). Mengulang 180 detik..."));
            startCalibrationPhase();
            calibrationStartMillis = millis();
        }
        strncpy(result.status_label, "Calibrating", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else {
        result = runDetectionCycle();
        // Setelah result = runDetectionCycle();

        // Health Score
        float hs = 100.0f - (result.mahalanobis_D2 / getChiSquare99()) * 100.0f;
        result.health_score = constrain(hs, 0.0f, 100.0f);

        // Trend
        static float sevHistory[30] = {0};
        static int sevIdx = 0;
        sevHistory[sevIdx++ % 30] = result.mahalanobis_D2;
        if (sevIdx >= 20) {
            float recent = 0, older = 0;
            for (int i = 0; i < 10; i++) {
                recent += sevHistory[(sevIdx-1-i+30)%30];
                older  += sevHistory[(sevIdx-11-i+30)%30];
            }
            recent /= 10; older /= 10;
            if      (recent > older * 1.15f) strncpy(result.trend, "Memburuk", 15);
            else if (recent < older * 0.85f) strncpy(result.trend, "Membaik",  15);
            else                             strncpy(result.trend, "Stabil",   15);
        } else {
            strncpy(result.trend, "Mengumpulkan", 15);
        }

        // Estimasi Servis
        if      (result.health_score > 80) strncpy(result.servis_estimasi, "30+ hari",     31);
        else if (result.health_score > 60) strncpy(result.servis_estimasi, "14-30 hari",   31);
        else if (result.health_score > 40) strncpy(result.servis_estimasi, "7-14 hari",    31);
        else if (result.health_score > 20) strncpy(result.servis_estimasi, "1-7 hari",     31);
        else                               strncpy(result.servis_estimasi, "SEGERA SERVIS",31);

        // TinyML
        TinyML_Update(merged, result.rpm_estimated);
        if (TinyML_HasNewResult()) {
            strncpy(result.ml_label, TinyML_GetLabel(), 15);
            result.ml_confidence = TinyML_GetConfidence();
        }
    }


    Transmitter_SendResult(merged, result, groundTruthLabel);

#if DEBUG_BAND_ENERGY_MODE
        // Nyalakan mode ini SEMENTARA saat mesin dalam kondisi NORMAL untuk
        // mengumpulkan angka mean & std band energy yang benar. Matikan lagi
        // (kembalikan ke 0) setelah bandBaselineMean/Std di atas sudah diisi
        // angka hasil kalibrasi manual.
        Serial.printf("[BAND_ENERGY] E0=%.4f E1=%.4f E2=%.4f E3=%.4f\n",
            bandEnergies[0], bandEnergies[1], bandEnergies[2], bandEnergies[3]);
#endif
#if DEBUG_VERBOSE
    #if PLOTTER_MODE
        Serial.printf("Suhu:%.2f Arus:%.4f Getaran:%.4f Suara:%.6f Status:%s\n",
            merged.suhu, merged.arus, merged.rms_getaran, merged.rms_suara, result.status_label);
    #else
        Serial.printf("\n================= TELEMETRI MONITORING =================");
        Serial.printf("\nRPM ESTIMATED : %7.2f RPM", result.rpm_estimated);
        Serial.printf("\nANOMALY STATE : %s (Mahalanobis D2=%.3f, baseline self-calibrated)", result.status_label, result.mahalanobis_D2);
        Serial.printf("\n------------------- DATA MENTAH SENSOR -----------------");
        Serial.printf("\nGETARAN (RMS) : %7.4f", merged.rms_getaran);
        Serial.printf("\nSUARA (RMS)   : %7.2f", merged.rms_suara);
        Serial.printf("\nARUS MOTOR    : %7.4f A", merged.arus);
        Serial.printf("\nSUHU OPERASI  : %7.2f C", merged.suhu);
        Serial.printf("\n========================================================\n");
    #endif
#endif
    vTaskDelay(pdMS_TO_TICKS(TICK_DELAY_REPORT));
}
