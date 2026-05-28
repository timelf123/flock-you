#pragma once

#include "fy_types.h"
#include <stddef.h>

int fyAppGetDetectionCount();
bool fyAppCopyDetection(int index, FYDetection* out);
int fyAppGetRavenCount();
bool fyAppGetGpsStats(int* taggedOut, int* totalOut, bool* gpsValidOut, unsigned long* gpsAgeMsOut);
bool fyAppIsBleActive();
void fyAppClearDetections();

bool fyAppLoadPrevSession(FYDetection* out, int maxCount, int* countOut);

void fyAppGetPatternCounts(size_t* flockMacs, size_t* mfrMacs, size_t* stMacs,
                          size_t* names, size_t* mfrIds, size_t* ravenUuids);
const char* const* fyAppGetFlockMacPrefixes(size_t* count);
const char* const* fyAppGetMfrMacPrefixes(size_t* count);
const char* const* fyAppGetSoundThinkingMacPrefixes(size_t* count);
const char* const* fyAppGetDeviceNamePatterns(size_t* count);
const uint16_t* fyAppGetBleManufacturerIds(size_t* count);
const char* const* fyAppGetRavenServiceUuids(size_t* count);
