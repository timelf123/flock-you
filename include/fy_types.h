#pragma once

#include <stdint.h>

#define FY_MAX_DETECTIONS 200

struct FYDetection {
    char mac[18];
    char name[48];
    int rssi;
    char method[32];
    unsigned long firstSeen;
    unsigned long lastSeen;
    int count;
    bool isRaven;
    char ravenFW[16];
    double gpsLat;
    double gpsLon;
    float gpsAcc;
    bool hasGPS;
};
