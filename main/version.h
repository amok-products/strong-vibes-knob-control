// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/* main/version.h */
/**
 * Firmware release version for the Knob ESP32-S3 controller.
 *
 * This is the human-facing RELEASE version (support, changelog, regression
 * attribution). It is deliberately SEPARATE from the Vibe wire-protocol
 * constants in vibe_protocol.h (VIBE_DATAGRAM_VER / VIBE_OPCODE_VERSION),
 * which are on-the-wire values and must not be conflated with this.
 *
 * Bump this on EVERY functional change and add a matching CHANGELOG.md entry
 * (see CONTRIBUTING.md). The top CHANGELOG entry must match this string.
 */
#pragma once

#define SV_KNOB_FW_VERSION      "1.3.9"
