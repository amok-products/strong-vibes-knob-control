// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * Bridge — inspect-and-forward between BLE server and client roles.
 *
 * FreeRTOS task sits on an event queue.  When the Webapp writes to the
 * server's 0x1225, the bridge forwards to the EMW-APP via the client's
 * 0x1225.  When EMW-APP notifies on the client's 0x1227, the bridge
 * forwards to the Webapp via the server's 0x1227.  Bytes are never
 * modified in transit.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "vibe_protocol.h"

typedef enum {
    BRIDGE_EVT_WEBAPP_WRITE,   /* server 0x1225 → forward to EMW client */
    BRIDGE_EVT_EMW_NOTIFY,     /* client 0x1227 → forward to Webapp server */
} bridge_evt_type_t;

typedef struct {
    bridge_evt_type_t type;
    uint16_t len;
    uint8_t  data[VIBE_MAX_DATAGRAM];
} bridge_evt_t;

/** Create queue + bridge task.  Call once from app_main. */
void bridge_init(void);

/** Post an event (non-blocking, drops on full queue). */
void bridge_post(const bridge_evt_t *evt);

/** Get last known EMW motor state (0=idle, 1=repeat, 2=non-repeat). */
uint8_t bridge_get_emw_motor_state(void);

/** True once a Motor.State from the wand has been observed since the last
 *  (re)connect. Until then the cached state is only a power-on default and
 *  must not be trusted to auto-start a pattern. */
bool bridge_emw_state_known(void);

/** Last MIC (0-100) the WAND itself actually reported (from an inbound
 *  Motor.State/Motor.MIC echo — never from a local optimistic mark). This is the
 *  ground truth of what the wand is really running at, used to detect and heal a
 *  command the wand never received (e.g. a dropped MIC=0 stop). */
uint8_t bridge_get_emw_mic(void);

/** True once the wand has actually echoed a MIC level since the last (re)connect. */
bool bridge_emw_mic_known(void);

/** Note that the knob itself just started a (repeating) pattern on the wand,
 *  so the cached state reflects "running" without waiting for a Motor.State
 *  echo back through the bridge. Prevents consecutive detents from each
 *  re-issuing a fresh auto-flat. */
void bridge_note_emw_running(void);

/** Note that the knob just stopped the wand (Control.Reset when turned to 0), so
 *  the cached state is a KNOWN idle immediately — without waiting for the wand's
 *  idle Motor.State to arrive — so the next turn-up can re-arm the auto-flat. */
void bridge_note_emw_idle(void);

/** Forget the cached wand state (call on EMW disconnect). The next auto-start
 *  decision then waits for a fresh Motor.State instead of a stale value. */
void bridge_reset_emw_state(void);
