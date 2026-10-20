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

Status: the firmware and web UI build and run on the board. The bench tests below and the drive wiring have not been done yet. Bench-test with jumpers first.

## Hardware

Board: Lonely Binary ESP32-S3 N16R8 with the PinPulse shield (LED shield only, no I/O).

| Signal      | GPIO | Connection                                                                                                                            |
| ----------- | ---- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Run sense   | 4    | Drive System Run relay dry contact between GPIO and GND (internal pull-up; closed = running)                                          |
| Alarm sense | 5    | Drive System Alarm relay dry contact between GPIO and GND (closed = fault)                                                            |
| Aux control | 6    | Drives a relay module (3.3 V logic). Its NC contact connects the drive's +24 terminal to Aux In. Low = relay off = contact closed = pump allowed. High = relay energized = pump stopped |
| Status LED  | 48   | On-board WS2812                                                                                                                       |

Drive configuration (FE Connect app):

- Set Auxiliary Input to **Fault with Low**. The drive stops when the contact opens and restarts about 30 s after it closes. A broken wire to Aux In also stops the pump. If the ESP loses power the relay drops out and the pump is allowed to run, so a back-off is not enforced while the ESP is off or rebooting.
- The Run and Alarm relay contacts are rated 5 A at 250 VAC / 30 VDC. Use only these low-voltage terminals. Do not switch mains with the ESP.
- The drive raises F26 whenever Aux is open, which also activates the Alarm relay. The firmware ignores the alarm until a grace period (default 60 s) after it closes Aux, or until the pump starts running (which proves the drive restarted), whichever comes first. Faults while the pump is running are reported after a 0.5 s debounce.
- Pins 4, 5 and 6 were chosen as free pins on the S3 N16R8. Check them against your wiring, and avoid GPIO 35-37 (octal PSRAM), strapping pins and the USB pins.

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
                                      │   (relay NC contact, closed = run allowed)
    AUX IN ───────────────────────────┴── NC
```

Notes:

- The drive's relay contacts are isolated, so the only shared reference is the ESP GND via the COM wires.
- Run and Alarm each pull their GPIO to GND when the contact closes. The resistor and capacitor are optional filtering for long cable runs.
- The relay module's switched contact only connects the drive's own +24 to its Aux In. Nothing from the ESP is connected to the 24 V rail.
- Use a relay module with an opto-isolated or transistor driver and a flyback diode, and confirm its input works at 3.3 V. The firmware drives GPIO 6 high to energize the relay (pump stopped), so set the module's trigger jumper to **High** (an active-low module would do the opposite).
- Check the coil voltage printed on the relay. A 5 V coil is powered from the 5V pin and a 3.3 V coil from 3V3. Do not power the coil from the drive's +24, which would break the isolation.
- Use the relay's COM and NC contacts, not NO. The relay is energized only when the pump is disabled or in back-off, and with the relay unpowered the contact is closed and the pump may run.
- Drive-side wiring is on terminal block labels. See the owner's manual (`ref/`), pages 22-23 and 37.

## Initial dev setup

1. Install VS Code and the **PlatformIO IDE** extension. It downloads the ESP-IDF toolchain on first build (several hundred MB, takes a few minutes).
2. Install **Node.js** LTS (`winget install OpenJS.NodeJS.LTS`) for the web UI.
3. Optional, for host unit tests: a C++ compiler on PATH (for example MinGW-w64 via MSYS2).
4. The Wi-Fi network is entered over the serial port the first time the device runs (see *Wi-Fi setup* below). Nothing about the network is stored in the source or in the firmware image. The network must be 2.4 GHz.
5. Connect the board with **one cable, on the CH340 (UART) USB connector**. It handles uploads, the serial log and power. `platformio.ini` sets `upload_port` and `monitor_port` to `COM5`. Change them if your port differs (`pio device list`). The native USB connector (VID:PID 303A:1001, usually COM3) re-enumerates on reset and breaks uploads, so leave it unplugged.

## Build and load the firmware

In a terminal (`$pio` is the path to `pio.exe`, normally `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`):

```powershell
# 1. Build and upload the firmware. This also builds the web UI (web/ -> data/) and embeds it.
& $pio run -e esp32s3 -t upload

# 2. Open the serial monitor. On a new device it asks for the Wi-Fi network (see Wi-Fi setup below).
#    Look for "connected, https://<ip>/"
& $pio device monitor
```

There is no separate filesystem upload: the web UI is embedded in the firmware image, so one image carries everything. `tools/build_web.py` runs `npm install` (first time only) and `npm run build` before each firmware build, then writes the built files into `src/generated/web_assets.cpp` (git-ignored) as plain C++ arrays, so Node.js must be on the PATH of the terminal that runs PlatformIO. If the UI build fails and `data/` already exists, the build continues with the existing files.

Settings, counters and the event log live in the NVS partition, so flashing does not erase them.

**First flash after moving to the OTA layout.** The partition table now has two app slots (`ota_0`, `ota_1`) plus `otadata`, replacing the old `factory` and `spiffs` partitions. Flash it once over USB with `upload`. The NVS partition keeps the same address, so stored settings survive. To turn on rollback, delete the generated `sdkconfig.esp32s3` once before that build so the new default in `sdkconfig.defaults` is picked up.

Editing `platformio.ini` makes PlatformIO rebuild everything. To change the port without that, pass it on the command line (`--upload-port COM5`), or flash the last build with `-t nobuild -t upload`.

UI development without hardware:

```powershell
cd web
npm run dev                                  # fake device API (web/mock-api.js), hot reload
$env:DEVICE_URL="https://<device-ip>"; npm run dev   # or proxy to a real device
```

With the fake API, `curl -X POST localhost:5173/api/control -d '{"action":"fault"}'` switches the dashboard into the back-off view.

Host unit tests for the back-off logic (needs a compiler):

```powershell
& $pio test -e native
```

Notes:

- If the upload cannot connect, hold BOOT, tap RESET, release BOOT, and retry.
- `platformio.ini` passes `-DPROJECT_VER=0.1.0` so the build works in a repo with no commits.
- The monitor holds the serial port. Stop it (Ctrl+C) before uploading.

## Updating the firmware over Wi-Fi

After the first USB flash you can update over Wi-Fi from the UI:

1. Build the firmware (`& $pio run -e esp32s3`). The image is `.pio/build/esp32s3/firmware.bin`.
2. In the app, open the menu, then *Settings*, and find **Firmware update**. It shows the running version and build time.
3. Choose `firmware.bin` and press *Upload and restart*, then confirm.
4. The device checks that the file is an app image for this project, writes it to the idle slot, validates it, switches slot and restarts. The page reloads when the device is back.

Behaviour and safety:

- The upload needs a signed-in user and runs over HTTPS. It is refused while the pump is running, so a call for water is not interrupted.
- The restart drops the relay, so the pump is allowed to run while the device is down (see the NC wiring note). A back-off is not enforced during the restart.
- Rollback: a new image must get a Wi-Fi address, start the web server and run for 30 s before it marks itself valid. If it does not, or it resets before then, the bootloader returns to the previous firmware. A new image that stays up but never gets online restarts itself after 5 minutes, which also rolls back.
- Rollback needs `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (set in `sdkconfig.defaults`; delete the generated `sdkconfig.esp32s3` once so it takes effect).
- The version shown is `PROJECT_VER` from `platformio.ini`, plus the build date and time, so you can tell builds apart. Change `PROJECT_VER` to label releases.

## Wi-Fi setup

The network name and password are entered over the serial port and stored in flash. They are never compiled in.

- **First run:** if no network is saved, the device asks for one in the serial monitor (`& $pio device monitor`, 115200 baud). If a network is already saved, it does not ask.
- **Change the network:** hold the **BOOT** button for 2 seconds while the device is running. The status LED turns purple and the prompt opens. Leave the network name empty, or stop typing for 60 s, to cancel and keep the current network.
- **Test before saving:** the new network is tried for up to 30 s. It is saved only if the device gets an IP address. If it fails, the prompt says why (for example wrong password or network not found), nothing is saved, and you are asked again. If a network was already saved, the device reconnects to it in the meantime.
- Typed passwords are shown as `*`. A WPA2 password needs at least 8 characters, and an empty password means an open network.
- The pump keeps being controlled while the prompt is open. The web page is unavailable while a new network is being tested.
- To forget the network entirely, erase the flash (see Limitations), which also clears all other settings.
- Holding BOOT while resetting the board enters the USB download mode instead. Only hold it while the device is already running.

## Using the app

Open `https://<device-ip>/`. The IP is printed in the serial log when Wi-Fi connects and is also listed in your router. The certificate is self-signed, so the browser shows a warning. Accept it once (Advanced, then continue). Installing the certificate into Windows will not remove the warning yet, because it has no Subject Alternative Name (see Limitations).

Sign in on the login screen as `admin` / `admin`, then change the password right away (the UI warns while the default is in use). Passwords must be at least 6 characters and not `admin`. The UI keeps the credentials for the browser tab only and sends them with each API request.

Dashboard (dark theme, phone friendly):

- **Title bar:** the title on the left and a menu (hamburger) on the right.
- **Status card:** two large icons, *Control* (Enabled, Disabled, Backoff) and *Pump* (Off, On, Fault), then an *Estimated capacity* gauge with ticks at the low and high levels. Pump shows Fault during a back-off, or when the drive alarm is active while armed. It is not shown as a fault while disabled, because the drive raises F26 whenever Aux is open. Tap the Control icon to disable (when Enabled), enable (when Disabled) or end the back-off (when Backoff, after a confirmation). Metrics follow: back-off length and retry time (during a back-off), the next back-off length, fault count, total run time, gallons this demand, the demand runtime, and the 30-day average against your target (shown under the gal/day value). "Gallons this demand" and "Demand runtime" count up while the pump runs and keep their last values after it stops, until the next pump start resets them. They are not saved across a reboot.
- **Capacity graph:** two lines on different scales. Estimated capacity (left scale, gal) and the 30-day average pump rate (right scale, gal/day), with a dashed line at your gal/day target. Gridlines and labelled axes show the scales and dates, and 7 / 30 / 90 day buttons change the range. The capacity line steps down to the low level at each call for water and back up to the high level at each clean stop. The average counts only days since the first recorded pumping, so it settles after the first month.
- **Menu:**
  - *Settings*: initial and maximum back-off (hours), restart check delay (seconds, minimum 35), pump rate (gal/min, default 5), target (gal/day, display only), low and high capacity levels (gal), change password, and firmware update.
  - *Event log*: the last 100 events, newest first, with a *Clear log* button. Each pump start logs *Pump on*, and every stop logs *Pump off* with the run time and gallons pumped, whatever the cause (clean stop, fault or disable).
  - *Disable pump* / *Enable pump*: opens or closes Aux. A back-off in progress is kept while disabled.
  - *Reset back-off*: returns to the initial duration and resumes immediately.
  - *End back-off now* (only during a back-off): resumes immediately but keeps the escalated duration.
  - *Sign out*.

States:

| State    | Aux    | Meaning                                              |
| -------- | ------ | ---------------------------------------------------- |
| Settling | closed | Aux just closed; alarm ignored until the delay passes or the pump starts running |
| Armed    | closed | Normal. Any alarm triggers a fault and back-off      |
| Backoff  | open   | Waiting until the retry time                         |
| Disabled | open   | Manually disabled                                    |

## Architecture

```
src/
  main.cpp        pins, NTP, 100 ms control loop, BOOT button, OTA self-confirmation
  wifi.*          Wi-Fi driver, saved network, serial setup prompt
  controller.*    pure state machine (no ESP dependencies, host-testable)
  app.*           owns controller, persistence, event log, daily history, credentials; mutex-guarded
  web.*           HTTPS server, Basic auth, JSON API, OTA upload, embedded UI files
  cert.*          generates and stores a self-signed ECDSA certificate on first boot
  storage.h       NVS blob helpers
  led.*           WS2812 status LED
web/              Preact + Vite UI; built into data/ and embedded in the firmware
tools/            build_web.py (pre-build step that builds the UI)
test/             Unity tests for the controller
```

Flash layout (16 MB): `nvs`, `otadata`, `phy_init`, and two 3 MB app slots `ota_0` and `ota_1`.

Data flow: the control loop reads the Run and Alarm GPIOs every 100 ms, calls `App::tick`, which runs `Controller::tick`, and then sets the Aux GPIO from `Controller::auxClosed()`. HTTP handlers take the same mutex.

Controller details:

- Inputs are debounced (Run 0.5 s, Alarm 0.5 s). A Run drop is confirmed as a clean stop only after 3 s with no alarm, because the drive can drop Run slightly before raising Alarm.
- A new pump start resets the per-demand gallons and runtime, unless the pump restarted within the 3 s stop-confirm window. They are not reset by a stop, so the last run stays visible.
- **Capacity estimate** (0 to 2000 gal, `kMaxCapacityGal`). It rises by the pump rate (gal/min) while the pump runs and is corrected to a known level at two trusted moments, because the well has other water sources and the estimate drifts:
  - **Low level** (default 1400 gal) when the pump starts with no fault or back-off in play, meaning the state is armed. A start while the device is still settling (after boot, enable, or a back-off) does not count, nor does a restart within the 3 s stop-confirm window.
  - **High level** (default 1700 gal) when the pump goes from running to off without a fault, which is the same event that resets the back-off.
  - Both levels are settings; low must be below high and neither can exceed 2000. With nothing saved yet the estimate starts at the low level. Nothing lowers it while the pump is off, since outflow is not measured.
  - Capacity history: a sample is saved just before and just after each correction (so the graph shows a clean step), and every 10 minutes while the pump runs. Samples need the wall-clock time, so none are recorded before NTP syncs. The newest 256 are kept, written to flash about once a minute when there is something new.
- The back-off end is stored as a wall-clock epoch time. If the clock is not yet set after a reboot (NTP not synced), the device stays in back-off until it is, so it fails safe.
- Aux relay output is high (relay energized, pump stopped) from the first instructions of `app_main`. Before that, during the bootloader, the pin floats and the pump may be allowed to run briefly.

Persistence (NVS, namespace `pump`): settings, controller state (`persist`), the capacity estimate (`capacity`), capacity history (`caphist`, the last 256 samples), event log (`events2`), 90 days of daily gallons (`days`), credentials (`cred`, salted SHA-256), and the TLS certificate and key. Structural changes are saved immediately and run-time counters about once a minute, to limit flash wear.

HTTP API (all `/api` routes require Basic auth):

| Method  | Path              | Purpose                                                 |
| ------- | ----------------- | ------------------------------------------------------- |
| GET     | `/api/status`   | Current state and counters                              |
| GET/PUT | `/api/settings` | Read or update settings                                 |
| POST    | `/api/control`  | `{"action": "enable" \| "disable" \| "reset" \| "skip"}` |
| GET     | `/api/log`      | Event log                                               |
| DELETE  | `/api/log`      | Clear the event log                                     |
| GET     | `/api/history`  | Daily gallons                                           |
| GET     | `/api/capacity` | Capacity samples (`t`, `gal`), oldest first             |
| POST    | `/api/password` | `{"password": "..."}`                                 |
| POST    | `/api/firmware` | Raw `.bin` body; installs it and restarts               |
| GET     | `/*`            | Static UI files (public; the login page needs them)     |

## Bench test (drive disconnected)

Wire the relay module (VCC to 5V, GND to GND, IN to GPIO 6, trigger jumper High) with a multimeter across COM and NC.

1. **Boot state:** with the board unpowered the contact (COM-NC) is closed. After power-up the firmware energizes the relay (contact opens) and releases it once the settle begins, so it is closed again within a second or two. Unplugging the board closes it.
2. **Login:** sign in with `admin` / `admin`. A wrong password shows an error. Change the password and sign in again.
3. **Run sense:** touch a jumper from GPIO 4 to GND. The Pump tile shows On and the log records *Pump on*. Remove it, and the log shows *Pump off* with the run time and gallons, and the dashboard keeps those numbers.
4. **Fault and back-off:** touch GPIO 5 to GND. Within about a second the contact opens (relay energizes), Control shows Backoff, Pump shows Fault, and the log records a fault with a 48 h back-off. Use *End back-off now*, trigger again, and confirm the next back-off doubles (96 h, then 192 h). *Reset back-off* returns it to 48 h.
5. **Early fault:** within 60 s of boot, start the pump (GPIO 4) and then raise the alarm (GPIO 5). The fault is reported straight away.
6. **Manual control:** tap the Control icon or use the menu to disable and enable. The contact opens (relay energized) when disabled and closes when enabled.
7. **Reboot persistence:** reset the board during a back-off. It should come back in Backoff with the countdown kept and the contact open.
8. **Short back-off (optional):** set the initial back-off to 0.01 hours (36 s), trigger a fault, and confirm it ends by itself. Set it back to 48.

## Limitations and notes

- The drive does not report which fault occurred, so any alarm is treated the same.
- Gallons are an estimate (run time x gal/min), not a measurement.
- Daily buckets use UTC. The browser displays dates in local time.
- Updates are over USB or Wi-Fi (see above). Images are not signed.
- The Wi-Fi password is stored in the device's flash (NVS) in plain text. It is not in the firmware image or the repository, but anyone with physical access to the board could read it. Flash encryption is not enabled.
- The self-signed certificate is generated once and kept in flash. Erasing flash makes a new one. It has no Subject Alternative Name, so browsers warn about a name mismatch even if you install it. Planned: add a SAN with an mDNS hostname.
- Static UI files are served without a login so the login page can load. All data is behind `/api`.
- The default login is `admin` / `admin`. Change it on first use.
- To factory-reset stored data: `& $pio run -e esp32s3 -t erase`, then upload again.

## Troubleshooting

| Symptom                                  | Check                                                                                |
| ---------------------------------------- | ------------------------------------------------------------------------------------ |
| `Could not open COM3` on upload        | Use the CH340 port (`upload_port` in `platformio.ini`); close the serial monitor |
| Wi-Fi setup says "wrong password?" or reason 15 | Wrong password, 5 GHz-only or WPA3-only network, or MAC filtering |
| Wi-Fi setup says "network not found"     | Check the name (it is case sensitive) and that the network is 2.4 GHz |
| No prompt appears in the monitor         | The prompt only appears when no network is saved. Hold the BOOT button for 2 s to open it |
| No IP in the log                         | Wi-Fi not connected; see the disconnect reason in the log                            |
| Blank page or 404                        | The UI is embedded at build time. Make sure `npm run build` succeeded (check the build output) and re-upload the firmware |
| Firmware upload says "Not a firmware image for this device" | Upload `.pio/build/esp32s3/firmware.bin` from this project, not a bootloader or other `.bin` |
| Firmware upload says the pump is running | Try again when the pump is idle |
| Pump won't start after enabling          | Aux contact wiring, trigger jumper on High, drive set to "Fault with Low", and the settle delay (60 s) |
| Rebuilds everything when only the port changed | Editing `platformio.ini` triggers a full rebuild; use `--upload-port` on the command line |
| Browser warns about the certificate      | Expected for a self-signed cert; accept it once (see Limitations) |
| `MissingPackageManifestError` on build | Delete the half-downloaded folder in `~/.platformio/packages` and rebuild          |
