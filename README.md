# Flock-You: Surveillance Device Detector

<img src="flock.png" alt="Flock You" width="300px">

**Standalone BLE surveillance device detector with web dashboard, GPS wardriving, and session persistence.**

Available as part of the OUI-SPY project at [colonelpanic.tech](https://colonelpanic.tech)

---

## Overview

Flock-You detects Flock Safety surveillance cameras, Raven gunshot detectors, and related monitoring hardware using BLE-only heuristics. It runs a WiFi access point with a live web dashboard on your phone, tags detections with GPS from your phone's browser, and exports everything as JSON, CSV, or KML for Google Earth.

No WiFi sniffing — the radio is dedicated to serving the dashboard AP while BLE scans continuously in the background via ESP32 coexistence.

---

## Detection Methods

All detection is BLE-based:

| Method | Description |
|--------|-------------|
| **MAC prefix** | 20 known Flock Safety OUI prefixes (FS Ext Battery, Flock WiFi modules) |
| **BLE device name** | Case-insensitive substring match: `FS Ext Battery`, `Penguin`, `Flock`, `Pigvision` |
| **Manufacturer ID** | `0x09C8` (XUNTONG) — catches devices with no broadcast name. *From [wgreenberg/flock-you](https://github.com/wgreenberg/flock-you)* |
| **Raven service UUID** | Identifies Raven gunshot detectors by BLE GATT service UUIDs |
| **Raven FW estimation** | Determines firmware version (1.1.x / 1.2.x / 1.3.x) from advertised service patterns |

---

## Features

- **WiFi AP**: `flockyou` / password `flockyou123`
- **Web dashboard** at `192.168.4.1` — live detection feed, pattern database, export tools
- **GPS wardriving** — phone GPS via browser Geolocation API tags every detection with coordinates
- **Session persistence** — detections auto-save to flash (SPIFFS) every 60 seconds
- **Prior session tab** — previous session survives reboot and is viewable in the PREV tab
- **Export formats**: JSON, CSV, and KML (Google Earth) — current and prior sessions
- **Serial output** — Flask-compatible JSON over serial for live desktop ingestion
- **200 unique device storage** with FreeRTOS mutex thread safety
- **Crow call boot sounds** — modulated descending frequency sweeps with warble texture
- **Detection alerts** — ascending chirps + descending caw on new device detection
- **Heartbeat** — soft double coo every 10s while a device stays in range

---

## Enabling GPS (Android Chrome)

The dashboard uses your phone's GPS to geotag detections. Because it's served over HTTP, Chrome requires a one-time flag change:

1. Open a new Chrome tab and go to `chrome://flags`
2. Search for **"Insecure origins treated as secure"**
3. Add `http://192.168.4.1` to the text field
4. Set the flag to **Enabled**
5. Tap **Relaunch**

After relaunching, connect to the `flockyou` AP, open `192.168.4.1`, and tap the **GPS** card in the stats bar to grant location permission.

> **Note:** iOS Safari does not support Geolocation over HTTP. GPS wardriving requires Android with Chrome.

---

## Hardware

### Headless (default): Seeed XIAO ESP32-C3

| Pin | Function |
|-----|----------|
| GPIO 3 | Piezo buzzer |

### Screened: LCDWiki 2.8" ESP32-S3 E32N28P / E32C28P

[LCDWiki E32C28P/E32N28P](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display_E32C28P/E32N28P) — **ILI9341** 240×320, **not** the Waveshare ST7789 pinout.

| SKU | Touch | PlatformIO env |
|-----|-------|----------------|
| **E32C28P** / **E32N28P** | FT6336G (I2C 0x38) | `esp32_s3_lcdwiki_e32n28p` |

| Subsystem | GPIO |
|-----------|------|
| LCD SPI | CS 10, DC 46, MOSI 11, SCLK 12, MISO 13, BL 45 |
| Touch (C28P only) | SDA 16, SCL 15, RST 18, INT 17 |
| Audio I2S | EN 1 (low=on), MCLK 4, BCLK 5, DOUT 6, LRCK 7 |
| RGB status LED (back) | 42 |

**Waveshare** ESP32-S3-Touch-LCD-2.8 (ST7789) uses env `esp32_s3_waveshare_lcd_28` instead.

The screened build still runs the WiFi AP and web dashboard; use the phone for GPS wardriving and JSON/CSV/KML export. The LCD mirrors the web UI (LIVE / PREV / DB / TOOLS tabs).

---

## Building & Flashing

Requires [PlatformIO](https://platformio.org/).

**XIAO ESP32-C3 (headless):**

```bash
pio run -e xiao_esp32c3
pio run -e xiao_esp32c3 -t upload
pio device monitor
```

**LCDWiki 2.8" ESP32-S3 (ILI9341 + FT6336 touch):**

```bash
pio run -e esp32_s3_lcdwiki_e32n28p
pio run -e esp32_s3_lcdwiki_e32n28p -t upload
pio device monitor
```

If touches are offset, adjust `FY_TOUCH_SWAP_XY` / `FY_TOUCH_MIRROR_X` / `FY_TOUCH_MIRROR_Y` in `include/fy_board.h`. ES3**N**28P boards ship without a touch panel — I2C scan will show no device at `0x38`.

**Dependencies** (managed by PlatformIO):

- `NimBLE-Arduino` — BLE scanning
- `ESP Async WebServer` + `AsyncTCP` — web dashboard
- `ArduinoJson` — JSON serialization
- `SPIFFS` — session persistence to flash
- `LVGL` + `TFT_eSPI` — on-device UI (screened env only)

---

## Flask Companion App

The `api/` folder contains a Flask web application for desktop analysis of detection data.

```bash
cd api
pip install -r requirements.txt
python flockyou.py
```

Open `http://localhost:5000` for the desktop dashboard.

**Import support:** JSON, CSV, and KML files exported from the ESP32 can be imported directly into the Flask app. Live serial ingestion is also supported — connect the ESP32 via USB and select the serial port in the Flask UI.

---

## Raven Gunshot Detector Detection

Flock-You identifies SoundThinking/ShotSpotter Raven devices through BLE service UUID fingerprinting:

| Service | UUID | Description |
|---------|------|-------------|
| Device Info | `0000180a-...` | Serial, model, firmware |
| GPS | `00003100-...` | Real-time coordinates |
| Power | `00003200-...` | Battery & solar status |
| Network | `00003300-...` | LTE/WiFi connectivity |
| Upload | `00003400-...` | Data transmission metrics |
| Error | `00003500-...` | Diagnostics & error logs |
| Health (legacy) | `00001809-...` | Firmware 1.1.x |
| Location (legacy) | `00001819-...` | Firmware 1.1.x |

Firmware version is estimated automatically from which service UUIDs are advertised.

---

## Acknowledgments

- **Will Greenberg** ([@wgreenberg](https://github.com/wgreenberg)) — BLE manufacturer company ID detection (`0x09C8` XUNTONG) sourced from his [flock-you](https://github.com/wgreenberg/flock-you) fork
- **[DeFlock](https://deflock.me)** ([FoggedLens/deflock](https://github.com/FoggedLens/deflock)) — crowdsourced ALPR location data and detection methodologies. Datasets included in `datasets/`
- **[GainSec](https://github.com/GainSec)** — Raven BLE service UUID dataset (`raven_configurations.json`) enabling detection of SoundThinking/ShotSpotter acoustic surveillance devices

---

## OUI-SPY Firmware Ecosystem

Flock-You is part of the OUI-SPY firmware family:

| Firmware | Description | Board |
|----------|-------------|-------|
| **[OUI-SPY Unified](https://github.com/colonelpanichacks/oui-spy-unified-blue)** | Multi-mode BLE + WiFi detector | ESP32-S3 / ESP32-C5 |
| **[OUI-SPY Detector](https://github.com/colonelpanichacks/ouispy-detector)** | Targeted BLE scanner with OUI filtering | ESP32-S3 |
| **[OUI-SPY Foxhunter](https://github.com/colonelpanichacks/ouispy-foxhunter)** | RSSI-based proximity tracker | ESP32-S3 |
| **[Flock You](https://github.com/colonelpanichacks/flock-you)** | Flock Safety / Raven surveillance detection (this project) | ESP32-S3 |
| **[Sky-Spy](https://github.com/colonelpanichacks/Sky-Spy)** | Drone Remote ID detection | ESP32-S3 / ESP32-C5 |
| **[Remote-ID-Spoofer](https://github.com/colonelpanichacks/Remote-ID-Spoofer)** | WiFi Remote ID spoofer & simulator with swarm mode | ESP32-S3 |
| **[OUI-SPY UniPwn](https://github.com/colonelpanichacks/Oui-Spy-UniPwn)** | Unitree robot exploitation system | ESP32-S3 |

---

## Author

**colonelpanichacks**

**Oui-Spy devices available at [colonelpanic.tech](https://colonelpanic.tech)**

---

## Disclaimer

This tool is intended for security research, privacy auditing, and educational purposes. Detecting the presence of surveillance hardware in public spaces is legal in most jurisdictions. Always comply with local laws regarding wireless scanning and signal interception. The authors are not responsible for misuse.
