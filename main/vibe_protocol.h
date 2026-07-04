// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * Vibe Protocol v2.0 — Shared definitions
 *
 * 2-characteristic design used uniformly by EMW-APP, Knob, and Webapp.
 * Service 0x1221, Write char 0x1225, Read char 0x1227.
 * Base UUID: 0000xxxx-1313-71be-1221-785dfeabc321
 */

#pragma once

#include <stdint.h>
#include "host/ble_uuid.h"

/* ── Protocol constants ─────────────────────────────────────────────── */

#define VIBE_DATAGRAM_VER       0x10
#define VIBE_MAX_DATAGRAM       121   /* L2CAP 128 → ATT 124 → payload 121 */
#define VIBE_MAX_PAYLOAD        119   /* 121 - 2 byte header */

/* Raw opcodes (first byte != 0x10 on char 0x1225) */
#define VIBE_OPCODE_VERSION     0x0F
#define VIBE_OPCODE_STATUS      0x0B

/* Response markers (first byte on char 0x1227) */
#define VIBE_RESP_ERROR         0xFF

/* Module IDs (byte[2] inside datagram) */
#define VIBE_MOD_CONTROL        0x00
#define VIBE_MOD_MOTOR          0x01

/* Message IDs — Motor module */
#define VIBE_MSG_REPEATING      0x00
#define VIBE_MSG_NONREPEATING   0x01
#define VIBE_MSG_STATE          0x02
#define VIBE_MSG_ERROR_EVT      0x03
#define VIBE_MSG_MIC            0x04

/* Message IDs — Control module */
#define VIBE_MSG_RESET          0x00

/* PatternID the Knob uses for patterns IT authors (auto-flat + the power-on
 * response). Deliberately a client id (1..254), NOT 0xFF: the wand reports its
 * own local power-button pattern as 0xFF ("server-dictated"), so a distinct id
 * lets the knob tell the wand's button apart from the echo of its own pattern. */
#define KNOB_PATTERN_ID         0x01

/* ── 128-bit UUIDs (NimBLE little-endian byte order) ────────────────
 *
 * Base UUID big-endian:  0000xxxx-1313-71be-1221-785dfeabc321
 * Reversed for NimBLE:   21 c3 ab fe 5d 78  21 12  be 71  13 13  xx xx 00 00
 */

/* Service 00001221-1313-71be-1221-785dfeabc321 */
#define VIBE_SVC_UUID                                        \
    BLE_UUID128_INIT(0x21, 0xc3, 0xab, 0xfe, 0x5d, 0x78,    \
                     0x21, 0x12, 0xbe, 0x71, 0x13, 0x13,     \
                     0x21, 0x12, 0x00, 0x00)

/* Write char 00001225-1313-71be-1221-785dfeabc321 */
#define VIBE_CHR_WRITE_UUID                                  \
    BLE_UUID128_INIT(0x21, 0xc3, 0xab, 0xfe, 0x5d, 0x78,    \
                     0x21, 0x12, 0xbe, 0x71, 0x13, 0x13,     \
                     0x25, 0x12, 0x00, 0x00)

/* Read char 00001227-1313-71be-1221-785dfeabc321 */
#define VIBE_CHR_READ_UUID                                   \
    BLE_UUID128_INIT(0x21, 0xc3, 0xab, 0xfe, 0x5d, 0x78,    \
                     0x21, 0x12, 0xbe, 0x71, 0x13, 0x13,     \
                     0x27, 0x12, 0x00, 0x00)

/* Device name advertised by the Knob (scan-response payload).
 * Must stay byte-identical to the web controller's startsWith('Strong Vibes
 * Knob') detection (paired web-app change). 17 chars — fits the 31-byte
 * scan-response budget (2-byte AD header + 17 = 19 bytes). */
#define VIBE_DEVICE_NAME        "Strong Vibes Knob"
