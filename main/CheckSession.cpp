#include "CheckSession.h"
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static unsigned long sessionStartMillis = 0;
static bool sessionActive = false;
static int  currentSessionSlot = -1;
static int countNormal, countWaspada, countBahaya, countDiam;
static int countUnbalance, countMisalign, countBpfo, countBpfi;
static float sumHealthScore;
static int sampleCount;
static float tempAtStart, tempAtEnd;

static Preferences checkFlashStorage;

void startCheckSession(int slot) {
    sessionStartMillis = millis();
    sessionActive = true;
    currentSessionSlot = slot;
    countNormal = countWaspada = countBahaya = countDiam = 0;
    countUnbalance = countMisalign = countBpfo = countBpfi = 0;
    sumHealthScore = 0.0f;
    sampleCount = 0;
    tempAtStart = -999.0f;
    Serial.printf("[CheckSession] Sesi cek dimulai untuk slot #%d (1 menit)...\n", slot);
}

void updateCheckSession(DetectionResult result, float currentTemp) {
    if (!sessionActive) return;

    if (tempAtStart < -900.0f) tempAtStart = currentTemp;
    tempAtEnd = currentTemp;

    if (strcmp(result.status_label, "Normal") == 0) countNormal++;
    else if (strcmp(result.status_label, "Waspada") == 0) countWaspada++;
    else if (strcmp(result.status_label, "Bahaya") == 0) countBahaya++;
    else if (strcmp(result.status_label, "Diam") == 0) countDiam++;

    if (strcmp(result.diagnosis_label, "UNBALANCE") == 0) countUnbalance++;
    else if (strcmp(result.diagnosis_label, "MISALIGNMENT") == 0) countMisalign++;
    else if (strcmp(result.diagnosis_label, "BEARING_BPFO") == 0) countBpfo++;
    else if (strcmp(result.diagnosis_label, "BEARING_BPFI") == 0) countBpfi++;

    sumHealthScore += result.health_score;
    sampleCount++;

    if (millis() - sessionStartMillis >= CHECK_SESSION_DURATION_MS) {
        sessionActive = false;
        Serial.println(F("[CheckSession] Waktu habis, sesi selesai."));
    }
}

bool isCheckSessionActive() { return sessionActive; }

CheckSessionSummary getCheckSessionSummary() {
    CheckSessionSummary s;
    s.slot = currentSessionSlot;
    s.count_normal = countNormal;
    s.count_waspada = countWaspada;
    s.count_bahaya = countBahaya;
    s.count_diam = countDiam;
    s.count_diagnosis_unbalance = countUnbalance;
    s.count_diagnosis_misalign = countMisalign;
    s.count_diagnosis_bpfo = countBpfo;
    s.count_diagnosis_bpfi = countBpfi;

    const char* dominant = "Normal";
    int maxCount = countNormal;
    if (countBahaya > maxCount)  { dominant = "Bahaya";  maxCount = countBahaya; }
    if (countWaspada > maxCount) { dominant = "Waspada"; maxCount = countWaspada; }
    if (countDiam > maxCount)    { dominant = "Diam";    maxCount = countDiam; }
    strncpy(s.dominant_status, dominant, sizeof(s.dominant_status) - 1);
    s.dominant_status[sizeof(s.dominant_status) - 1] = '\0';

    s.avg_health_score = (sampleCount > 0) ? (sumHealthScore / sampleCount) : 0.0f;
    s.temp_start = tempAtStart;
    s.temp_end = tempAtEnd;
    s.temp_delta = tempAtEnd - tempAtStart;
    s.duration_ms = CHECK_SESSION_DURATION_MS;
    s.total_samples = sampleCount;
    return s;
}

void saveCheckSummaryToFlash(CheckSessionSummary s) {
    char ns[16];
    snprintf(ns, sizeof(ns), "check%d", s.slot);
    checkFlashStorage.begin(ns, false);
    checkFlashStorage.putBytes("summary", &s, sizeof(CheckSessionSummary));
    checkFlashStorage.end();
    Serial.printf("[CheckSession] Hasil cek slot #%d tersimpan ke flash.\n", s.slot);
}

bool loadCheckSummaryFromFlash(int slot, CheckSessionSummary *out) {
    char ns[16];
    snprintf(ns, sizeof(ns), "check%d", slot);
    checkFlashStorage.begin(ns, true);
    size_t len = checkFlashStorage.getBytesLength("summary");
    if (len != sizeof(CheckSessionSummary)) {
        checkFlashStorage.end();
        return false;
    }
    checkFlashStorage.getBytes("summary", out, len);
    checkFlashStorage.end();
    return true;
}

void deleteCheckSummaryFromFlash(int slot) {
    char ns[16];
    snprintf(ns, sizeof(ns), "check%d", slot);
    checkFlashStorage.begin(ns, false);
    checkFlashStorage.clear();
    checkFlashStorage.end();
}