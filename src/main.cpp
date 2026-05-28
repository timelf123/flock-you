// ============================================================================
// FLOCK-YOU: Surveillance Device Detector with Web Dashboard
// ============================================================================
// Detection methods (BLE only - WiFi radio used for AP):
//   1. BLE MAC prefix matching (known Flock Safety OUIs)
//   2. BLE device name pattern matching (case-insensitive substring)
//   3. BLE manufacturer company ID matching (0x09C8 XUNTONG) [from wgreenberg]
//   4. Raven gunshot detector service UUID matching
//   5. Raven firmware version estimation from service UUID patterns
//
// WiFi AP "flockyou" / "flockyou123" serves web dashboard at 192.168.4.1
// All detections stored in memory, exportable as JSON or CSV
// Optional WiFi STA connection for future features
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_https_server.h"
#include "fy_board.h"
#include "fy_types.h"
#include "fy_patterns.h"
#include "fy_shared.h"
#include "fy_audio.h"
#include "fy_display.h"
#include "fy_ap_cert.h"
#ifdef FY_HAS_DISPLAY
#include "fy_display_hw.h"
#endif

// BLE scanning (mutable — companion mode increases scan duty cycle)
static int fyBleScanDuration = 2;              // seconds per scan
static unsigned long fyBleScanInterval = 3000; // ms between scans

// ============================================================================
// RAVEN SURVEILLANCE DEVICE UUID PATTERNS
// ============================================================================

#define RAVEN_DEVICE_INFO_SERVICE   "0000180a-0000-1000-8000-00805f9b34fb"
#define RAVEN_GPS_SERVICE           "00003100-0000-1000-8000-00805f9b34fb"
#define RAVEN_POWER_SERVICE         "00003200-0000-1000-8000-00805f9b34fb"
#define RAVEN_NETWORK_SERVICE       "00003300-0000-1000-8000-00805f9b34fb"
#define RAVEN_UPLOAD_SERVICE        "00003400-0000-1000-8000-00805f9b34fb"
#define RAVEN_ERROR_SERVICE         "00003500-0000-1000-8000-00805f9b34fb"
#define RAVEN_OLD_HEALTH_SERVICE    "00001809-0000-1000-8000-00805f9b34fb"
#define RAVEN_OLD_LOCATION_SERVICE  "00001819-0000-1000-8000-00805f9b34fb"

// ============================================================================
// DETECTION STORAGE
// ============================================================================

FYDetection fyDet[FY_MAX_DETECTIONS];
int fyDetCount = 0;
SemaphoreHandle_t fyMutex = NULL;

// ============================================================================
// GLOBALS
// ============================================================================

static unsigned long fyLastBleScan = 0;
bool fyTriggered = false;
bool fyDeviceInRange = false;
static unsigned long fyLastDetTime = 0;
static unsigned long fyLastHB = 0;
NimBLEScan* fyBLEScan = NULL;
static bool fyBlePausedForApClient = false;
static bool fyBleStackUp = false;
static httpd_handle_t fyHttpsServer = NULL;
static void fyStartHttpsServer();
static void fyStopHttpsServer();
static void fyInitBleStack();
static void fyDeinitBleStack();

// BLE GATT server (DeFlock app connectivity)
#define FY_SERVICE_UUID     "a1b2c3d4-e5f6-7890-abcd-ef0123456789"
#define FY_TX_CHAR_UUID     "a1b2c3d4-e5f6-7890-abcd-ef01234567aa"
static NimBLEServer*         fyBLEServer = NULL;
static NimBLECharacteristic* fyTxChar    = NULL;
static volatile bool         fyBLEClientConnected = false;
static volatile uint16_t     fyNegotiatedMTU = 23;

// Serial host detection (USB heartbeat from DeFlock desktop)
static volatile bool         fySerialHostConnected = false;
static unsigned long         fyLastSerialHeartbeat = 0;
#define FY_SERIAL_TIMEOUT_MS 5000

// Deferred companion mode switch — BLE callbacks set this flag,
// loop() applies the WiFi/scan changes in the Arduino task context.
static volatile bool         fyCompanionChangePending = false;

// Phone GPS state (updated via browser Geolocation API -> /api/gps)
double fyGPSLat = 0;
double fyGPSLon = 0;
float fyGPSAcc = 0;
bool fyGPSValid = false;
unsigned long fyGPSLastUpdate = 0;

#define FY_SAVE_INTERVAL 15000
static unsigned long fyLastSave = 0;
static int fyLastSaveCount = 0;
bool fySpiffsReady = false;

// ============================================================================
// DETECTION HELPERS
// ============================================================================

static bool checkFlockMAC(const char* mac_str) {
    for (size_t i = 0; i < flock_mac_prefixes_count; i++) {
        if (strncasecmp(mac_str, flock_mac_prefixes[i], 8) == 0) return true;
    }
    return false;
}

static bool checkFlockMfrMAC(const char* mac_str) {
    for (size_t i = 0; i < flock_mfr_mac_prefixes_count; i++) {
        if (strncasecmp(mac_str, flock_mfr_mac_prefixes[i], 8) == 0) return true;
    }
    return false;
}

static bool checkSoundThinkingMAC(const char* mac_str) {
    for (size_t i = 0; i < soundthinking_mac_prefixes_count; i++) {
        if (strncasecmp(mac_str, soundthinking_mac_prefixes[i], 8) == 0) return true;
    }
    return false;
}

static bool checkDeviceName(const char* name) {
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < device_name_patterns_count; i++) {
        if (strcasestr(name, device_name_patterns[i])) return true;
    }
    return false;
}

static bool checkManufacturerID(uint16_t id) {
    for (size_t i = 0; i < ble_manufacturer_ids_count; i++) {
        if (ble_manufacturer_ids[i] == id) return true;
    }
    return false;
}

// ============================================================================
// RAVEN UUID DETECTION
// ============================================================================

static bool checkRavenUUID(NimBLEAdvertisedDevice* device, char* out_uuid = nullptr) {
    if (!device || !device->haveServiceUUID()) return false;
    int count = device->getServiceUUIDCount();
    if (count == 0) return false;
    for (int i = 0; i < count; i++) {
        NimBLEUUID svc = device->getServiceUUID(i);
        std::string str = svc.toString();
        for (size_t j = 0; j < raven_service_uuids_count; j++) {
            if (strcasecmp(str.c_str(), raven_service_uuids[j]) == 0) {
                if (out_uuid) strncpy(out_uuid, str.c_str(), 40);
                return true;
            }
        }
    }
    return false;
}

static const char* estimateRavenFW(NimBLEAdvertisedDevice* device) {
    if (!device || !device->haveServiceUUID()) return "?";
    bool has_new_gps = false, has_old_loc = false, has_power = false;
    int count = device->getServiceUUIDCount();
    for (int i = 0; i < count; i++) {
        std::string u = device->getServiceUUID(i).toString();
        if (strcasecmp(u.c_str(), RAVEN_GPS_SERVICE) == 0)          has_new_gps = true;
        if (strcasecmp(u.c_str(), RAVEN_OLD_LOCATION_SERVICE) == 0) has_old_loc = true;
        if (strcasecmp(u.c_str(), RAVEN_POWER_SERVICE) == 0)        has_power = true;
    }
    if (has_old_loc && !has_new_gps) return "1.1.x";
    if (has_new_gps && !has_power)   return "1.2.x";
    if (has_new_gps && has_power)    return "1.3.x";
    return "?";
}

// ============================================================================
// GPS HELPERS
// ============================================================================

static void fyAttachGPS(FYDetection& d) {
    if (fyGPSIsFresh()) {
        d.hasGPS = true;
        d.gpsLat = fyGPSLat;
        d.gpsLon = fyGPSLon;
        d.gpsAcc = fyGPSAcc;
    }
}

// ============================================================================
// DETECTION MANAGEMENT
// ============================================================================

static int fyAddDetection(const char* mac, const char* name, int rssi,
                          const char* method, bool isRaven = false,
                          const char* ravenFW = "") {
    if (!fyMutex || xSemaphoreTake(fyMutex, pdMS_TO_TICKS(100)) != pdTRUE) return -1;

    // Update existing by MAC
    for (int i = 0; i < fyDetCount; i++) {
        if (strcasecmp(fyDet[i].mac, mac) == 0) {
            fyDet[i].count++;
            fyDet[i].lastSeen = millis();
            fyDet[i].rssi = rssi;
            if (name && name[0]) {
                strncpy(fyDet[i].name, name, sizeof(fyDet[i].name) - 1);
            }
            // Update GPS on every re-sighting (captures movement)
            fyAttachGPS(fyDet[i]);
            xSemaphoreGive(fyMutex);
            return i;
        }
    }

    // Add new
    if (fyDetCount < FY_MAX_DETECTIONS) {
        FYDetection& d = fyDet[fyDetCount];
        memset(&d, 0, sizeof(d));
        strncpy(d.mac, mac, sizeof(d.mac) - 1);
        // Sanitize name for JSON safety
        if (name) {
            for (int j = 0; j < (int)sizeof(d.name) - 1 && name[j]; j++) {
                d.name[j] = (name[j] == '"' || name[j] == '\\') ? '_' : name[j];
            }
        }
        d.rssi = rssi;
        strncpy(d.method, method, sizeof(d.method) - 1);
        d.firstSeen = millis();
        d.lastSeen = millis();
        d.count = 1;
        d.isRaven = isRaven;
        strncpy(d.ravenFW, ravenFW ? ravenFW : "", sizeof(d.ravenFW) - 1);
        // Attach GPS from phone
        fyAttachGPS(d);
        int idx = fyDetCount++;
        xSemaphoreGive(fyMutex);
        return idx;
    }

    xSemaphoreGive(fyMutex);
    return -1;
}

// ============================================================================
// BLE GATT SERVER (DeFlock companion connectivity)
// ============================================================================

class FYServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        fyBLEClientConnected = true;
        fyCompanionChangePending = true;
    }
    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        fyBLEClientConnected = false;
        fyNegotiatedMTU = 23;
        NimBLEDevice::startAdvertising();
        fyCompanionChangePending = true;
    }
    void onMTUChange(uint16_t mtu, ble_gap_conn_desc* desc) override {
        fyNegotiatedMTU = mtu;
        printf("[FLOCK-YOU] MTU negotiated: %u\n", mtu);
    }
};

static void fySendBLE(const char* data, size_t len) {
    if (!fyBLEClientConnected || !fyTxChar) return;
    uint16_t chunkSize = fyNegotiatedMTU - 3;
    if (chunkSize < 1) chunkSize = 1;
    if (len <= chunkSize) {
        fyTxChar->setValue((const uint8_t*)data, len);
        fyTxChar->notify();
    } else {
        size_t offset = 0;
        while (offset < len) {
            size_t remaining = len - offset;
            size_t send = remaining < chunkSize ? remaining : chunkSize;
            fyTxChar->setValue((const uint8_t*)(data + offset), send);
            fyTxChar->notify();
            offset += send;
        }
    }
}

// ============================================================================
// COMPANION MODE (WiFi AP vs BLE/serial)
// ============================================================================

static void fyOnCompanionChange() {
    if (fyBLEClientConnected || fySerialHostConnected) {
        // Companion mode — disable WiFi AP, boost BLE scanning
        fyStopHttpsServer();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        fyBleScanDuration = 3;
        printf("[FLOCK-YOU] Companion mode: WiFi AP OFF, scan duration %ds\n",
               fyBleScanDuration);
    } else {
        // Standalone mode — re-enable WiFi AP and web dashboard
        WiFi.mode(WIFI_AP);
        delay(100);
        WiFi.softAP(FY_AP_SSID, FY_AP_PASS);
        fyStartHttpsServer();
        fyBleScanDuration = 2;
        printf("[FLOCK-YOU] Standalone mode: WiFi AP ON (%s), scan duration %ds\n",
               FY_AP_SSID, fyBleScanDuration);
    }
}

static void fyUpdateBleForApClients() {
    if (WiFi.getMode() != WIFI_AP) return;
    int stations = WiFi.softAPgetStationNum();
    bool hasApClients = stations > 0;

    if (hasApClients && !fyBlePausedForApClient) {
        fyDeinitBleStack();
        fyBlePausedForApClient = true;
        printf("[FLOCK-YOU] AP client connected (%d) - BLE disabled for HTTPS heap\n", stations);
    } else if (!hasApClients && fyBlePausedForApClient) {
        fyBlePausedForApClient = false;
        fyInitBleStack();
        fyLastBleScan = 0;
        printf("[FLOCK-YOU] AP clients gone - BLE re-enabled\n");
    }
}

// ============================================================================
// BLE SCANNING
// ============================================================================

class FYBLECallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        NimBLEAddress addr = dev->getAddress();
        std::string addrStr = addr.toString();

        // Extract MAC prefix string for OUI checks
        char macPrefix[9];
        snprintf(macPrefix, sizeof(macPrefix), "%.8s", addrStr.c_str());

        int rssi = dev->getRSSI();
        std::string name = dev->haveName() ? dev->getName() : "";

        bool detected = false;
        bool highConfidence = true;
        const char* method = "";
        bool isRaven = false;
        const char* ravenFW = "";

        // 1. Check Flock Safety direct OUIs (high confidence)
        if (checkFlockMAC(macPrefix)) {
            detected = true;
            method = "mac_prefix";
        }

        // 2. Check SoundThinking/ShotSpotter OUIs (high confidence)
        if (!detected && checkSoundThinkingMAC(macPrefix)) {
            detected = true;
            method = "mac_prefix_soundthinking";
        }

        // 3. Check Flock contract manufacturer OUIs (low confidence)
        if (!detected && checkFlockMfrMAC(macPrefix)) {
            detected = true;
            method = "mac_prefix_mfr";
            highConfidence = false;
        }

        // 4. Check BLE device name patterns
        if (!detected && !name.empty() && checkDeviceName(name.c_str())) {
            detected = true;
            method = "device_name";
        }

        // 5. Check BLE manufacturer company IDs (from wgreenberg/flock-you)
        if (!detected) {
            for (int i = 0; i < (int)dev->getManufacturerDataCount(); i++) {
                std::string data = dev->getManufacturerData(i);
                if (data.size() >= 2) {
                    uint16_t code = ((uint16_t)(uint8_t)data[1] << 8) |
                                     (uint16_t)(uint8_t)data[0];
                    if (checkManufacturerID(code)) {
                        detected = true;
                        method = "ble_mfr_id";
                        break;
                    }
                }
            }
        }

        // 6. Check Raven gunshot detector service UUIDs
        if (!detected) {
            char detUUID[41] = {0};
            if (checkRavenUUID(dev, detUUID)) {
                detected = true;
                method = "raven_uuid";
                isRaven = true;
                ravenFW = estimateRavenFW(dev);
            }
        }

        if (detected) {
            int idx = fyAddDetection(addrStr.c_str(), name.c_str(), rssi,
                                     method, isRaven, ravenFW);

            // Human-readable log
            printf("[FLOCK-YOU] DETECTED: %s %s RSSI:%d [%s] count:%d\n",
                   addrStr.c_str(), name.c_str(), rssi, method,
                   idx >= 0 ? fyDet[idx].count : 0);

            // JSON output — build into buffer for serial + BLE
            char gpsBuf[80] = "";
            if (fyGPSIsFresh()) {
                snprintf(gpsBuf, sizeof(gpsBuf),
                    ",\"gps\":{\"latitude\":%.8f,\"longitude\":%.8f,\"accuracy\":%.1f}",
                    fyGPSLat, fyGPSLon, fyGPSAcc);
            }
            char jsonBuf[512];
            int jsonLen = snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"event\":\"detection\",\"detection_method\":\"%s\","
                "\"protocol\":\"bluetooth_le\",\"mac_address\":\"%s\","
                "\"device_name\":\"%s\",\"rssi\":%d,"
                "\"is_raven\":%s,\"raven_fw\":\"%s\"%s}",
                method, addrStr.c_str(), name.c_str(), rssi,
                isRaven ? "true" : "false", isRaven ? ravenFW : "", gpsBuf);
            printf("%s\n", jsonBuf);
            // Append newline for BLE framing and send
            if (jsonLen > 0 && jsonLen < (int)sizeof(jsonBuf) - 1) {
                jsonBuf[jsonLen] = '\n';
                fySendBLE(jsonBuf, jsonLen + 1);
            }

            if (idx >= 0) {
                bool isNew = false;
                if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    isNew = (fyDet[idx].count == 1);
                    xSemaphoreGive(fyMutex);
                }
                if (isNew) fyUiNotifyNewDetection();
            }

            if (!fyTriggered && highConfidence) {
                fyTriggered = true;
                fyDetectBeep();
            }
            if (highConfidence) {
                fyDeviceInRange = true;
                fyLastDetTime = millis();
                fyLastHB = millis();
            }
        }
    }
};

static void fyInitBleStack() {
    if (fyBleStackUp) return;

    NimBLEDevice::init("flockyou");
    NimBLEDevice::setMTU(512);

    fyBLEScan = NimBLEDevice::getScan();
    fyBLEScan->setAdvertisedDeviceCallbacks(new FYBLECallbacks());
    fyBLEScan->setActiveScan(true);
    fyBLEScan->setInterval(100);
    fyBLEScan->setWindow(99);

    fyBLEServer = NimBLEDevice::createServer();
    fyBLEServer->setCallbacks(new FYServerCallbacks());
    NimBLEService* pService = fyBLEServer->createService(FY_SERVICE_UUID);
    fyTxChar = pService->createCharacteristic(
        FY_TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );
    pService->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(FY_SERVICE_UUID);
    pAdv->setName("flockyou");
    pAdv->setScanResponse(true);
    pAdv->start();

    fyBleStackUp = true;
    fyBlePausedForApClient = false;
    fyLastBleScan = 0;
    printf("[FLOCK-YOU] BLE stack initialized\n");
}

static void fyDeinitBleStack() {
    if (!fyBleStackUp) return;
    if (fyBLEScan && fyBLEScan->isScanning()) fyBLEScan->stop();
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(true);
    fyBLEScan = NULL;
    fyBLEServer = NULL;
    fyTxChar = NULL;
    fyBLEClientConnected = false;
    fyNegotiatedMTU = 23;
    fyBleStackUp = false;
    printf("[FLOCK-YOU] BLE stack deinitialized for HTTPS heap\n");
}

// ============================================================================
// JSON HELPER
// ============================================================================

static String fyBuildDetectionsJSON() {
    String out = "[";
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = 0; i < fyDetCount; i++) {
            if (i > 0) out += ",";
            char row[384];
            snprintf(row, sizeof(row),
                     "{\"mac\":\"%s\",\"name\":\"%s\",\"rssi\":%d,\"method\":\"%s\","
                     "\"first\":%lu,\"last\":%lu,\"count\":%d,"
                     "\"raven\":%s,\"fw\":\"%s\"",
                     fyDet[i].mac, fyDet[i].name, fyDet[i].rssi, fyDet[i].method,
                     fyDet[i].firstSeen, fyDet[i].lastSeen, fyDet[i].count,
                     fyDet[i].isRaven ? "true" : "false", fyDet[i].ravenFW);
            out += row;
            if (fyDet[i].hasGPS) {
                char gps[128];
                snprintf(gps, sizeof(gps), ",\"gps\":{\"lat\":%.8f,\"lon\":%.8f,\"acc\":%.1f}",
                         fyDet[i].gpsLat, fyDet[i].gpsLon, fyDet[i].gpsAcc);
                out += gps;
            }
            out += "}";
        }
        xSemaphoreGive(fyMutex);
    }
    out += "]";
    return out;
}

static String fyBuildDetectionsCSV() {
    String out = "mac,name,rssi,method,first_seen_ms,last_seen_ms,count,is_raven,raven_fw,latitude,longitude,gps_accuracy\n";
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = 0; i < fyDetCount; i++) {
            FYDetection& d = fyDet[i];
            char row[384];
            if (d.hasGPS) {
                snprintf(row, sizeof(row),
                         "\"%s\",\"%s\",%d,\"%s\",%lu,%lu,%d,%s,\"%s\",%.8f,%.8f,%.1f\n",
                         d.mac, d.name, d.rssi, d.method, d.firstSeen, d.lastSeen, d.count,
                         d.isRaven ? "true" : "false", d.ravenFW, d.gpsLat, d.gpsLon, d.gpsAcc);
            } else {
                snprintf(row, sizeof(row),
                         "\"%s\",\"%s\",%d,\"%s\",%lu,%lu,%d,%s,\"%s\",,,\n",
                         d.mac, d.name, d.rssi, d.method, d.firstSeen, d.lastSeen, d.count,
                         d.isRaven ? "true" : "false", d.ravenFW);
            }
            out += row;
        }
        xSemaphoreGive(fyMutex);
    }
    return out;
}

static String fyBuildPatternsJSON() {
    String out = "{\"macs\":[";
    for (size_t i = 0; i < flock_mac_prefixes_count; i++) {
        if (i > 0) out += ",";
        out += "\"";
        out += flock_mac_prefixes[i];
        out += "\"";
    }
    out += "],\"macs_mfr\":[";
    for (size_t i = 0; i < flock_mfr_mac_prefixes_count; i++) {
        if (i > 0) out += ",";
        out += "\"";
        out += flock_mfr_mac_prefixes[i];
        out += "\"";
    }
    out += "],\"macs_soundthinking\":[";
    for (size_t i = 0; i < soundthinking_mac_prefixes_count; i++) {
        if (i > 0) out += ",";
        out += "\"";
        out += soundthinking_mac_prefixes[i];
        out += "\"";
    }
    out += "],\"names\":[";
    for (size_t i = 0; i < device_name_patterns_count; i++) {
        if (i > 0) out += ",";
        out += "\"";
        out += device_name_patterns[i];
        out += "\"";
    }
    out += "],\"mfr\":[";
    for (size_t i = 0; i < ble_manufacturer_ids_count; i++) {
        if (i > 0) out += ",";
        out += String(ble_manufacturer_ids[i]);
    }
    out += "],\"raven\":[";
    for (size_t i = 0; i < raven_service_uuids_count; i++) {
        if (i > 0) out += ",";
        out += "\"";
        out += raven_service_uuids[i];
        out += "\"";
    }
    out += "]}";
    return out;
}

// ============================================================================
// SESSION PERSISTENCE (SPIFFS)
// ============================================================================

void fySaveSession() {
    if (!fySpiffsReady || !fyMutex) return;
    if (xSemaphoreTake(fyMutex, pdMS_TO_TICKS(300)) != pdTRUE) return;

    File f = SPIFFS.open(FY_SESSION_FILE, "w");
    if (!f) { xSemaphoreGive(fyMutex); return; }

    f.print("[");
    for (int i = 0; i < fyDetCount; i++) {
        if (i > 0) f.print(",");
        FYDetection& d = fyDet[i];
        f.printf("{\"mac\":\"%s\",\"name\":\"%s\",\"rssi\":%d,\"method\":\"%s\","
                 "\"first\":%lu,\"last\":%lu,\"count\":%d,"
                 "\"raven\":%s,\"fw\":\"%s\"",
                 d.mac, d.name, d.rssi, d.method,
                 d.firstSeen, d.lastSeen, d.count,
                 d.isRaven ? "true" : "false", d.ravenFW);
        if (d.hasGPS) {
            f.printf(",\"gps\":{\"lat\":%.8f,\"lon\":%.8f,\"acc\":%.1f}", d.gpsLat, d.gpsLon, d.gpsAcc);
        }
        f.print("}");
    }
    f.print("]");
    f.close();
    fyLastSaveCount = fyDetCount;
    printf("[FLOCK-YOU] Session saved: %d detections\n", fyDetCount);
    xSemaphoreGive(fyMutex);
}

static void fyPromotePrevSession() {
    // Copy current session to prev_session on boot, then delete original
    // NOTE: SPIFFS.rename() is unreliable on ESP32 — use copy+delete instead
    if (!fySpiffsReady) return;
    if (!SPIFFS.exists(FY_SESSION_FILE)) {
        printf("[FLOCK-YOU] No prior session file to promote\n");
        return;
    }

    File src = SPIFFS.open(FY_SESSION_FILE, "r");
    if (!src) {
        printf("[FLOCK-YOU] Failed to open session file for promotion\n");
        return;
    }
    String data = src.readString();
    src.close();

    if (data.length() == 0) {
        printf("[FLOCK-YOU] Session file empty, skipping promotion\n");
        SPIFFS.remove(FY_SESSION_FILE);
        return;
    }

    // Write to prev_session (overwrite any existing)
    File dst = SPIFFS.open(FY_PREV_FILE, "w");
    if (!dst) {
        printf("[FLOCK-YOU] Failed to create prev_session file\n");
        return;
    }
    dst.print(data);
    dst.close();

    // Delete the old session file so it doesn't get re-promoted next boot
    SPIFFS.remove(FY_SESSION_FILE);
    printf("[FLOCK-YOU] Prior session promoted: %d bytes\n", data.length());
}

// ============================================================================
// KML EXPORT
// ============================================================================

static String fyBuildDetectionsKML() {
    String out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n"
                 "<name>Flock-You Detections</name>\n"
                 "<description>Surveillance device detections with GPS</description>\n"
                 "<Style id=\"det\"><IconStyle><color>ff4489ec</color>"
                 "<scale>1.0</scale></IconStyle></Style>\n"
                 "<Style id=\"raven\"><IconStyle><color>ff4444ef</color>"
                 "<scale>1.2</scale></IconStyle></Style>\n";

    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        for (int i = 0; i < fyDetCount; i++) {
            FYDetection& d = fyDet[i];
            if (!d.hasGPS) continue;
            char row[512];
            snprintf(row, sizeof(row),
                     "<Placemark>\n<name>%s</name>\n<styleUrl>#%s</styleUrl>\n<description><![CDATA[",
                     d.mac, d.isRaven ? "raven" : "det");
            out += row;
            if (d.name[0]) {
                out += "<b>Name:</b> ";
                out += d.name;
                out += "<br/>";
            }
            snprintf(row, sizeof(row),
                     "<b>Method:</b> %s<br/><b>RSSI:</b> %d dBm<br/><b>Count:</b> %d<br/>",
                     d.method, d.rssi, d.count);
            out += row;
            if (d.isRaven) {
                out += "<b>Raven FW:</b> ";
                out += d.ravenFW;
                out += "<br/>";
            }
            snprintf(row, sizeof(row),
                     "<b>Accuracy:</b> %.1f m]]></description>\n"
                     "<Point><coordinates>%.8f,%.8f,0</coordinates></Point>\n"
                     "</Placemark>\n",
                     d.gpsAcc, d.gpsLon, d.gpsLat);
            out += row;
        }
        xSemaphoreGive(fyMutex);
    }
    out += "</Document>\n</kml>";
    return out;
}

static String fyBuildPrevSessionKML(const String& content, int* placedOut = nullptr) {
    if (placedOut) *placedOut = 0;
    String out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n"
                 "<name>Flock-You Prior Session</name>\n"
                 "<description>Surveillance device detections from prior session</description>\n"
                 "<Style id=\"det\"><IconStyle><color>ff4489ec</color>"
                 "<scale>1.0</scale></IconStyle></Style>\n"
                 "<Style id=\"raven\"><IconStyle><color>ff4444ef</color>"
                 "<scale>1.2</scale></IconStyle></Style>\n";

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, content);
    if (!err && doc.is<JsonArray>()) {
        int placed = 0;
        for (JsonObject d : doc.as<JsonArray>()) {
            JsonObject gps = d["gps"];
            if (!gps || !gps.containsKey("lat")) continue;
            bool isRaven = d["raven"] | false;
            char row[768];
            snprintf(row, sizeof(row),
                     "<Placemark><name>%s</name>\n<styleUrl>#%s</styleUrl>\n<description><![CDATA[",
                     d["mac"] | "?", isRaven ? "raven" : "det");
            out += row;
            if (d["name"].is<const char*>() && strlen(d["name"] | "") > 0) {
                out += "<b>Name:</b> ";
                out += (const char*)(d["name"] | "");
                out += "<br/>";
            }
            snprintf(row, sizeof(row),
                     "<b>Method:</b> %s<br/><b>RSSI:</b> %d<br/><b>Count:</b> %d",
                     d["method"] | "?", d["rssi"] | 0, d["count"] | 1);
            out += row;
            if (isRaven && d["fw"].is<const char*>()) {
                out += "<br/><b>Raven FW:</b> ";
                out += (const char*)(d["fw"] | "");
            }
            snprintf(row, sizeof(row),
                     "]]></description>\n<Point><coordinates>%.8f,%.8f,0</coordinates></Point>\n</Placemark>\n",
                     (double)(gps["lon"] | 0.0), (double)(gps["lat"] | 0.0));
            out += row;
            placed++;
        }
        if (placedOut) *placedOut = placed;
    }

    out += "</Document>\n</kml>";
    return out;
}

// ============================================================================
// DASHBOARD HTML
// ============================================================================

static const char FY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>FLOCK-YOU</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
html,body{height:100%;overflow:hidden}
body{font-family:'Courier New',monospace;background:#0a0012;color:#e0e0e0;display:flex;flex-direction:column}
.hd{background:#1a0033;padding:10px 14px;border-bottom:2px solid #ec4899;flex-shrink:0}
.hd h1{font-size:22px;color:#ec4899;letter-spacing:3px}
.hd .sub{font-size:11px;color:#8b5cf6;margin-top:2px}
.st{display:flex;gap:8px;padding:8px 12px;background:rgba(139,92,246,.08);border-bottom:1px solid rgba(139,92,246,.19);flex-shrink:0}
.sc{flex:1;text-align:center;padding:6px;border:1px solid rgba(139,92,246,.25);border-radius:5px}
.sc .n{font-size:22px;font-weight:bold;color:#ec4899}
.sc .l{font-size:10px;color:#8b5cf6;margin-top:2px}
.tb{display:flex;border-bottom:1px solid #8b5cf6;flex-shrink:0}
.tb button{flex:1;padding:9px;text-align:center;cursor:pointer;color:#8b5cf6;border:none;background:none;font-family:inherit;font-size:13px;font-weight:bold;letter-spacing:1px}
.tb button.a{color:#ec4899;border-bottom:2px solid #ec4899;background:rgba(236,72,153,.08)}
.cn{flex:1;overflow-y:auto;padding:10px}
.pn{display:none}.pn.a{display:block}
.det{background:rgba(45,27,105,.4);border:1px solid rgba(139,92,246,.25);border-radius:7px;padding:10px;margin-bottom:8px}
.det .mac{color:#ec4899;font-weight:bold;font-size:14px}
.det .nm{color:#c084fc;font-size:13px;margin-left:4px}
.det .inf{display:flex;flex-wrap:wrap;gap:5px;margin-top:5px;font-size:12px}
.det .inf span{background:rgba(139,92,246,.15);padding:3px 6px;border-radius:4px}
.det .rv{background:rgba(239,68,68,.15)!important;color:#ef4444;font-weight:bold}
.pg{margin-bottom:12px}
.pg h3{color:#ec4899;font-size:14px;margin-bottom:4px;border-bottom:1px solid rgba(139,92,246,.19);padding-bottom:4px}
.pg .it{display:flex;flex-wrap:wrap;gap:4px;font-size:12px}
.pg .it span{background:rgba(139,92,246,.15);padding:3px 6px;border-radius:4px;border:1px solid rgba(139,92,246,.12)}
.btn{display:block;width:100%;padding:10px;margin-bottom:8px;background:#8b5cf6;color:#fff;border:none;border-radius:5px;cursor:pointer;font-family:inherit;font-size:14px;font-weight:bold}
.btn:active{background:#ec4899}
.btn.dng{background:#ef4444}
.empty{text-align:center;color:rgba(139,92,246,.5);padding:28px;font-size:14px}
.sep{border:none;border-top:1px solid rgba(139,92,246,.12);margin:12px 0}
h4{color:#ec4899;font-size:14px;margin-bottom:8px}
</style></head><body>
<div class="hd"><h1>FLOCK-YOU</h1><div class="sub">Surveillance Device Detector &bull; Wardriving + GPS</div></div>
<div class="st">
<div class="sc"><div class="n" id="sT">0</div><div class="l">DETECTED</div></div>
<div class="sc"><div class="n" id="sR">0</div><div class="l">RAVEN</div></div>
<div class="sc"><div class="n" id="sB">ON</div><div class="l">BLE</div></div>
<div class="sc" onclick="reqGPS()" style="cursor:pointer"><div class="n" id="sG" style="font-size:14px">TAP</div><div class="l">GPS</div></div>
</div>
<div class="tb">
<button class="a" onclick="tab(0,this)">LIVE</button>
<button onclick="tab(1,this)">PREV</button>
<button onclick="tab(2,this)">DB</button>
<button onclick="tab(3,this)">TOOLS</button>
</div>
<div class="cn">
<div class="pn a" id="p0">
<div id="dL"><div class="empty">Scanning for surveillance devices...<br>BLE active on all channels</div></div>
</div>
<div class="pn" id="p1"><div id="hL"><div class="empty">Loading prior session...</div></div></div>
<div class="pn" id="p2"><div id="pC">Loading patterns...</div></div>
<div class="pn" id="p3">
<h4>EXPORT DETECTIONS</h4>
<p style="font-size:10px;color:#8b5cf6;margin-bottom:8px">Download current session to import into Flask dashboard</p>
<button class="btn" onclick="location.href='/api/export/json'">DOWNLOAD JSON</button>
<button class="btn" onclick="location.href='/api/export/csv'">DOWNLOAD CSV</button>
<button class="btn" onclick="location.href='/api/export/kml'" style="background:#22c55e">DOWNLOAD KML (GPS MAP)</button>
<hr class="sep">
<h4>PRIOR SESSION</h4>
<button class="btn" onclick="location.href='/api/history/json'" style="background:#6366f1">DOWNLOAD PREV JSON</button>
<button class="btn" onclick="location.href='/api/history/kml'" style="background:#22c55e">DOWNLOAD PREV KML</button>
<hr class="sep">
<button class="btn dng" onclick="if(confirm('Clear all detections?'))fetch('/api/clear').then(()=>refresh())">CLEAR ALL DETECTIONS</button>
</div>
</div>
<script>
let D=[],H=[];
function tab(i,el){document.querySelectorAll('.tb button').forEach(b=>b.classList.remove('a'));document.querySelectorAll('.pn').forEach(p=>p.classList.remove('a'));el.classList.add('a');document.getElementById('p'+i).classList.add('a');if(i===1&&!window._hL)loadHistory();if(i===2&&!window._pL)loadPat();}
function refresh(){fetch('/api/detections').then(r=>r.json()).then(d=>{D=d;render();stats();}).catch(()=>{});}
function render(){const el=document.getElementById('dL');if(!D.length){el.innerHTML='<div class="empty">Scanning for surveillance devices...<br>BLE active on all channels</div>';return;}
D.sort((a,b)=>b.last-a.last);el.innerHTML=D.map(card).join('');}
function stats(){document.getElementById('sT').textContent=D.length;document.getElementById('sR').textContent=D.filter(d=>d.raven).length;
fetch('/api/stats').then(r=>r.json()).then(s=>{let g=document.getElementById('sG');if(s.gps_valid){g.textContent=s.gps_tagged+'/'+s.total;g.style.color='#22c55e';}else{g.textContent='OFF';g.style.color='#ef4444';}}).catch(()=>{});}
function card(d){return '<div class="det"><div class="mac">'+d.mac+(d.name?'<span class="nm">'+d.name+'</span>':'')+'</div><div class="inf"><span>RSSI: '+d.rssi+'</span><span>'+d.method+'</span><span style="color:#ec4899;font-weight:bold">&times;'+d.count+'</span>'+(d.raven?'<span class="rv">RAVEN '+d.fw+'</span>':'')+(d.gps?'<span style="color:#22c55e">&#9673; '+d.gps.lat.toFixed(5)+','+d.gps.lon.toFixed(5)+'</span>':'<span style="color:#666">no gps</span>')+'</div></div>';}
function loadHistory(){fetch('/api/history').then(r=>r.json()).then(d=>{H=d;let el=document.getElementById('hL');if(!H.length){el.innerHTML='<div class="empty">No prior session data</div>';return;}
H.sort((a,b)=>b.last-a.last);el.innerHTML='<div style="font-size:11px;color:#8b5cf6;margin-bottom:8px">'+H.length+' detections from prior session</div>'+H.map(card).join('');window._hL=1;}).catch(()=>{document.getElementById('hL').innerHTML='<div class="empty">History load failed</div>';});}
function loadPat(){fetch('/api/patterns').then(r=>r.json()).then(p=>{let h='';
h+='<div class="pg"><h3>Flock MAC Prefixes ('+p.macs.length+')</h3><div class="it">'+p.macs.map(m=>'<span>'+m+'</span>').join('')+'</div></div>';
h+='<div class="pg"><h3>Contract Mfr MACs ('+p.macs_mfr.length+')</h3><div class="it">'+p.macs_mfr.map(m=>'<span>'+m+'</span>').join('')+'</div></div>';
h+='<div class="pg"><h3>SoundThinking MACs ('+p.macs_soundthinking.length+')</h3><div class="it">'+p.macs_soundthinking.map(m=>'<span>'+m+'</span>').join('')+'</div></div>';
h+='<div class="pg"><h3>BLE Device Names ('+p.names.length+')</h3><div class="it">'+p.names.map(n=>'<span>'+n+'</span>').join('')+'</div></div>';
h+='<div class="pg"><h3>BLE Manufacturer IDs ('+p.mfr.length+')</h3><div class="it">'+p.mfr.map(m=>'<span>0x'+m.toString(16).toUpperCase().padStart(4,'0')+'</span>').join('')+'</div></div>';
h+='<div class="pg"><h3>Raven UUIDs ('+p.raven.length+')</h3><div class="it">'+p.raven.map(u=>'<span style="font-size:8px">'+u+'</span>').join('')+'</div></div>';
document.getElementById('pC').innerHTML=h;window._pL=1;}).catch(()=>{document.getElementById('pC').innerHTML='<div class="empty">Pattern DB load failed</div>';});}
// GPS from phone -> ESP32 (wardriving)
// Geolocation requires secure context; this dashboard is served over HTTPS.
// We only request on user tap (gesture) for best permission prompt chance.
let _gW=null,_gOk=false,_gTried=false;
function sendGPS(p){_gOk=true;let g=document.getElementById('sG');g.textContent='OK';g.style.color='#22c55e';
fetch('/api/gps?lat='+p.coords.latitude+'&lon='+p.coords.longitude+'&acc='+(p.coords.accuracy||0)).catch(()=>{});}
function gpsErr(e){_gOk=false;let g=document.getElementById('sG');
var msg='ERR';if(e.code===1){msg='DENIED';g.style.color='#ef4444';alert('GPS permission denied. Check browser Location permissions and certificate trust for this HTTPS page.');}
else if(e.code===2){msg='N/A';g.style.color='#ef4444';}
else if(e.code===3){msg='WAIT';g.style.color='#facc15';}
g.textContent=msg;}
function startGPS(){if(!navigator.geolocation){return false;}
if(_gW!==null){navigator.geolocation.clearWatch(_gW);_gW=null;}
let g=document.getElementById('sG');g.textContent='...';g.style.color='#facc15';
_gW=navigator.geolocation.watchPosition(sendGPS,gpsErr,{enableHighAccuracy:true,maximumAge:5000,timeout:15000});return true;}
function reqGPS(){if(!navigator.geolocation){alert('GPS not available in this browser.');return;}
if(_gOk){return;}
if(!window.isSecureContext){alert('GPS requires a secure context (HTTPS). Re-open the dashboard over https://192.168.4.1 or https://flockyou.local.');}
startGPS();_gTried=true;}
refresh();setInterval(refresh,2500);
</script></body></html>
)rawliteral";

// ============================================================================
// WEB SERVER SETUP
// ============================================================================

static esp_err_t fySendResponse(httpd_req_t* req, const char* status, const char* contentType, const String& body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, contentType);
    return httpd_resp_send(req, body.c_str(), body.length());
}

static bool fyGetQueryParam(httpd_req_t* req, const char* key, char* out, size_t outLen) {
    int qLen = httpd_req_get_url_query_len(req) + 1;
    if (qLen <= 1 || (size_t)qLen > 256) return false;
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    return httpd_query_key_value(query, key, out, outLen) == ESP_OK;
}

static esp_err_t fyHandleRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, FY_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t fyHandleDetections(httpd_req_t* req) {
    return fySendResponse(req, "200 OK", "application/json", fyBuildDetectionsJSON());
}

static esp_err_t fyHandleStats(httpd_req_t* req) {
    int raven = 0;
    int withGPS = 0;
    int total = 0;
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        total = fyDetCount;
        for (int i = 0; i < fyDetCount; i++) {
            if (fyDet[i].isRaven) raven++;
            if (fyDet[i].hasGPS) withGPS++;
        }
        xSemaphoreGive(fyMutex);
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"total\":%d,\"raven\":%d,\"ble\":\"active\","
             "\"gps_valid\":%s,\"gps_age\":%lu,\"gps_tagged\":%d}",
             total, raven, fyGPSIsFresh() ? "true" : "false",
             fyGPSValid ? (millis() - fyGPSLastUpdate) : 0UL, withGPS);
    return fySendResponse(req, "200 OK", "application/json", String(buf));
}

static esp_err_t fyHandleGps(httpd_req_t* req) {
    char lat[32];
    char lon[32];
    char acc[32];
    if (!fyGetQueryParam(req, "lat", lat, sizeof(lat)) || !fyGetQueryParam(req, "lon", lon, sizeof(lon))) {
        return fySendResponse(req, "400 Bad Request", "application/json", "{\"error\":\"lat,lon required\"}");
    }
    fyGPSLat = atof(lat);
    fyGPSLon = atof(lon);
    fyGPSAcc = fyGetQueryParam(req, "acc", acc, sizeof(acc)) ? (float)atof(acc) : 0.0f;
    fyGPSValid = true;
    fyGPSLastUpdate = millis();
    return fySendResponse(req, "200 OK", "application/json", "{\"status\":\"ok\"}");
}

static esp_err_t fyHandlePatterns(httpd_req_t* req) {
    return fySendResponse(req, "200 OK", "application/json", fyBuildPatternsJSON());
}

static esp_err_t fyHandleExportJson(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"flockyou_detections.json\"");
    return fySendResponse(req, "200 OK", "application/json", fyBuildDetectionsJSON());
}

static esp_err_t fyHandleExportCsv(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"flockyou_detections.csv\"");
    return fySendResponse(req, "200 OK", "text/csv", fyBuildDetectionsCSV());
}

static esp_err_t fyHandleExportKml(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"flockyou_detections.kml\"");
    return fySendResponse(req, "200 OK", "application/vnd.google-earth.kml+xml", fyBuildDetectionsKML());
}

static esp_err_t fyHandleHistory(httpd_req_t* req) {
    if (!fySpiffsReady || !SPIFFS.exists(FY_PREV_FILE))
        return fySendResponse(req, "200 OK", "application/json", "[]");
    File f = SPIFFS.open(FY_PREV_FILE, "r");
    if (!f) return fySendResponse(req, "500 Internal Server Error", "text/plain", "read error");
    String content = f.readString();
    f.close();
    return fySendResponse(req, "200 OK", "application/json", content.length() ? content : "[]");
}

static esp_err_t fyHandleHistoryJson(httpd_req_t* req) {
    if (!fySpiffsReady || !SPIFFS.exists(FY_PREV_FILE))
        return fySendResponse(req, "404 Not Found", "application/json", "{\"error\":\"no prior session\"}");
    File f = SPIFFS.open(FY_PREV_FILE, "r");
    if (!f) return fySendResponse(req, "500 Internal Server Error", "text/plain", "read error");
    String content = f.readString();
    f.close();
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"flockyou_prev_session.json\"");
    return fySendResponse(req, "200 OK", "application/json", content.length() ? content : "[]");
}

static esp_err_t fyHandleHistoryKml(httpd_req_t* req) {
    if (!fySpiffsReady || !SPIFFS.exists(FY_PREV_FILE))
        return fySendResponse(req, "404 Not Found", "application/json", "{\"error\":\"no prior session\"}");
    File f = SPIFFS.open(FY_PREV_FILE, "r");
    if (!f) return fySendResponse(req, "500 Internal Server Error", "text/plain", "read error");
    String content = f.readString();
    f.close();
    if (content.length() == 0)
        return fySendResponse(req, "404 Not Found", "application/json", "{\"error\":\"prior session empty\"}");
    int placed = 0;
    String kml = fyBuildPrevSessionKML(content, &placed);
    printf("[FLOCK-YOU] Prior session KML: %d placemarks\n", placed);
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"flockyou_prev_session.kml\"");
    return fySendResponse(req, "200 OK", "application/vnd.google-earth.kml+xml", kml);
}

static esp_err_t fyHandleClear(httpd_req_t* req) {
    fySaveSession();
    if (fyMutex && xSemaphoreTake(fyMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        fyDetCount = 0;
        memset(fyDet, 0, sizeof(fyDet));
        fyTriggered = false;
        fyDeviceInRange = false;
        xSemaphoreGive(fyMutex);
    }
    printf("[FLOCK-YOU] All detections cleared (session saved)\n");
    return fySendResponse(req, "200 OK", "application/json", "{\"status\":\"cleared\"}");
}

static void fyStartHttpsServer() {
    if (fyHttpsServer) return;
    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    // Keep TLS RAM usage low on ESP32-S3 builds with display + BLE active.
    // SSL sessions are memory-heavy; limiting socket count helps avoid
    // mbedtls alloc failures during handshake.
    cfg.httpd.max_uri_handlers = 12;
    cfg.httpd.max_resp_headers = 4;
    cfg.httpd.backlog_conn = 2;
    cfg.httpd.max_open_sockets = 2;
    cfg.httpd.stack_size = 6144;
    cfg.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    cfg.port_secure = 443;
    cfg.cacert_pem = (const uint8_t*)FY_AP_TLS_CERT_PEM;
    cfg.cacert_len = FY_AP_TLS_CERT_PEM_len;
    cfg.prvtkey_pem = (const uint8_t*)FY_AP_TLS_KEY_PEM;
    cfg.prvtkey_len = FY_AP_TLS_KEY_PEM_len;
    printf("[FLOCK-YOU] HTTPS cfg: sockets=%d handlers=%d stack=%d heap=%u\n",
           cfg.httpd.max_open_sockets, cfg.httpd.max_uri_handlers,
           cfg.httpd.stack_size,
           (unsigned)ESP.getFreeHeap());
    esp_err_t err = httpd_ssl_start(&fyHttpsServer, &cfg);
    if (err != ESP_OK) {
        printf("[FLOCK-YOU] HTTPS server start failed: %s\n", esp_err_to_name(err));
        fyHttpsServer = NULL;
        return;
    }

    static const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = fyHandleRoot, .user_ctx = nullptr},
        {.uri = "/api/detections", .method = HTTP_GET, .handler = fyHandleDetections, .user_ctx = nullptr},
        {.uri = "/api/stats", .method = HTTP_GET, .handler = fyHandleStats, .user_ctx = nullptr},
        {.uri = "/api/gps", .method = HTTP_GET, .handler = fyHandleGps, .user_ctx = nullptr},
        {.uri = "/api/patterns", .method = HTTP_GET, .handler = fyHandlePatterns, .user_ctx = nullptr},
        {.uri = "/api/export/json", .method = HTTP_GET, .handler = fyHandleExportJson, .user_ctx = nullptr},
        {.uri = "/api/export/csv", .method = HTTP_GET, .handler = fyHandleExportCsv, .user_ctx = nullptr},
        {.uri = "/api/export/kml", .method = HTTP_GET, .handler = fyHandleExportKml, .user_ctx = nullptr},
        {.uri = "/api/history", .method = HTTP_GET, .handler = fyHandleHistory, .user_ctx = nullptr},
        {.uri = "/api/history/json", .method = HTTP_GET, .handler = fyHandleHistoryJson, .user_ctx = nullptr},
        {.uri = "/api/history/kml", .method = HTTP_GET, .handler = fyHandleHistoryKml, .user_ctx = nullptr},
        {.uri = "/api/clear", .method = HTTP_GET, .handler = fyHandleClear, .user_ctx = nullptr},
    };
    for (size_t i = 0; i < (sizeof(routes) / sizeof(routes[0])); i++) {
        esp_err_t regErr = httpd_register_uri_handler(fyHttpsServer, &routes[i]);
        if (regErr != ESP_OK) {
            printf("[FLOCK-YOU] URI register failed for %s: %s\n", routes[i].uri, esp_err_to_name(regErr));
        }
    }
    printf("[FLOCK-YOU] HTTPS server started on port 443\n");
}

static void fyStopHttpsServer() {
    if (!fyHttpsServer) return;
    httpd_ssl_stop(fyHttpsServer);
    fyHttpsServer = NULL;
    printf("[FLOCK-YOU] HTTPS server stopped\n");
}

// ============================================================================
// MAIN FUNCTIONS
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    fyAudioInit();
    fyMutex = xSemaphoreCreateMutex();

    // Reclaim Classic BT heap; this firmware uses BLE (NimBLE) only.
    esp_err_t btRelease = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (btRelease == ESP_OK)
        printf("[FLOCK-YOU] Released Classic BT memory\n");
    else
        printf("[FLOCK-YOU] Classic BT memory release: %s\n", esp_err_to_name(btRelease));

    // Init SPIFFS for session persistence
    if (SPIFFS.begin(true)) {
        fySpiffsReady = true;
        printf("[FLOCK-YOU] SPIFFS ready\n");
        // Promote last session to prev_session before we start a new one
        fyPromotePrevSession();
    } else {
        printf("[FLOCK-YOU] SPIFFS init failed - no persistence\n");
    }

    printf("\n========================================\n");
    printf("  FLOCK-YOU Surveillance Detector\n");
    printf("  Audio: %s\n", fyAudioIsMuted() ? "MUTED" : "ON");
    printf("========================================\n");

    // WiFi AP before display/LVGL — needs internal RAM for NVS/WiFi stacks
    WiFi.mode(WIFI_AP);
    delay(100);
    if (!WiFi.softAP(FY_AP_SSID, FY_AP_PASS)) {
        printf("[FLOCK-YOU] WARNING: softAP start failed\n");
    }
    printf("[FLOCK-YOU] AP: %s / %s\n", FY_AP_SSID, FY_AP_PASS);
    printf("[FLOCK-YOU] IP: %s\n", WiFi.softAPIP().toString().c_str());
    fyStartHttpsServer();
#if defined(BOARD_HAS_PSRAM)
    printf("[FLOCK-YOU] Heap internal=%u psram=%u\n",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
#endif

    fyInitBleStack();
    // Kick off the first scan right away
    if (fyBLEScan) {
        fyBLEScan->start(fyBleScanDuration, false);
        fyLastBleScan = millis();
        printf("[FLOCK-YOU] BLE scanning ACTIVE\n");
        printf("[FLOCK-YOU] BLE GATT server advertising (service %s)\n", FY_SERVICE_UUID);
    }

    fyBootBeep();

#ifdef FY_HAS_DISPLAY
    if (!fyDisplayHwInit()) {
        printf("[FLOCK-YOU] WARNING: LCD hardware init failed\n");
    }
    fyDisplayInit();
#endif

    printf("[FLOCK-YOU] Detection methods: MAC prefix, device name, manufacturer ID, Raven UUID\n");
    printf("[FLOCK-YOU] Build tag: https-ble-deinit-v2\n");
    printf("[FLOCK-YOU] Dashboard: https://192.168.4.1 (or https://flockyou.local)\n");
    printf("[FLOCK-YOU] Ready - BLE GATT + AP mode\n\n");
}

void loop() {
    // Serial host detection (heartbeat from DeFlock desktop app)
    if (Serial.available()) {
        while (Serial.available()) Serial.read();  // drain buffer
        fyLastSerialHeartbeat = millis();
        if (!fySerialHostConnected) {
            fySerialHostConnected = true;
            fyCompanionChangePending = true;
        }
    } else if (fySerialHostConnected &&
               millis() - fyLastSerialHeartbeat >= FY_SERIAL_TIMEOUT_MS) {
        fySerialHostConnected = false;
        fyCompanionChangePending = true;
    }

    // Apply deferred companion mode switch (from BLE callbacks or serial detection)
    if (fyCompanionChangePending) {
        fyCompanionChangePending = false;
        fyOnCompanionChange();
    }

    // Pause BLE scans while phone is connected to AP to free heap for TLS.
    fyUpdateBleForApClients();

    // BLE scanning cycle
    if (fyBLEScan &&
        !fyBlePausedForApClient &&
        millis() - fyLastBleScan >= fyBleScanInterval &&
        !fyBLEScan->isScanning()) {
        fyBLEScan->start(fyBleScanDuration, false);
        fyLastBleScan = millis();
    }

    if (fyBLEScan &&
        !fyBLEScan->isScanning() &&
        millis() - fyLastBleScan > (unsigned long)fyBleScanDuration * 1000) {
        fyBLEScan->clearResults();
    }

    // Heartbeat tracking
    if (fyDeviceInRange) {
        if (millis() - fyLastHB >= 10000) {
            fyHeartbeat();
            fyLastHB = millis();
        }
        if (millis() - fyLastDetTime >= 30000) {
            printf("[FLOCK-YOU] Device out of range - stopping heartbeat\n");
            fyDeviceInRange = false;
            fyTriggered = false;
        }
    }

    // Auto-save session to SPIFFS every 15s if detections changed
    // Also triggers an early save 5s after first detection to minimize loss on power-cycle
    if (fySpiffsReady && millis() - fyLastSave >= FY_SAVE_INTERVAL) {
        if (fyDetCount > 0 && fyDetCount != fyLastSaveCount) {
            fySaveSession();
        }
        fyLastSave = millis();
    } else if (fySpiffsReady && fyDetCount > 0 && fyLastSaveCount == 0 &&
               millis() - fyLastSave >= 5000) {
        // Quick first-save: persist within 5s of first detection
        fySaveSession();
        fyLastSave = millis();
    }

#ifdef FY_HAS_DISPLAY
    fyUiTick();
#else
    delay(100);
#endif
}
