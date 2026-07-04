# Strong Vibes Knob Control

ESP-IDF / NimBLE firmware for the **Europe Magic Wand Knob** — an ESP32-S3-Knob-Touch-LCD-1.8 board (GUITION or Waveshare hardware).

It turns the Knob into a **BLE bridge + tactile controller** for a Strong-Vibes / Vibe-Protocol
motor device: it connects to the device as a BLE client, re-exposes the same GATT service to a
browser or app as a BLE server (transparent inspect-and-forward), and adds standalone control via
the rotary encoder, touch LCD, and haptic driver.

> **Start with your AI assistant.** This repo is designed so an AI coding assistant can take you
> from a fresh clone to a flashed knob. Jump to [Start with your AI assistant](#start-with-your-ai-assistant).

---

## Hardware target

| | |
|---|---|
| Board | **Europe Magic Wand Knob** — ESP32-S3-Knob-Touch-LCD-1.8 (GUITION or Waveshare hardware) |
| MCU | ESP32-S3 (Xtensa LX7 dual-core, 240 MHz) — the one we flash |
| Co-processor | ESP32 (audio only — no firmware needed here) |
| Display | ST77916 LCD, 360×360, QSPI |
| Touch | CST816, I²C 0x15 (GPIO11 SDA / GPIO12 SCL) |
| Haptic | DRV2605L, I²C 0x5A (shared bus with touch) |
| Encoder | Rotary, GPIO8 (A) / GPIO7 (B), polled |
| BLE | NimBLE, simultaneous server + client |

---

## Prerequisites

- **ESP-IDF v5.3 or newer**. Install: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/>
- A **USB-A → USB-C** cable (see the USB-C gotcha below — a C-to-C cable will fight you).
- `python`, `git` (installed by the ESP-IDF installer).

Verify your toolchain:

```bash
idf.py --version
```

---

## Build & flash quickstart

```bash
# 1. Clone
git clone https://github.com/amok-products/strong-vibes-knob-control.git
cd strong-vibes-knob-control

# 2. Set the target (first time only)
idf.py set-target esp32s3

# 3. Build — component dependencies auto-resolve from main/idf_component.yml
idf.py build

# 4. Flash + open serial monitor (pick the right port — see USB-C gotcha)
idf.py -p PORT flash monitor      # e.g. PORT=/dev/cu.usbmodemXXXX
```

Exit the monitor with `Ctrl+]`.

### Dependencies (auto-resolved)

You do **not** need to run `idf.py add-dependency` on a fresh clone — the checked-in manifest
`main/idf_component.yml` pulls everything during `idf.py build`. For reference, it declares:

| Component | Purpose |
|---|---|
| `espressif/esp_lcd_st77916` | ST77916 LCD QSPI driver |
| `espressif/esp_lvgl_port` | LVGL integration for ESP-IDF |
| `lvgl/lvgl` (v9) | UI framework |
| `espressif/esp_lcd_touch_cst816s` | Touch controller |
| `espressif/button` | Button input helper |

The rotary-encoder driver (`user_encoder_bsp`, a Waveshare bidi_switch_knob port) is **bundled
in-tree** under `components/` — it is not a registry download.

---

## USB-C cable orientation — READ THIS FIRST

The board multiplexes a **single USB-C port between two MCUs** with an on-board analog switch
(CH445P). The **physical orientation of the USB-C plug** selects which MCU you talk to.

- **Use a USB-A → USB-C cable.** USB-C-to-C cables do **not** reliably select the MCU (the CC-pin
  negotiation is unpredictable). With A-to-C, flipping the plug at the USB-C end switches the MCU.

| USB-C orientation | Routes to | Port pattern |
|---|---|---|
| **A** | **ESP32-S3 native USB** — this is what we flash | `/dev/cu.usbmodem*` (macOS) · `/dev/ttyACM*` (Linux) |
| **B** (flip 180°) | ESP32 audio co-processor (via CH343P bridge) | `/dev/cu.wchusbserial*` (macOS) · `/dev/ttyUSB*` (Linux) |

**Verify you're on the ESP32-S3:**

```bash
ls /dev/cu.usb*                 # macOS
ls /dev/ttyACM* /dev/ttyUSB*    # Linux
esptool.py --port PORT chip_id  # should report ESP32-S3
```

If `chip_id` reports plain **ESP32**, flip the USB-C plug 180° and retry.

**If no port appears / flashing fails — enter download mode:** hold **BOOT**, press **RST**, release
**BOOT**, then retry. On macOS you may also need the CH343 serial driver
(<https://github.com/WCHSoftGroup/ch34xser_macos>), authorized under System Settings → Privacy & Security.

---

## Start with your AI assistant

Paste this into your AI coding assistant after cloning:

> You are helping me build and flash firmware for the Europe Magic Wand Knob
> (an ESP32-S3-Knob-Touch-LCD-1.8 board — GUITION or Waveshare hardware). Read `AGENTS.md` in this repo first — it has the verified
> build/flash commands, the hardware target, and the hard rules. Then:
> 1. Confirm my ESP-IDF install (`idf.py --version`, expect v5.3+).
> 2. Run `idf.py set-target esp32s3` then `idf.py build`. Dependencies auto-resolve from
>    `main/idf_component.yml` — do not hand-add them.
> 3. Help me pick the serial port for the **ESP32-S3** (not the ESP32 audio chip) using the
>    USB-C-orientation rules in `AGENTS.md`, then `idf.py -p PORT flash monitor`.
> 4. If the build or flash fails, diagnose from the monitor output and the USB-C / download-mode
>    steps in `AGENTS.md`. Never commit secrets or the forbidden files listed in `AGENTS.md`.

---

## Protocol

This firmware speaks the **Strong Vibes Protocol** (a.k.a. the Vibe Protocol) — a compact,
datagram-based BLE GATT protocol on two characteristics (`0x1225` write / `0x1227` notify) under
service `0x1221`. A synced copy of the spec lives at [`skills/vibe-protocol.md`](skills/vibe-protocol.md).
The **canonical** spec lives in the umbrella repo (see below); treat the in-repo copy as read-only.

---

## Related projects

- **Strong Vibes umbrella** (landing page + canonical protocol spec + builder kit):
  <https://github.com/amok-products/strong-vibes>
- **Strong Vibes Protocol** spec: canonical copy in the umbrella repo above.

---

## Contributing / license

The code is licensed under **Apache-2.0** (see [`LICENSE`](LICENSE)); names and branding are
governed by [`TRADEMARK.md`](TRADEMARK.md), third-party attribution by
[`THIRD_PARTY.md`](THIRD_PARTY.md). See [`AGENTS.md`](AGENTS.md) for the agent/contributor operating
rules and [`CONTRIBUTING.md`](CONTRIBUTING.md) for how to propose changes.
