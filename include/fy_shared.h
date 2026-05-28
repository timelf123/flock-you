#pragma once

#include "fy_types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class NimBLEScan;

#define FY_SESSION_FILE  "/session.json"
#define FY_PREV_FILE     "/prev_session.json"
#define GPS_STALE_MS     30000

#define FY_AP_SSID "flockyou"
#define FY_AP_PASS "flockyou123"

extern FYDetection fyDet[];
extern int fyDetCount;
extern SemaphoreHandle_t fyMutex;

extern double fyGPSLat;
extern double fyGPSLon;
extern float fyGPSAcc;
extern bool fyGPSValid;
extern unsigned long fyGPSLastUpdate;

extern NimBLEScan* fyBLEScan;
extern bool fySpiffsReady;
extern bool fyTriggered;
extern bool fyDeviceInRange;

void fySaveSession();

bool fyGPSIsFresh();
