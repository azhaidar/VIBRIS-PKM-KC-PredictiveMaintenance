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
#include "CheckSession.h"
#include "FactoryPresets.h"   // BARU (20 Agustus 2026): preset pabrikan Mesin 1/2, lihat file itu utk cara isi

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
#define CALIBRATION_DURATION_MS 180000UL 

static unsigned long bootMillis = 0;
static char groundTruthLabel[16] = "NORMAL";

static float bandBaselineMean[4] = {0.20f, 0.20f, 0.20f, 0.20f};
static float bandBaselineStd[4]  = {0.10f, 0.10f, 0.10f, 0.10f};
 
static unsigned long calibrationStartMillis = 0;
static int currentMachineSlot = 0;   // BARU: -1 = belum ada mesin dipilih
static int currentRegime = 0;  
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
        xTaskCreatePinnedToCore(TaskDriverArus, "Task_Arus", STACK_TASK_ARUS, NULL, PRIO_TASK_ARUS, NULL, CORE_DSP_HIGH_SPEED);
    #endif

    xTaskCreatePinnedToCore(TaskDriverSuhu, "Task_Suhu", STACK_TASK_SUHU, NULL, PRIO_TASK_SUHU, NULL, CORE_SYSTEM_SLOW_IO);

    bootMillis = millis();

    // BARU (27 Agustus 2026): Inisialisasi baseline saat boot berdasarkan Slot 0 Regime 0
    // Logika:
    //   Jika Slot 0 Regime 0 punya preset pabrikan → load preset, skip kalibrasi
    //   Jika Slot 0 Regime 0 punya baseline di flash → load dari flash, skip kalibrasi
    //   Jika tidak ada sama-sama → mulai kalibrasi 180 detik
    // Tujuan: Device siap deteksi dari detik pertama jika Slot 0/1 sudah punya preset
    if (isPresetLockedSlot(0, 0)) {
        applyMachineBaseline(0, 0);  // Load preset pabrikan Mesin 1, skip kalibrasi
    } else {
        // Cek flash (temporary array buat cek existence saja)
        float tempMean[3], tempSigmaInv[3][3], tempStdDev[3];
        if (loadBaselineFromFlash(0, tempMean, tempSigmaInv, tempStdDev, 0)) {
            applyMachineBaseline(0, 0);  // Preset tidak ada, tapi flash ada → load dari flash
        } else {
            // Tidak ada preset, tidak ada di flash → kalibrasi dari awal
            startCalibrationPhase();
            calibrationStartMillis = millis();
            Serial.println(F("[SYSTEM] Belum ada baseline untuk Slot 0 Regime 0. Memulai fase kalibrasi self-baseline (180 detik nyata)."));
        }
    }

    Serial.println(F("[SYSTEM] Boot Complete."));

}

static void printFactoryPresetExport(int slot, int regime, float mean[3], float sigmaInv[3][3], float stdDev[3],
                                      float bandMean[4], float bandStd[4],
                                      float audioMean[AUDIO_BAND_COUNT], float audioStd[AUDIO_BAND_COUNT]) {
    Serial.println(F("\n[EXPORT PRESET] Kalau kalibrasi ini BERSIH (motor sudah stabil sebelum mulai),"));
    Serial.println(F("[EXPORT PRESET] data untuk dimasukan ke ke FactoryPresets.h:"));
    Serial.printf("// ---- Export slot #%d regime #%d ----\n", slot, regime);
    Serial.printf("static float preset_mean[3] = {%.6ff, %.6ff, %.6ff};\n", mean[0], mean[1], mean[2]);
    Serial.printf("static float preset_sigmaInv[3][3] = {{%.6ff,%.6ff,%.6ff},{%.6ff,%.6ff,%.6ff},{%.6ff,%.6ff,%.6ff}};\n",
        sigmaInv[0][0], sigmaInv[0][1], sigmaInv[0][2],
        sigmaInv[1][0], sigmaInv[1][1], sigmaInv[1][2],
        sigmaInv[2][0], sigmaInv[2][1], sigmaInv[2][2]);
    Serial.printf("static float preset_stdDev[3] = {%.6ff, %.6ff, %.6ff};\n", stdDev[0], stdDev[1], stdDev[2]);
    Serial.printf("static float preset_bandMean[4] = {%.6ff, %.6ff, %.6ff, %.6ff};\n", bandMean[0], bandMean[1], bandMean[2], bandMean[3]);
    Serial.printf("static float preset_bandStd[4] = {%.6ff, %.6ff, %.6ff, %.6ff};\n", bandStd[0], bandStd[1], bandStd[2], bandStd[3]);
    Serial.printf("static float preset_audioMean[AUDIO_BAND_COUNT] = {%.6ff, %.6ff, %.6ff};\n", audioMean[0], audioMean[1], audioMean[2]);
    Serial.printf("static float preset_audioStd[AUDIO_BAND_COUNT] = {%.6ff, %.6ff, %.6ff};\n", audioStd[0], audioStd[1], audioStd[2]);
    Serial.println(F("[EXPORT PRESET] Selesai.\n"));
}

bool isPresetLockedSlot(int slot, int regime) {
    if (slot == 0 && regime == 0 && FACTORY_PRESET_MESIN1_READY) return true;
    if (slot == 0 && regime == 1 && FACTORY_PRESET_MESIN1_REGIME1_READY) return true;
    if (slot == 0 && regime == 2 && FACTORY_PRESET_MESIN1_REGIME2_READY) return true;
    if (slot == 1 && regime == 0 && FACTORY_PRESET_MESIN2_READY) return true;
    return false;
}

static void applyMachineBaseline(int slot, int regime) {
    float mean[3], sigmaInv[3][3], stdDev[3];
    if (slot == 0 && regime == 0 && FACTORY_PRESET_MESIN1_READY) {
        setFeatureStdDev(presetMesin1_stdDev);
        initializeBaselineLearner(presetMesin1_mean, presetMesin1_stdDev, presetMesin1_sigmaInv);
        setDiagnosisBandBaseline(presetMesin1_bandMean, presetMesin1_bandStd);
        setAudioBandBaseline(presetMesin1_audioMean, presetMesin1_audioStd);
        Serial.println(F("[SYSTEM] Preset pabrikan Mesin 1 dimuat -- deteksi langsung aktif TANPA kalibrasi."));
    }
    else if (slot == 0 && regime == 1 && FACTORY_PRESET_MESIN1_REGIME1_READY) {
        setFeatureStdDev(presetMesin1Regime1_stdDev);
        initializeBaselineLearner(presetMesin1Regime1_mean, presetMesin1Regime1_stdDev, presetMesin1Regime1_sigmaInv);
        setDiagnosisBandBaseline(presetMesin1Regime1_bandMean, presetMesin1Regime1_bandStd);
        setAudioBandBaseline(presetMesin1Regime1_audioMean, presetMesin1Regime1_audioStd);
        Serial.println(F("[SYSTEM] Preset pabrikan Mesin 1 regime 1 (pulley kecil) dimuat -- deteksi langsung aktif TANPA kalibrasi."));
    }
    // BARU (20 Agustus 2026): regime 2 (pulley besar) -- pola sama persis
    // kayak regime 1 di atas.
    else if (slot == 0 && regime == 2 && FACTORY_PRESET_MESIN1_REGIME2_READY) {
        setFeatureStdDev(presetMesin1Regime2_stdDev);
        initializeBaselineLearner(presetMesin1Regime2_mean, presetMesin1Regime2_stdDev, presetMesin1Regime2_sigmaInv);
        setDiagnosisBandBaseline(presetMesin1Regime2_bandMean, presetMesin1Regime2_bandStd);
        setAudioBandBaseline(presetMesin1Regime2_audioMean, presetMesin1Regime2_audioStd);
        Serial.println(F("[SYSTEM] Preset pabrikan Mesin 1 regime 2 (pulley besar) dimuat -- deteksi langsung aktif TANPA kalibrasi."));
    }
    else if (slot == 1 && regime == 0 && FACTORY_PRESET_MESIN2_READY) {
        setFeatureStdDev(presetMesin2_stdDev);
        initializeBaselineLearner(presetMesin2_mean, presetMesin2_stdDev, presetMesin2_sigmaInv);
        setDiagnosisBandBaseline(presetMesin2_bandMean, presetMesin2_bandStd);
        setAudioBandBaseline(presetMesin2_audioMean, presetMesin2_audioStd);
        Serial.println(F("[SYSTEM] Preset pabrikan Mesin 2 dimuat -- deteksi langsung aktif TANPA kalibrasi."));
    }
    // Slot yang TIDAK dikunci preset (misal slot 2-9, atau slot 0/1 sebelum
    // preset-nya diisi READY): baru di sini cek flash -- baseline dari
    // kalibrasi 'R' terakhir kali slot ini dipakai.
    else if (loadBaselineFromFlash(slot, mean, sigmaInv, stdDev, regime)) {
        setFeatureStdDev(stdDev);
        initializeBaselineLearner(mean, stdDev, sigmaInv);

        float bandMean[4], bandStd[4];
        if (loadBandBaselineFromFlash(slot, bandMean, bandStd, regime)) {
            setDiagnosisBandBaseline(bandMean, bandStd);
        }
        float audioMean[AUDIO_BAND_COUNT], audioStd[AUDIO_BAND_COUNT];
        if (loadAudioBandBaselineFromFlash(slot, audioMean, audioStd, regime)) {
            setAudioBandBaseline(audioMean, audioStd);
        }
        Serial.printf("[SYSTEM] Baseline mesin #%d regime #%d dimuat dari flash -- deteksi langsung aktif.\n", slot, regime);
    }
    else {
        resetBaselineLearner();
        resetDiagnosisBandBaseline();
        Serial.printf("[SYSTEM] Belum ada baseline utk mesin #%d regime #%d. Mulai kalibrasi baru (180 detik)...\n", slot, regime);
        startCalibrationPhase();
        calibrationStartMillis = millis();
    }
}

void selectMachineBaselineSlot(int slot) {
    if (slot == currentMachineSlot) return;   // sudah di mesin ini, gak perlu ngapa-ngapain
    currentMachineSlot = slot;
    applyMachineBaseline(currentMachineSlot, currentRegime);

    setBearingCluster(slot);
}

void selectRegime(int regime) {
    if (regime == currentRegime) return;   // udah di regime ini, gak perlu ngapa-ngapain
    currentRegime = regime;
    applyMachineBaseline(currentMachineSlot, currentRegime);
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
        } else if (cmd >= 'a' && cmd <= 'j') {   // BARU (20 Agustus 2026): pilih regime/kondisi
            // operasi (0-9) DALAM mesin yang lagi aktif -- huruf kecil sengaja dipakai
            // biar gak ketuker sama command huruf besar yang udah ada ('B','N',dst).
            // 'a'=regime 0 (default/sama kayak sebelum fitur ini ada), 'b'=regime 1, dst.
            selectRegime(cmd - 'a');
        } else if (cmd == 'R') {   // 'R' = trigger kalibrasi ulang, TANPA reboot/putus koneksi
            // FIX (25 Agustus 2026): slot yang udah dikunci preset (lihat
            // isPresetLockedSlot() di atas) DITOLAK di sini -- supaya nggak
            // ada lagi kejadian kalibrasi 1 sesi cepat (bisa aja motor lagi
            // nggak stabil/mounting goyang) diam-diam nimpa preset yang udah
            // divalidasi hati-hati. Kalau memang mau update preset slot ini,
            // caranya SENGAJA dibikin manual: edit angka baru langsung ke
            // FactoryPresets.h + reflash -- bukan lewat command serial.
            if (isPresetLockedSlot(currentMachineSlot, currentRegime)) {
                Serial.printf("[CMD] Kalibrasi ulang DITOLAK -- slot #%d regime #%d dikunci ke preset pabrikan "
                              "(FactoryPresets.h). Preset ini cuma boleh diganti lewat edit kode manual + reflash, "
                              "supaya kalibrasi barunya sempat direview dulu sebelum dipercaya. Kalau memang mau "
                              "kalibrasi ulang slot ini, kumpulkan datanya dulu (bisa pakai slot kosong lain buat uji "
                              "coba), baru masukkan manual ke FactoryPresets.h kalau hasilnya sudah bersih.\n",
                              currentMachineSlot, currentRegime);
            } else if (isCheckSessionActive()) {
                Serial.println(F("[CMD] Kalibrasi ulang DITOLAK -- sesi Check sedang berjalan, tunggu selesai (1 menit) dulu."));
            } else {
                Serial.println(F("[CMD] Kalibrasi ulang diminta dari Raspi/laptop..."));
                // FIX (20 Agustus 2026): sebelumnya resetBaselineLearner() di
                // bawah ini TIDAK ADA. Akibatnya isBaselineLearnerReady() tetap
                // TRUE (baseline LAMA masih dianggap "siap" di memori), jadi
                // cabang "Calibrating" di loop() (butuh !isBaselineLearnerReady())
                // TIDAK PERNAH aktif -- sistem malah langsung jalanin deteksi
                // pakai baseline LAMA, makanya status lompat ke Waspada/Bahaya
                // walau kamu baru pencet "Kalibrasi Ulang", dan sample kalibrasi
                // baru TIDAK PERNAH terkumpul sama sekali.
                resetBaselineLearner();
                resetDiagnosisBandBaseline();
                startCalibrationPhase();
                calibrationStartMillis = millis();
            }
        } else if (cmd == 'K') {
            if (!isBaselineLearnerReady()) {
                Serial.println(F("[CMD] Mulai Check DITOLAK -- sistem masih dalam fase kalibrasi, tunggu selesai dulu."));
            } else {
                startCheckSession(currentMachineSlot);
            }
        } else if (cmd == 'P') {
            CheckSessionSummary lastResult;
            if (loadCheckSummaryFromFlash(currentMachineSlot, &lastResult)) {
                Serial.printf("[SLOT #%d] Cek terakhir: %s | Normal=%d Waspada=%d Bahaya=%d Diam=%d | Health=%.1f\n",
                    lastResult.slot, lastResult.dominant_status,
                    lastResult.count_normal, lastResult.count_waspada,
                    lastResult.count_bahaya, lastResult.count_diam, lastResult.avg_health_score);
            } else {
                Serial.printf("[SLOT #%d] Belum pernah ada hasil cek tersimpan.\n", currentMachineSlot);
            }
        } else if (cmd == 'Z') {
            // FIX (25 Agustus 2026): ditolak juga di slot yang dikunci preset
            // -- sejak fix di atas, preset SELALU menang di slot ini apapun
            // isi flash-nya, jadi 'Z' di sini nggak akan ngubah apa-apa
            // secara nyata (cuma bikin bingung karena user ngira ini
            // ngapa-ngapain). Tolak dari awal biar jelas kenapa gak
            // ngefek, daripada diam-diam gak berasa apa-apa.
            if (isPresetLockedSlot(currentMachineSlot, currentRegime)) {
                Serial.printf("[CMD] Hapus baseline DITOLAK -- slot #%d regime #%d dikunci ke preset pabrikan, "
                              "jadi flash-nya nggak dipakai sama sekali (lihat isPresetLockedSlot()). "
                              "Nggak ada yang perlu dihapus di sini.\n",
                              currentMachineSlot, currentRegime);
                return;
            }
            deleteBaselineFromFlash(currentMachineSlot, currentRegime);
            deleteCheckSummaryFromFlash(currentMachineSlot);
            resetBaselineLearner();
            resetDiagnosisBandBaseline();
            Serial.printf("[CMD] Baseline slot #%d regime #%d DIHAPUS (riwayat cek slot ikut kehapus). Perlu kalibrasi ulang.\n", currentMachineSlot, currentRegime);
        } else if (cmd == 'V') {
            setBearingCluster(0);   // Klaster A ~1400RPM
        } else if (cmd == 'W') {
            setBearingCluster(1);   // Klaster B ~2800RPM
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
            saveBaselineToFlash(currentMachineSlot >= 0 ? currentMachineSlot : 0, mean, sigmaInv, stdDev, currentRegime);

            // TAMBAHAN: baseline band frekuensi sekarang dihitung dari data
            // kalibrasi NYATA, bukan placeholder 0.20/0.10 selamanya.
            float bandMean[4], bandStd[4];
            computeBandEnergyBaseline(bandMean, bandStd);
            setDiagnosisBandBaseline(bandMean, bandStd);
            saveBandBaselineToFlash(currentMachineSlot >= 0 ? currentMachineSlot : 0, bandMean, bandStd, currentRegime);

            float audioMean[AUDIO_BAND_COUNT], audioStd[AUDIO_BAND_COUNT];
            computeAudioBandBaseline(audioMean, audioStd);
            setAudioBandBaseline(audioMean, audioStd);
            saveAudioBandBaselineToFlash(currentMachineSlot >= 0 ? currentMachineSlot : 0, audioMean, audioStd, currentRegime);

            setRuntimeSNRThreshold(computeSNRThresholdFromCalibration());
            Serial.println(F("[SYSTEM] Kalibrasi VALID. Baseline mean/sigma dan band energy siap."));

            // BARU (20 Agustus 2026): cetak hasil kalibrasi ini dalam format
            // kode C++ siap-copas -- BUKAN buat dipakai otomatis, cuma biar
            // kamu bisa salin manual ke FactoryPresets.h kalau kalibrasi
            // INI beneran bersih (motor udah stabil sebelum mulai). Cek
            // printFactoryPresetExport() di bawah loop() buat detailnya.
            // FIX (20 Agustus 2026): sebelumnya baris ini kirim currentMachineSlot
            // MENTAH-MENTAH -- kalau belum pernah pilih slot (masih -1, nilai
            // default), komentar hasil export jadi salah nyebut "slot #-1"
            // padahal baseline-nya BENERAN kesimpen di slot #0 (lihat
            // saveBaselineToFlash 3 baris di atas, yang sudah pakai fallback
            // ">= 0 ? ... : 0"). Baris ini disamain fallback-nya biar labelnya
            // gak nyasar, walau datanya sendiri sebenarnya sudah benar dari awal.
            printFactoryPresetExport(currentMachineSlot >= 0 ? currentMachineSlot : 0, currentRegime,
                mean, sigmaInv, stdDev, bandMean, bandStd, audioMean, audioStd);
        } else {
            Serial.println(F("[SYSTEM] Kalibrasi GAGAL (varians terlalu rendah). Mengulang 180 detik..."));
            startCalibrationPhase();
            calibrationStartMillis = millis();
        }
        strncpy(result.status_label, "Calibrating", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else {
        result = runDetectionCycle();

        // Health Score
        // FIX (21 Agustus 2026): blok ini dipindah ke ATAS updateCheckSession().
        // Sebelumnya updateCheckSession() dipanggil DULUAN, padahal saat itu
        // result.health_score masih 0.0 (nilai default dari runDetectionCycle()
        // yang baru saja menimpa 'result' sepenuhnya) -- health_score yang
        // BENAR baru dihitung 2 baris di bawahnya. Akibatnya CheckSession.cpp
        // selalu menjumlahkan 0.0, jadi avg_health_score di ringkasan sesi
        // SELALU 0.0 walau motor sehat. Fix: hitung health_score DULU, baru
        // panggil updateCheckSession() supaya dia membaca angka yang sudah jadi.
        float hs = 100.0f - (result.mahalanobis_D2 / getChiSquare99()) * 100.0f;
        result.health_score = constrain(hs, 0.0f, 100.0f);

        if (isCheckSessionActive()) {
            updateCheckSession(result, merged.suhu);   // 'merged' sesuaikan nama variabel aslinya
        }

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
        strncpy(result.ml_label, TinyML_GetLabel(), 15);
        result.ml_label[15] = '\0';
        result.ml_confidence = TinyML_GetConfidence();

    }
    Transmitter_SendResult(merged, result, groundTruthLabel);
    static bool wasSessionActive = false; 
    if (wasSessionActive && !isCheckSessionActive()) {
        CheckSessionSummary summary = getCheckSessionSummary();
        Transmitter_SendSessionSummary(summary);
        saveCheckSummaryToFlash(summary);
    }
    wasSessionActive = isCheckSessionActive();
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
