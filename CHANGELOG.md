# Changelog

All notable changes to the Knob ESP32-S3 firmware are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The version here mirrors `SV_KNOB_FW_VERSION` in `main/version.h`. Bump both
together on every functional change (see `CONTRIBUTING.md`).

## [1.3.9] - 2026-07-04

### Changed
- Broadened the hardware naming so the docs no longer read as Waveshare-exclusive.
  The firmware runs on the **Europe Magic Wand Knob** — an ESP32-S3-Knob-Touch-LCD-1.8
  board sold as GUITION or Waveshare hardware — so the board is now identified that
  way across the README, AGENTS.md, CONTRIBUTING.md, the `main.c` header, and the
  hardware-reference skill. Upstream provenance references are unchanged and still
  credit their origin: the bundled `user_encoder_bsp` is a port of the Waveshare
  `bidi_switch_knob` driver, and the ST77916 init table / hardware reference are
  derived from Waveshare example code and schematics.

## [1.3.8] - 2026-07-04

### Fixed
- Retargeted the display to the correct panel driver IC. The board's panel is an
  **ST77916 LCD** — the firmware drives a real PWM backlight on GPIO47 (a
  self-emissive AMOLED has none) and the init table is the ST77916 register
  profile — but it was being driven through the `waveshare/esp_lcd_sh8601` AMOLED
  component used only as a generic QSPI transport shim. Swapped to the native
  `espressif/esp_lcd_st77916` driver (same init table, QSPI config, and pins).
  Verified on hardware: colors, orientation, and backlight render correctly with
  no regression across cold boots. This is a correctness fix (the right driver
  for the panel IC) — the register stream sent to the panel is unchanged.

### Changed
- Corrected every "SH8601 AMOLED" label to "ST77916 LCD" across code comments and
  docs (README, AGENTS.md, dashboard, THIRD_PARTY.md).

## [1.3.7] - 2026-07-03

### Changed
- Demoted per-datagram BLE bridge traffic logging from info to debug level, so
  the default console no longer prints the forwarded intensity/pattern
  datagrams. Enable a debug log level to restore the traffic dump.
- Dropped a machine-specific local-tool entry (and its comment) from
  `.gitignore` so the public file carries no third-party tool names; that path
  is covered by contributors' global gitignore instead.

### Fixed
- Corrected the repository clone and private security-reporting URLs to the
  public `strong-vibes-knob-control` slug (README, dashboard, issue template).

### Removed
- Editor-specific `.cursorrules` file; its build/flash and contribution
  guidance is already covered by `AGENTS.md`.
- Internal orchestration note not relevant to the public firmware. It was never
  part of a firmware release; the compiled firmware is unaffected.

## [1.3.6] - 2026-07-03

### Changed
- Shortened the post-turn "rotation rules" window (`ECHO_HOLD_US`) from 1 s to
  200 ms. This is how long the knob keeps ownership of the on-screen level after a
  detent before it re-syncs to the wand's/Webapp's reported intensity. 200 ms makes
  the re-sync (and the reconcile backstop from 1.3.5) recover noticeably faster
  after a turn while, on hardware, still being long enough that a laggy wand echo
  does not re-introduce the ± jitter the window was added to prevent.

## [1.3.5] - 2026-07-03

### Fixed
- Fixed the knob showing `0` while the wand kept running (typically at ~2%), both
  standalone and with a Webapp connected. Root cause is transport, not the wand: the
  knob→wand link is an unacknowledged write-without-response, so the final `MIC=0` of
  a down-turn (and its `Control.Reset`) could be **silently dropped** — most often on a
  fast turn when the BLE buffer pool is briefly exhausted. Because the screen is painted
  before the send, and the wand only echoes on a real state change (so a command it never
  received produces no echo), nothing corrected the mismatch: the screen stayed at `0`
  while the wand held its last received value. Three defenses now close this:
  - **Reliable forward.** `ble_client_forward` retries a transient `ENOMEM` (buffer
    shortage) with a short yield instead of dropping the datagram on the first failure.
  - **Idle reconcile.** The bridge now tracks the MIC the wand *actually echoes* (ground
    truth). A periodic check re-asserts `MIC=0` whenever the knob sits at `0` but the wand
    still reports a non-zero level — the only thing that can heal a command the wand never
    received. It is self-terminating (stops once the wand confirms `0`), suppressed during
    the post-turn window, and only enforces the stop, so it never fights a Webapp.
  - **Floor re-send.** Continuing to turn down while already at `0` now re-issues the stop
    immediately, as a manual retry lever on top of the automatic reconcile.
  Also serialized the shared intensity counter with a spinlock to remove a real
  (if rare) dual-core race between the knob-input task and the bridge task.

## [1.3.4] - 2026-07-03

### Fixed
- Made turning the knob to `0` actually stop the wand in standalone use. `MIC=0`
  scales the running flat pattern's output to zero, but the pattern keeps running
  and the wand can idle at a small residual (it "still runs on 2"). When the knob
  reaches `0` and no Webapp is attached — so the knob authored the pattern — it
  now follows the `MIC=0` with a `Control.Reset`, which stops any running pattern
  and returns the wand to idle, and marks its cached wand state known-idle so the
  next turn-up re-arms the flat immediately. A connected Webapp owns pattern
  authorship, so that path is unchanged (the `MIC=0` is passed through and the
  Webapp is left to sequence its own session).

## [1.3.3] - 2026-07-03

### Fixed
- Fixed the knob showing `0` (or any stale value) while the wand kept running at
  a different intensity. The knob kept its own MIC counter to step from on each
  detent but never updated it from inbound traffic, so once the wand or a Webapp
  changed the intensity, the knob's counter drifted out of sync with reality: the
  screen could read one level while the wand ran at another, and the next detent
  stepped from the stale value (e.g. jumping to `0`) instead of the real one. The
  bridge now re-syncs the knob's counter (and the screen) to the wand's/Webapp's
  reported MIC. A turn still wins: for one second after the last detent the
  rotation "rules" and inbound updates are held off (this also lengthens the
  anti-jitter window from the previous 500 ms); once that window elapses the knob
  realigns to the reported intensity. Turning to `0` now sends `MIC=0` that
  matches the wand's actual running state, so it stops as expected.

## [1.3.2] - 2026-07-03

### Fixed
- Stopped the on-screen intensity value from jittering ± while turning the knob
  when a wand is connected. Each detent sends a MIC update to the wand, which
  echoes its state back; the bridge was repainting the label/arc from every such
  echo unconditionally, so during a turn the screen alternated between the knob's
  own (correct, rising/falling) value and the wand's laggy echo of an older value
  — a visible up-and-down flutter. The knob now paints its local value on every
  detent and, for a short window after each turn, the bridge ignores inbound wand
  echoes of the MIC level; once the knob is idle again external updates repaint as
  before. The bridge also no longer renders the protocol's `0xFF` "unchanged"
  sentinel (or any out-of-range value) as a literal level. Diagnosed by capturing
  the raw encoder signal, which confirmed the rotary decoding itself is clean
  (a one-way turn decodes as a monotonic run of same-direction steps) — the
  flutter was entirely in the display-echo path, not the encoder, so the vendor
  encoder driver is unchanged.

## [1.3.1] - 2026-07-02

### Changed
- Optimized the firmware for flash size. The build now compiles for size
  (`-Os`) instead of the default debug (`-Og`), no longer bundles LVGL's
  example/demo code and sample image assets into the product, and drops the
  ~26 LVGL widgets the UI never instantiates (only arc, button, checkbox,
  image, label and tileview are used). Together these reclaim ~145 KB: the app
  binary shrinks from ~977 KB to ~832 KB and free space in the 1 MB app
  partition goes from ~5% to ~19%. No functional or on-screen change — runtime
  behavior is identical; the headroom is for future features. The settings live
  in `sdkconfig.defaults`; re-enable a widget's `CONFIG_LV_USE_*` line there
  before using that widget in the UI, or the build will fail to link.

## [1.3.0] - 2026-07-02

### Added
- Boot splash. On power-on the knob now shows the Strong Vibes brand logo
  centered on a black screen for ~3 seconds, then fades into the normal
  tileview UI. The logo is compiled in as an 8-bit luminance (L8) LVGL image
  (`main/sv_logo.c`, ~29 KB) — chosen over RGB565 to halve the flash footprint,
  lossless for the monochrome mark. The splash is drawn on the LVGL top layer
  above the tiles and removed by a one-shot timer, so it never interferes with
  the running UI underneath. If a wand is discovered while the splash is
  showing, its connect prompt is held until the splash finishes so the splash
  is never covered.

## [1.2.0] - 2026-07-02

### Added
- Ask-before-connecting. The knob no longer auto-connects to the first wand it
  discovers — it pauses and shows an on-screen prompt ("EMW found" + the device
  name) with **Connect** / **Not now** buttons (tap to choose; the knob has no
  physical button, so confirmation is by touch). Tapping **Connect** connects
  and remembers that wand as approved for this power-on, so later drops
  auto-reconnect without re-prompting; tapping **Not now** keeps the knob
  standalone until it reboots. This lets the wand keep full local control until
  the user opts the knob in.

## [1.1.4] - 2026-07-02

### Added
- Best-effort: respond to a wand-reported power-button press. When latched to a
  Vibe client the wand reports its local power-button press as a `Motor.State`
  (idle→running, patternId `0xFF`) but does not drive the motor itself. In
  standalone use the knob now detects that wand-initiated transition into the
  `0xFF` pattern and replies with a flat repeating pattern (`MIC=0xFF` = "leave
  the current level unchanged") so the motor runs. **Caveat:** on-hardware
  testing showed the wand emits *nothing* in its latched-off state (the state
  where the power button "does nothing"), so this only helps when the wand does
  emit the transition — the robust fix is wand-side (report state on every
  press / keep driving the local pattern when a client is connected). Gated to
  standalone; a connected Webapp owns the pattern.
- The knob stamps its own patterns (auto-flat + the response) with a distinct
  `KNOB_PATTERN_ID` (`0x01`, a client id) instead of the wand's server-dictated
  `0xFF`, so the two can be told apart on the wire (removes an ambiguity where
  the knob mistook its own pattern's echo for the wand's button). The on-screen
  motor label still reads "Flat" for both.

### Changed
- Expanded the wand→knob notification log to the full frame and flagged
  non-Vibe (`wand-private`) frames, to aid debugging the dual-protocol link.

## [1.1.3] - 2026-07-02

### Fixed
- Stopped the knob from hijacking a Webapp designer session into the "Flat"
  pattern. Turning the physical knob to adjust intensity while a Webapp was
  streaming patterns could preempt the active pattern with a knob-initiated
  flat (pattern `0xFF`) and get stuck there. The knob now sends intensity-only
  updates whenever a Webapp is connected and never auto-starts its own pattern —
  the Webapp owns pattern authorship. Auto-flat remains available in standalone
  use (no Webapp attached).
- Prevented repeated auto-flat re-fires: the knob now records its own flat as
  the running wand state (previously only an inbound `Motor.State` updated the
  cache, and a knob-initiated flat is not one), so consecutive detents no longer
  each emit a fresh flat datagram.
- Gated standalone auto-flat on a *known* wand state: after boot or an EMW
  reconnect the knob waits for a real `Motor.State` before it can auto-start a
  flat, closing a window where a default/stale cached "idle" could trigger a
  flat with no user intent.

## [1.1.2] - 2026-07-02

### Changed
- Unified every on-screen accent to a single brand blue (`#1EA5DE`). The MIC arc
  and value, the BLE-activity indicator, the "Webapp/EMW: Connected" labels, and
  the running motor state previously used a mix of mint green and a lighter blue;
  they now all use one brand blue via a shared `SV_ACCENT` constant in
  `display.c`. Amber (warning) and red (error) state colors are unchanged.

## [1.1.1] - 2026-07-02

### Fixed
- Corrected the on-screen BLE status labels so the `EMW` label reflects the live
  wand link. The status used a connection predicate that only became true after
  GATT discovery finished; an inbound web-controller connection arriving during
  that window repainted `EMW: --` over an already-connected wand, and nothing
  refreshed it afterwards. Display status now follows the link state
  (`ble_client_is_linked()`), while datagram forwarding still waits for the
  discovered write handle (`ble_client_is_connected()`).
- Fixed a startup race that could assert and reboot the device: the NimBLE host
  task (which begins advertising/scanning and can complete a connection within a
  few milliseconds) was started before `display_init()`. An early GAP callback
  took the LVGL lock before LVGL was initialized. The display and encoder are now
  initialized before the host task starts, guarded by a `display_ready` flag.
- Logged the peer address on an inbound (web-controller) connection to make it
  clear which central connected.

## [1.1.0] - 2026-07-02

### Changed
- Renamed the advertised BLE device name from `EMW-Knob` to `Strong Vibes Knob`
  (17 chars, carried in the scan-response payload). Defined once in
  `VIBE_DEVICE_NAME` (`main/vibe_protocol.h`); the GATT server and the scanner
  self-exclusion reference the macro and follow automatically. Paired with the
  matching `startsWith('Strong Vibes Knob')` detection in the web controller —
  the two must stay byte-identical. The LCD main-tile title already reads
  `Strong Vibes Knob`.

## [1.0.0] - 2026-07-01

Initial public open-source release of the Knob firmware.

### Added
- NimBLE dual-role BLE bridge (peripheral for the web controller + central to
  the wand) with standalone tactile control (rotary encoder + SH8601 AMOLED LCD
  + CST816 touch).
- Firmware release version `SV_KNOB_FW_VERSION`, surfaced on the LCD Settings
  screen and in the boot log, kept distinct from the Vibe wire-protocol
  constants.
- Apache-2.0 `LICENSE`, `NOTICE`, `TRADEMARK.md`, `THIRD_PARTY.md`, and
  community/safety docs (`SECURITY.md`, `CODE_OF_CONDUCT.md`, `CONTRIBUTING.md`,
  `DISCLAIMER.md`); Apache-2.0 SPDX headers on all first-party sources.
- Public `README.md` and `AGENTS.md`; a synced Vibe / Strong Vibes Protocol
  spec mirror with a divergence-check workflow.

### Changed
- Rebranded the firmware's own identity from "EMW Knob" to "Strong Vibes Knob
  Control" (CMake project name, boot log, LCD title, dashboard). The advertised
  BLE device name and all remote Europe Magic Wand references are unchanged.

### Removed
- Internal hub-coupled operating notes and onboarding docs, replaced publicly by
  `AGENTS.md`.
