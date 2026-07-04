# Contributing to Strong Vibes Knob Control

Thanks for your interest in contributing! This is ESP-IDF firmware for the
Europe Magic Wand Knob — an ESP32-S3-Knob-Touch-LCD board (GUITION or Waveshare hardware).

## Ground rules

- By contributing you agree your contributions are licensed under
  **Apache-2.0** (see `LICENSE`), and you certify the **Developer Certificate
  of Origin** (below) by signing off your commits.
- Be respectful — see `CODE_OF_CONDUCT.md`.
- This firmware drives a **motor**; changes that affect motor or BLE behavior
  must be tested on real hardware before they are proposed for merge.

## Developer Certificate of Origin (DCO)

We use the DCO. Every commit must be signed off:

    git commit -s -m "your message"

which appends a line:

    Signed-off-by: Your Name <your@email>

By signing off you certify the DCO (https://developercertificate.org/) —
in short, that you wrote the change or have the right to submit it under the
project's open-source license.

## License headers

New first-party source files must start with:

    // SPDX-License-Identifier: Apache-2.0
    // SPDX-FileCopyrightText: <year> <your name or org>

Do **not** add or alter copyright headers on vendored third-party files (e.g.
`components/user_encoder_bsp/src/*`); leave their upstream headers intact and
credit them in `THIRD_PARTY.md`.

## Versioning

This firmware uses [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`.

The single source of truth is `SV_KNOB_FW_VERSION` in `main/version.h`. It is a
RELEASE version and is intentionally separate from the Vibe wire-protocol
constants (`VIBE_DATAGRAM_VER`, `VIBE_OPCODE_VERSION`) in `main/vibe_protocol.h`
— never conflate the two.

On every functional change:

1. Bump `SV_KNOB_FW_VERSION` in `main/version.h`
   (PATCH for fixes, MINOR for backward-compatible features, MAJOR for breaking).
2. Add a matching top entry to `CHANGELOG.md`. The top CHANGELOG version must
   equal `SV_KNOB_FW_VERSION`.
3. When cutting a release, tag it `vX.Y.Z` on the merge commit.

Even defensive / no-behavior-change edits get a PATCH bump so builds are
attributable during hardware regression testing.

## Build & test

    idf.py set-target esp32s3
    idf.py build
    idf.py -p <PORT> flash monitor

Verify the firmware builds cleanly and, for motor/BLE changes, that it behaves
correctly on hardware before opening a pull request.

## Pull requests

- Branch from the default branch; keep PRs focused.
- Reference any related issue.
- **Open a pull request — do not push directly to the default branch.** A
  maintainer merges after review; there is no auto-merge.
- Fill in the PR template.
