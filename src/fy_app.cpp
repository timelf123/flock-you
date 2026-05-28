#include "fy_app.h"
#include "fy_patterns.h"
#include "fy_shared.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <string.h>

bool fyGPSIsFresh() {
    return fyGPSValid && (millis() - fyGPSLastUpdate < GPS_STALE_MS);
}

int fyAppGetDetectionCount() {
    int n = 0;
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        n = fyDetCount;
        xSemaphoreGive(fyMutex);
    }
    return n;
}

bool fyAppCopyDetection(int index, FYDetection* out) {
    if (!out) return false;
    bool ok = false;
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (index >= 0 && index < fyDetCount) {
            *out = fyDet[index];
            ok = true;
        }
        xSemaphoreGive(fyMutex);
    }
    return ok;
}

int fyAppGetRavenCount() {
    int n = 0;
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < fyDetCount; i++) {
            if (fyDet[i].isRaven) n++;
        }
        xSemaphoreGive(fyMutex);
    }
    return n;
}

bool fyAppGetGpsStats(int* taggedOut, int* totalOut, bool* gpsValidOut, unsigned long* gpsAgeMsOut) {
    int tagged = 0;
    int total = 0;
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        total = fyDetCount;
        for (int i = 0; i < fyDetCount; i++) {
            if (fyDet[i].hasGPS) tagged++;
        }
        xSemaphoreGive(fyMutex);
    }
    if (taggedOut) *taggedOut = tagged;
    if (totalOut) *totalOut = total;
    if (gpsValidOut) *gpsValidOut = fyGPSIsFresh();
    if (gpsAgeMsOut) {
        *gpsAgeMsOut = fyGPSValid ? (millis() - fyGPSLastUpdate) : 0;
    }
    return true;
}

bool fyAppIsBleActive() {
    return fyBLEScan != nullptr;
}

void fyAppClearDetections() {
    fySaveSession();
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        fyDetCount = 0;
        memset(fyDet, 0, sizeof(FYDetection) * FY_MAX_DETECTIONS);
        fyTriggered = false;
        fyDeviceInRange = false;
        xSemaphoreGive(fyMutex);
    }
}

bool fyAppLoadPrevSession(FYDetection* out, int maxCount, int* countOut) {
    if (!out || maxCount <= 0 || !countOut) return false;
    *countOut = 0;
    if (!fySpiffsReady || !SPIFFS.exists(FY_PREV_FILE)) return false;

    File f = SPIFFS.open(FY_PREV_FILE, "r");
    if (!f) return false;
    String content = f.readString();
    f.close();
    if (content.length() == 0) return false;

    JsonDocument doc;
    if (deserializeJson(doc, content)) return false;
    if (!doc.is<JsonArray>()) return false;

    int n = 0;
    for (JsonObject d : doc.as<JsonArray>()) {
        if (n >= maxCount) break;
        FYDetection& det = out[n];
        memset(&det, 0, sizeof(det));
        strlcpy(det.mac, d["mac"] | "?", sizeof(det.mac));
        strlcpy(det.name, d["name"] | "", sizeof(det.name));
        det.rssi = d["rssi"] | 0;
        strlcpy(det.method, d["method"] | "?", sizeof(det.method));
        det.firstSeen = d["first"] | 0UL;
        det.lastSeen = d["last"] | 0UL;
        det.count = d["count"] | 1;
        det.isRaven = d["raven"] | false;
        strlcpy(det.ravenFW, d["fw"] | "", sizeof(det.ravenFW));
        JsonObject gps = d["gps"];
        if (!gps.isNull() && gps.containsKey("lat")) {
            det.hasGPS = true;
            det.gpsLat = gps["lat"] | 0.0;
            det.gpsLon = gps["lon"] | 0.0;
            det.gpsAcc = gps["acc"] | 0.0f;
        }
        n++;
    }
    *countOut = n;
    return n > 0;
}

void fyAppGetPatternCounts(size_t* flockMacs, size_t* mfrMacs, size_t* stMacs,
                          size_t* names, size_t* mfrIds, size_t* ravenUuids) {
    if (flockMacs) *flockMacs = flock_mac_prefixes_count;
    if (mfrMacs) *mfrMacs = flock_mfr_mac_prefixes_count;
    if (stMacs) *stMacs = soundthinking_mac_prefixes_count;
    if (names) *names = device_name_patterns_count;
    if (mfrIds) *mfrIds = ble_manufacturer_ids_count;
    if (ravenUuids) *ravenUuids = raven_service_uuids_count;
}

const char* const* fyAppGetFlockMacPrefixes(size_t* count) {
    if (count) *count = flock_mac_prefixes_count;
    return flock_mac_prefixes;
}

const char* const* fyAppGetMfrMacPrefixes(size_t* count) {
    if (count) *count = flock_mfr_mac_prefixes_count;
    return flock_mfr_mac_prefixes;
}

const char* const* fyAppGetSoundThinkingMacPrefixes(size_t* count) {
    if (count) *count = soundthinking_mac_prefixes_count;
    return soundthinking_mac_prefixes;
}

const char* const* fyAppGetDeviceNamePatterns(size_t* count) {
    if (count) *count = device_name_patterns_count;
    return device_name_patterns;
}

const uint16_t* fyAppGetBleManufacturerIds(size_t* count) {
    if (count) *count = ble_manufacturer_ids_count;
    return ble_manufacturer_ids;
}

const char* const* fyAppGetRavenServiceUuids(size_t* count) {
    if (count) *count = raven_service_uuids_count;
    return raven_service_uuids;
}
