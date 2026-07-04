# AGENTS.md — operating manual for AI assistants & contributors

This file tells an AI coding assistant (or a new contributor) how to work in this repo safely and
build the firmware without extra context.

## What this repo is

ESP-IDF / NimBLE firmware for the **Europe Magic Wand Knob** — an ESP32-S3-Knob-Touch-LCD-1.8 board (GUITION or Waveshare hardware). It is a BLE bridge
(client to a Vibe-Protocol motor device, server to a browser/app) plus standalone tactile control
(rotary encoder, touch LCD, haptic). Product name: **Strong Vibes Knob Control**.

## Hardware target

- **Flash target:** the **ESP32-S3** (Xtensa LX7). The board also has an ESP32 audio co-processor —
  we do **not** flash it.
- Display: ST77916 LCD 360×360 (QSPI, PWM backlight GPIO47). Touch: CST816 (I²C 0x15). Haptic: DRV2605L (I²C 0x5A).
  Encoder: GPIO8/GPIO7. BLE: NimBLE, simultaneous server + client.

## Verified build & flash commands

```bash
idf.py set-target esp32s3          # first time only
idf.py build                       # deps auto-resolve from main/idf_component.yml
idf.py -p PORT flash monitor       # PORT = the ESP32-S3 port (see below)
```

- **Do not** run `idf.py add-dependency` to build a fresh clone — the manifest already lists every
  component and `idf.py build` fetches them. Only use `add-dependency` when intentionally adding a
  new component (then commit the updated `main/idf_component.yml`).
- The rotary-encoder driver is bundled in-tree under `components/user_encoder_bsp/` — it is not a
  registry download; don't "fix" a phantom missing dependency for it.
- Requires **ESP-IDF v5.3+**.

## USB-C orientation (critical — two MCUs, one port)

The board multiplexes one USB-C port between the ESP32-S3 and the ESP32 audio chip. **Use a
USB-A → USB-C cable**; flipping the plug selects the MCU.

- ESP32-S3 (what we flash): port like `/dev/cu.usbmodem*` (macOS) or `/dev/ttyACM*` (Linux).
- ESP32 audio chip: port like `/dev/cu.wchusbserial*` (macOS) or `/dev/ttyUSB*` (Linux).
- Verify with `esptool.py --port PORT chip_id` → must say **ESP32-S3**. If it says ESP32, flip the
  plug 180°.
- Download mode: hold **BOOT**, press **RST**, release **BOOT**.

## Capturing the boot log without an interactive terminal

`idf.py monitor` needs an interactive TTY and fails in a headless / CI / agent shell (exit code 2).
The ESP32-S3's built-in **USB-serial-JTAG resets together with the chip**, which makes scripted
capture unintuitive. Know these before you try:

- Toggling RTS/DTR from pyserial does **not** reliably reset this chip — reset with **esptool**.
- Any reset drops the USB CDC, so the `/dev` port disappears and re-enumerates (~1 s). You must
  **reopen** the port after the reset.
- Do **not** run esptool and a serial reader at the same time — they collide on the port
  (`multiple access on port`) and can leave the chip stuck in download mode.
- In steady state the app is quiet (it logs only at boot and on BLE events), so you must **reboot**
  it to see the boot banner.

Reliable non-interactive recipe — reset with esptool **alone**, then reopen and read:

1. Reboot into the app with esptool alone (nothing else holding the port):

   ```bash
   python -m esptool --chip esp32s3 -p <PORT> --after hard-reset chip_id
   ```

2. Immediately run a short pyserial script that loops opening `<PORT>` until it re-enumerates, then
   reads for ~10–13 s into a file. `pyserial` ships inside the ESP-IDF Python environment that
   `idf.py` uses (no separate install needed). It captures `app_main()` output onward — enough to
   confirm the project name, firmware version, GATT device name, and BLE connect path.

`<PORT>` is the ESP32-S3 port from the section above.

## BLE-name note

The firmware advertises a fixed BLE device name defined once in `main/vibe_protocol.h`
(`VIBE_DEVICE_NAME` — currently `Strong Vibes Knob`). It is a **contract**. If you ever change the
advertised name:

1. Edit the `VIBE_DEVICE_NAME` string in `main/vibe_protocol.h` — this is the single source of truth.
   The GATT server (`main/ble_server.c`) and the scanner **self-exclusion** in `main/ble_client.c`
   (`strcmp(name, VIBE_DEVICE_NAME)`) both reference the macro, so they follow automatically — do
   **not** re-introduce a hard-coded name or a truncated `strncmp(..., <len>)` length there.
2. Update the **LCD title label** in `main/display.c` (the main-tile title text) — this is a separate
   hard-coded string (kept decoupled so the on-screen title can be shortened independently if it
   clips) and must be changed by hand to match, byte-for-byte.
3. Update the paired **web/browser controller** detection (`startsWith('…')`) in its own repo — a
   cross-repo literal that must be byte-identical to `VIBE_DEVICE_NAME`.

Do not change the name casually; it touches BLE advertising, the bridge discovery path, and the
paired controller, so it requires an on-hardware test.

## Hard rules

- **Never commit secrets** (Wi-Fi creds, tokens, private keys, provisioning data).
- **Never commit build output or local config:** `build/`, `sdkconfig`, `sdkconfig.old`, `.vscode/`,
  `__pycache__/`, `*.pyc`, `.DS_Store`. (These are in `.gitignore`; keep it that way.)
- **Never commit the `managed_components/` directory** the ESP-IDF component manager downloads — it
  is regenerated from `main/idf_component.yml` on build.
- Do **not** add an AI-vendor name or an AI co-author trailer to commits. Generic "AI assistant" /
  "AI agent" phrasing and tool names are fine.
- Keep commits scoped and describe *what* changed and *why*.

## Versioning

Bump `SV_KNOB_FW_VERSION` in `main/version.h` on every functional change and add
a matching top entry to `CHANGELOG.md` (the top changelog version must equal the
macro). It is a RELEASE version, separate from the Vibe wire-protocol constants
in `main/vibe_protocol.h` — never conflate the two. See `CONTRIBUTING.md`.

## Where things live

```
main/            firmware sources (ble_client, ble_server, bridge, encoder, display, main)
main/idf_component.yml   dependency manifest (source of truth for components)
components/      bundled local components (rotary-encoder BSP)
skills/          synced reference docs (Vibe/Strong Vibes protocol, hardware map)
docs/            in-repo dashboard / programming guide
sdkconfig.defaults       committed defaults (NimBLE, PSRAM, target)
```

If you suspect a bug is outside this firmware (protocol spec, a paired client), don't patch around
it — open an issue describing what you found.
