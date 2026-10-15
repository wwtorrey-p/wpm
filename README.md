# Water Pump Monitor

ESP32-S3 firmware and web UI that monitors a Franklin Electric SubDrive/MonoDrive Connect pump controller and enforces a progressive back-off after faults.

## Overview

The well recovers only about 50 gal/day, but a single demand pumps roughly 600 gal. The drive's own restart delay (Underload "Off Time") is too short, so this device gates the drive's Aux Input and applies its own back-off:

- Any drive fault (Alarm relay) opens the Aux contact and starts a back-off. The first back-off is user-set (default 48 h), and each further fault doubles it up to a cap (default 30 days).
- When a back-off ends, the pump is allowed to run again.
- The first time the pump stops with no fault, the back-off resets to the initial value.
- The pump can also be disabled manually, and the back-off can be reset or ended early.
- The device logs calls for water, faults, recoveries and run time, and estimates gallons pumped (run time x configured gal/min).

The device is reachable over Wi-Fi only, using HTTPS with a login.

Status: the firmware builds and the web UI builds. It has not yet been tested against the real drive. Bench-test with jumpers first (see below).

## Hardware

Board: Lonely Binary ESP32-S3 N16R8 with the PinPulse shield (LED shield only, no I/O).

| Signal | GPIO | Connection |
|---|---|---|
| Run sense | 4 | Drive System Run relay dry contact between GPIO and GND (internal pull-up; closed = running) |
| Alarm sense | 5 | Drive System Alarm relay dry contact between GPIO and GND (closed = fault) |
| Aux control | 6 | Drives a relay module (3.3 V logic). Its NO contact connects the drive's +24 terminal to Aux In. High = contact closed = pump allowed |
| Status LED | 48 | On-board WS2812 |

Drive configuration (FE Connect app):
- Set Auxiliary Input to **Fault with Low**. The drive stops when the contact opens and restarts about 30 s after it closes. This is fail-stop: if the ESP loses power, the relay opens and the pump stops.
- The Run and Alarm relay contacts are rated 5 A at 250 VAC / 30 VDC. Use only these low-voltage terminals. Do not switch mains with the ESP.
- The drive raises F26 whenever Aux is open, which also activates the Alarm relay. The firmware ignores the alarm until a grace period (default 60 s) after it closes Aux.
- Pins 4, 5 and 6 are my choice. Check them against your wiring, and avoid GPIO 35-37 (octal PSRAM), strapping pins and the USB pins.

Status LED (dim): red = alarm, blue = running, amber = back-off, white = disabled, green = idle/armed.

### Schematic

```
 SubDrive / MonoDrive Connect                              ESP32-S3
 (low-voltage control terminals)                           (PinPulse shield)

  SYSTEM RUN RELAY (dry contact)
    COM ──────────────────────────────────────────────────── GND
    NO  ───────────────────┬────[ 220 R ]──────────────────── GPIO 4  (internal pull-up)
                           └────||── GND   (optional 100 nF)

  SYSTEM ALARM RELAY (dry contact)
    COM ──────────────────────────────────────────────────── GND
    NO  ───────────────────┬────[ 220 R ]──────────────────── GPIO 5  (internal pull-up)
                           └────||── GND   (optional 100 nF)

  TRANSDUCER or WET SENSOR terminal                 Relay module (3.3 V logic input)
    +24 ──────────────────────────────┐              VCC ───── 5V
                                      │              GND ───── GND
                                      ├── COM        IN  ───── GPIO 6
                                      │   (relay NO contact, closed = run allowed)
    AUX IN ───────────────────────────┴── NO
```

Notes:
- The drive's relay contacts are isolated, so the only shared reference is the ESP GND via the COM wires.
- Run and Alarm each pull their GPIO to GND when the contact closes. The resistor and capacitor are optional filtering for long cable runs.
- The relay module's switched contact only connects the drive's own +24 to its Aux In. Nothing from the ESP is connected to the 24 V rail.
- Use a relay module with an opto-isolated or transistor driver and a flyback diode, and confirm its input works at 3.3 V. If the contact must be held closed by a high level on IN, check whether your module is active-high or active-low and adjust `gpio_set_level` in `src/main.cpp` if it is active-low.
- Drive-side wiring is on terminal block labels. See the owner's manual (`ref/`), pages 22-23 and 37.

## Initial dev setup

1. Install VS Code and the **PlatformIO IDE** extension. It downloads the ESP-IDF toolchain on first build (several hundred MB, takes a few minutes).
2. Install **Node.js** LTS (`winget install OpenJS.NodeJS.LTS`) for the web UI.
3. Optional, for host unit tests: a C++ compiler on PATH (for example MinGW-w64 via MSYS2).
4. Copy `src/secrets.h.example` to `src/secrets.h` and set `WIFI_SSID` and `WIFI_PASS`. The file is git-ignored. The network must be 2.4 GHz.
5. Connect the board using the **CH340 (UART) USB port**. `platformio.ini` sets `upload_port` and `monitor_port` to `COM4`. Change them if your port differs (`pio device list`). The native USB port re-enumerates on reset and breaks uploads.

## Build and load the firmware

In a terminal (`$pio` is the path to `pio.exe`, normally `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`):

```powershell
# 1. Build the web UI into data/
cd web; npm install; npm run build; cd ..

# 2. Build and upload the firmware
& $pio run -e esp32s3 -t upload

# 3. Upload the web UI (SPIFFS image built from data/)
& $pio run -e esp32s3 -t uploadfs

# 4. Watch the log; look for "wifi connected, https://<ip>/"
& $pio device monitor
```

Re-run step 3 whenever the web UI changes, and step 2 whenever the firmware changes.

Host unit tests for the back-off logic (needs a compiler):

```powershell
& $pio test -e native
```

Notes:
- If the upload cannot connect, hold BOOT, tap RESET, release BOOT, and retry.
- `platformio.ini` passes `-DPROJECT_VER=0.1.0` so the build works in a repo with no commits.
- The monitor holds the serial port. Stop it (Ctrl+C) before uploading.

## Using the app

Open `https://<device-ip>/`. The IP is printed in the serial log when Wi-Fi connects and is also listed in your router. The certificate is self-signed, so the browser shows a warning to accept once.

Sign in as `admin` / `admin`, then change the password right away (the UI warns while the default is in use). Passwords must be at least 6 characters and not `admin`.

Dashboard:
- **Status:** state (armed, settling, backoff, disabled), pump running or idle, drive alarm, whether running is permitted, the back-off countdown and retry time, the next back-off length, fault count, total run time, gallons since the last clean stop, and a 30-day average against your target.
- **Buttons:**
  - *Disable pump* / *Enable pump*: opens or closes Aux. A back-off in progress is kept while disabled.
  - *Reset back-off*: returns to the initial duration and resumes immediately.
  - *End back-off now*: resumes immediately but keeps the escalated duration.
- **Chart:** estimated gallons per day (UTC days) with the target line.
- **Event log:** the last 100 events, newest first.
- **Settings:** initial and maximum back-off (hours), restart grace (seconds, minimum 35), pump rate (gal/min, default 5), target (gal/day, display only).

States:

| State | Aux | Meaning |
|---|---|---|
| Settling | closed | Just resumed; alarm ignored while the drive restarts |
| Armed | closed | Normal. Any alarm triggers a fault and back-off |
| Backoff | open | Waiting until the retry time |
| Disabled | open | Manually disabled |

## Architecture

```
src/
  main.cpp        pins, Wi-Fi, NTP, SPIFFS mount, 100 ms control loop
  controller.*    pure state machine (no ESP dependencies, host-testable)
  app.*           owns controller, persistence, event log, daily history, credentials; mutex-guarded
  web.*           HTTPS server, Basic auth, JSON API, static files from SPIFFS
  cert.*          generates and stores a self-signed ECDSA certificate on first boot
  storage.h       NVS blob helpers
  led.*           WS2812 status LED
web/              Preact + Vite UI; builds to data/ (SPIFFS image)
test/             Unity tests for the controller
```

Data flow: the control loop reads the Run and Alarm GPIOs every 100 ms, calls `App::tick`, which runs `Controller::tick`, and then sets the Aux GPIO from `Controller::auxClosed()`. HTTP handlers take the same mutex.

Controller details:
- Inputs are debounced (Run 0.5 s, Alarm 2 s). A Run drop is confirmed as a clean stop only after 3 s with no alarm, because the drive can drop Run slightly before raising Alarm.
- The back-off end is stored as a wall-clock epoch time. If the clock is not yet set after a reboot (NTP not synced), the device stays in back-off until it is, so it fails safe.
- Aux relay output is low (pump stopped) from the first instructions of boot.

Persistence (NVS, namespace `pump`): settings, controller state (`persist`), event log (`events`), 90 days of daily gallons (`days`), credentials (`cred`, salted SHA-256), and the TLS certificate and key. Structural changes are saved immediately and run-time counters about once a minute, to limit flash wear.

HTTP API (all require Basic auth):

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Current state and counters |
| GET/PUT | `/api/settings` | Read or update settings |
| POST | `/api/control` | `{"action": "enable" \| "disable" \| "reset" \| "skip"}` |
| GET | `/api/log` | Event log |
| GET | `/api/history` | Daily gallons |
| POST | `/api/password` | `{"password": "..."}` |
| GET | `/*` | Static UI files |

## Limitations and notes

- The drive does not report which fault occurred, so any alarm is treated the same.
- Gallons are an estimate (run time x gal/min), not a measurement.
- Daily buckets use UTC. The browser displays dates in local time.
- OTA updates are not implemented. Update over USB.
- Wi-Fi credentials are compiled in (`src/secrets.h`). Change them by rebuilding.
- The self-signed certificate is generated once and kept in flash. Erasing flash makes a new one.
- To factory-reset stored data: `& $pio run -e esp32s3 -t erase`, then upload again.

## Troubleshooting

| Symptom | Check |
|---|---|
| `Could not open COM3` on upload | Use the CH340 port (`upload_port` in `platformio.ini`); close the serial monitor |
| Wi-Fi loops with reason 15 | Wrong password, 5 GHz-only or WPA3-only network, or MAC filtering |
| No IP in the log | Wi-Fi not connected; see the disconnect reason in the log |
| Blank page or 404 | Run `uploadfs` after `npm run build` |
| Pump won't start after enabling | Aux contact wiring, drive set to "Fault with Low", and the grace period (60 s) |
| `MissingPackageManifestError` on build | Delete the half-downloaded folder in `~/.platformio/packages` and rebuild |
