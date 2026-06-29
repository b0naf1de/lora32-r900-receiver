# lora32-r900-receiver

**A standalone [Neptune R900](https://github.com/bemasher/rtlamr/wiki/Protocol#r900-consumption-message) water-meter reader for the ~$15 LilyGO LoRa32 (ESP32 + Semtech SX1276) — no RTL-SDR, no PC.**

It decodes R900 water-meter transmissions entirely on the microcontroller and publishes them to **Home Assistant over MQTT** (with auto-discovery), shows live status on the onboard **OLED**, and **self-calibrates** to a clean radio channel so it's portable to any site.

> **License: GPL-2.0-or-later.** The on-MCU R900 decoder is a port of [rtl_433](https://github.com/merbanan/rtl_433)'s `neptune_r900.c`. See [Credits](#credits--attribution).

---

## Why this exists

R900 meters frequency-hop OOK bursts at 32.768 kbit/s across ~50 channels in the 911–920 MHz band. The "obvious" approach — feed the SX1276's **hardware OOK data slicer** into [rtl_433_ESP](https://github.com/NorthernMan54/rtl_433_ESP) — **does not work for R900**: the slicer's AC-coupled envelope splits R900's sustained multi-chip pulses (the 2×/3× symbols), so frames never validate. This reproduces [rtl_433_ESP issue #87](https://github.com/NorthernMan54/rtl_433_ESP/issues/87). That path decodes *slower* OOK sensors (Acurite, Interlogix, …) fine — just not R900.

The fix used here ("Option B") bypasses the slicer entirely:

1. **Capture the raw RSSI envelope.** Poll `RegRssiValue` (0x11) over raw SPI at ~4 µs/sample (~8 samples per R900 chip). RSSI is *upstream* of the OOK slicer, so a sustained carrier reads continuously high.
2. **Recover bits in software.** Median filter → burst-local threshold → resample at the 30.5176 µs bit clock → **majority-vote per bit** (robust to the RSSI's shallow, asymmetric attack/decay).
3. **Decode the frame on-MCU.** A C port of rtl_433's `neptune_r900` decoder: find the preamble/sync, validate the base-6 payload (a strong implicit integrity check), and extract meter id, consumption, leak flags.

A continuous interferer once sat on the meter's band during development, which led to the **channel survey**: at boot the board sweeps 911–920 MHz, measures each window's RSSI **duty cycle**, and parks on the quietest one — automatically avoiding interferers with no external reference.

---

## What you get

- **Standalone decode** on the ESP32 (~100 ms/burst), no PC or SDR in the loop.
- **Home Assistant MQTT discovery** — each meter appears as its own device (`Neptune R900 (LoRa32) <id>`) with `Consumption` (gal, `total_increasing`), `Leak Now`, and `RSSI` sensors. Consumption is divided by 10 (R900 reports tenths of a gallon).
- **Availability** via MQTT Last-Will + a 5-min retained heartbeat → a `connectivity` binary sensor and `unavailable` entities when the board drops.
- **OLED status display** — WiFi / MQTT / RX indicators, your meter number, leak duration, leak-now (None / Intermittent / Continuous), and a live "updated N s ago" counter.
- **Self-calibrating channel survey** — portable; re-surveys if the channel goes quiet.
- **Captive-portal provisioning** — set WiFi + MQTT + meter id from your phone, no reflash (**double-tap RST** to reconfigure). Compile-time `secrets.h` defaults are optional.
- **Desktop tooling** (`experiments/`) for capturing and validating the decode pipeline against `rtl_433`.

> ⚠️ **Single-channel by nature.** The SX1276 has a 250 kHz max bandwidth and no I/Q, so this watches one ~250 kHz window and catches the meter as it hops by (typically every few minutes). For full broadband, multi-protocol coverage, use an RTL-SDR with [rtlamr](https://github.com/bemasher/rtlamr) / [rtlamr2mqtt](https://github.com/allangood/rtlamr2mqtt). This board is the cheap, low-power, single-meter appliance.

---

## Hardware

- **LilyGO LoRa32** (a.k.a. TTGO LoRa32 / T3) **V2.1.6**, 915 MHz variant — ESP32-PICO-D4 + Semtech SX1276 + onboard SSD1306 OLED. Other SX1276 LoRa32 revisions likely work with minor pin tweaks.
- A 915 MHz antenna (use the right band for your meter).
- USB-C/Micro-USB cable.
- A Neptune R900 water meter within radio range, and an MQTT broker (e.g. the one Home Assistant uses).

---

## Install & run

### 1. Prerequisites
- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).
- This repo: `git clone https://github.com/<you>/lora32-r900-receiver && cd lora32-r900-receiver`

### 2. Configure secrets
The build needs `src/secrets.h` to exist, so copy the template:
```bash
cp src/secrets.h.example src/secrets.h
```
You have **two ways** to provide WiFi/MQTT/meter settings:

- **Captive portal (recommended, no reflash):** leave `secrets.h` blank. On first boot (or whenever
  you **double-tap the RST button** — press it once, then again within ~3 s while the OLED shows
  "Press RST again now") the board hosts a WiFi access point named **`R900-Reader-Setup`**. Join it
  with your phone, the config page opens (or browse to `192.168.4.1`), and enter WiFi SSID/password
  plus MQTT host/port/user/password and meter id. The values are saved to flash and used from then
  on; the board reboots into normal operation. The OLED shows the setup instructions while the
  portal is open.

  *(Why double-tap RST and not a BOOT button? The older micro-USB LoRa32 boards (T3 v1.6.x) have no
  BOOT button. The double-tap flag is stored in flash, since the RST button drives the EN pin — a
  full hardware reset that wipes RTC memory.)*
- **Compile-time defaults:** fill the values into `secrets.h` and they'll be used on first boot
  without the portal. Handy if you're flashing many boards.

`secrets.h` is gitignored — your credentials never get committed.

### 3. Set the serial port
Find your board's port and set it in `platformio.ini` (`upload_port` / `monitor_port`):
```bash
pio device list
```

### 4. Build, flash, monitor
```bash
pio run -t upload      # builds the default (optionb) env and flashes
pio device monitor -b 115200
```
At boot you'll see the channel survey, WiFi/MQTT connect, then `R900 DECODE …` lines as meters are heard. The OLED shows live status immediately.

### 5. Home Assistant
With MQTT discovery enabled in HA (default), the sensors appear automatically under devices named **Neptune R900 (LoRa32) &lt;id&gt;** and **R900 LoRa32 Reader** (the connectivity sensor). No YAML needed.

A reference set of HA helpers/automations for "last reading time", a 1-hour staleness binary sensor, and offline email alerts is described in [`docs/home-assistant.md`](docs/home-assistant.md).

---

## Configuration reference

Most behavior is tunable via `#define`s near the top of `src/main_optionb.cpp`:

| Setting | Default | Meaning |
|---|---|---|
| `SCAN_LO` / `SCAN_HI` / `SCAN_STEP` | 911 / 920 / 0.25 MHz | Channel-survey band & step |
| `PRE` | 700 | Pre-trigger samples kept (captures the preamble onset) |
| `RX_ACTIVE_MS` | 600000 | OLED "RX" indicator window (any decode within N ms) |
| `MY_METER_ID` | from `secrets.h` | Meter tracked on the OLED |
| MQTT topics | `lora32r900/<id>/state`, `lora32r900/availability` | retained |

The duty filter in `loop()` (`above >= 700 && above <= 9000`) rejects continuous-carrier interferers; widen if your packets differ.

---

## Repository layout

```
src/main_optionb.cpp   The firmware (capture + DSP + R900 decode + WiFi/MQTT/OLED)
src/main.cpp           Experimental rtl_433_ESP hardware-slicer path (does NOT decode R900)
src/secrets.h.example  Template for your credentials (copy to secrets.h)
platformio.ini         Two envs; `optionb` is the working one (default)
experiments/           Desktop Python tools used to develop & validate the decoder:
  optionb_bitclock.py    bit-clock-recovery decoder for serial RSSI captures (the reference impl)
  optionb_decode.py      pulse-slice variant
  analyze_raw.py         pulse/gap histogram vs R900 chip timing
  esp_raw_to_ook.py      convert ESP captures to .ook for desktop rtl_433
  trial.py               serial + RTL-SDR oracle trial runner
```

---

## Credits & attribution

This project stands on a lot of prior work:

- **[rtl_433](https://github.com/merbanan/rtl_433)** (GPL-2.0-or-later) — the on-MCU R900 decoder is a port of `devices/neptune_r900.c` by **Jeffrey S. Ruby**. The protocol decode logic, base-6 mapping, and field layout are theirs.
- **[rtl_433_ESP](https://github.com/NorthernMan54/rtl_433_ESP)** by NorthernMan54 (and the [thesavant42 fork](https://github.com/thesavant42/rtl_433_ESP)) — the hardware-slicer path and the SX127x OOK bring-up; where the bitrate root-cause and [issue #87](https://github.com/NorthernMan54/rtl_433_ESP/issues/87) were understood.
- **[rtlamr](https://github.com/bemasher/rtlamr)** by Douglas Hall — the canonical R900 protocol reference (center frequency 912.38 MHz, packet structure).
- **[Entropy512/r900_esphome](https://github.com/Entropy512/r900_esphome)** — prior embedded R900-on-ESP art and discussion of the high-bitrate OOK limitation.
- **[RadioLib](https://github.com/jgromes/RadioLib)** by Jan Gromeš (MIT) — SX1276 driver / low-level register access.
- **[PubSubClient](https://github.com/knolleary/pubsubclient)** by Nick O'Leary (MIT) — MQTT client.
- **[U8g2](https://github.com/olikraus/u8g2)** by Oliver Kraus (BSD-2-Clause) — OLED graphics.
- **[WiFiManager](https://github.com/tzapu/WiFiManager)** by tzapu (MIT) — captive-portal WiFi/MQTT provisioning.
- **[Home Assistant](https://www.home-assistant.io/)** — MQTT discovery conventions.
- **[PlatformIO](https://platformio.org/)** + **[arduino-esp32](https://github.com/espressif/arduino-esp32)** — build & runtime.

If your meter is read by an RTL-SDR today, thank **rtlamr** / **[rtlamr2mqtt](https://github.com/allangood/rtlamr2mqtt)** — this project is a cheaper, single-meter complement, not a replacement.

---

## License

GPL-2.0-or-later. See [LICENSE](LICENSE). Because the R900 decoder derives from rtl_433 (GPL-2.0-or-later), this project is licensed under the same terms.
