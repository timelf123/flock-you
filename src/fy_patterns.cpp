#include "fy_patterns.h"

const char* const flock_mac_prefixes[] = {
    "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84",
    "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
    "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "14:5a:fc",
    "74:4c:a1", "08:3a:88", "9c:2f:9d", "94:08:53", "e4:aa:ea",
    "b4:1e:52"
};
const size_t flock_mac_prefixes_count = sizeof(flock_mac_prefixes) / sizeof(flock_mac_prefixes[0]);

const char* const flock_mfr_mac_prefixes[] = {
    "f4:6a:dd", "f8:a2:d6", "e0:0a:f6", "00:f4:8d", "d0:39:57",
    "e8:d0:fc"
};
const size_t flock_mfr_mac_prefixes_count = sizeof(flock_mfr_mac_prefixes) / sizeof(flock_mfr_mac_prefixes[0]);

const char* const soundthinking_mac_prefixes[] = {
    "d4:11:d6"
};
const size_t soundthinking_mac_prefixes_count =
    sizeof(soundthinking_mac_prefixes) / sizeof(soundthinking_mac_prefixes[0]);

const char* const device_name_patterns[] = {
    "FS Ext Battery",
    "Penguin",
    "Flock",
    "Pigvision"
};
const size_t device_name_patterns_count = sizeof(device_name_patterns) / sizeof(device_name_patterns[0]);

const uint16_t ble_manufacturer_ids[] = {
    0x09C8
};
const size_t ble_manufacturer_ids_count = sizeof(ble_manufacturer_ids) / sizeof(ble_manufacturer_ids[0]);

const char* const raven_service_uuids[] = {
    "0000180a-0000-1000-8000-00805f9b34fb",
    "00003100-0000-1000-8000-00805f9b34fb",
    "00003200-0000-1000-8000-00805f9b34fb",
    "00003300-0000-1000-8000-00805f9b34fb",
    "00003400-0000-1000-8000-00805f9b34fb",
    "00003500-0000-1000-8000-00805f9b34fb",
    "00001809-0000-1000-8000-00805f9b34fb",
    "00001819-0000-1000-8000-00805f9b34fb"
};
const size_t raven_service_uuids_count = sizeof(raven_service_uuids) / sizeof(raven_service_uuids[0]);
