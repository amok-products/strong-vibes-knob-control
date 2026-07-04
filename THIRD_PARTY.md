# Third-Party Software

Strong Vibes Knob Control bundles and depends on third-party software. Each is
distributed under its own license; those licenses govern the corresponding
files and are not superseded by this project's Apache-2.0 license.

## Vendored in this repository

### components/user_encoder_bsp/src/bidi_switch_knob.{c,h}
Rotary-encoder driver derived from Espressif's knob component, modified for this
project.
- **Copyright:** 2016-2024 Espressif Systems (Shanghai) CO LTD
- **License:** Apache-2.0 (SPDX headers retained verbatim in the files)
- These files are **not** relicensed under this project's copyright.

## Managed dependencies (resolved by `idf.py` from `main/idf_component.yml`)

| Component | Source | License |
| --- | --- | --- |
| `espressif/button` | Espressif Component Registry | Apache-2.0 |
| `espressif/esp_lcd_touch_cst816s` | Espressif Component Registry | Apache-2.0 |
| `espressif/esp_lvgl_port` | Espressif Component Registry | Apache-2.0 |
| `espressif/esp_lcd_st77916` | Espressif Component Registry | Apache-2.0 |
| `lvgl/lvgl` | Espressif Component Registry (LVGL Kft) | MIT |

## Platform

This firmware is built on **ESP-IDF** (Espressif IoT Development Framework),
which bundles, among others:
- **Apache NimBLE** BLE host stack — Apache-2.0
- **FreeRTOS** kernel — MIT

ESP-IDF, its components, and the toolchain are distributed under their
respective licenses (predominantly Apache-2.0 and MIT). See the ESP-IDF
distribution for full details.

---

Each managed dependency's authoritative license text ships in its own package
under `managed_components/<namespace>__<name>/LICENSE` after a build. The table
above reflects the license declared by those packages.
