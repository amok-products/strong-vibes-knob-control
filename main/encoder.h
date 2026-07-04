// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * Rotary encoder driver — generates MIC datagrams on rotation.
 *
 * GPIO8 (A), GPIO7 (B), polled at 3ms.
 * Each detent adjusts mic_value and sends a Motor.MIC datagram
 * to the EMW-APP via ble_client_forward().
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Start encoder polling timer.  Call from app_main. */
void encoder_init(void);

/** Current MIC intensity (0–100). */
uint8_t encoder_get_mic_value(void);

/**
 * Re-sync the knob's internal intensity to an externally-reported level.
 *
 * The knob keeps its own MIC counter to step from on each detent. When the wand
 * (or a connected Webapp) reports a different intensity the two would otherwise
 * drift apart — the screen showing one value while the wand runs at another. The
 * bridge calls this to realign the counter, but only after the post-turn window
 * (encoder_recently_commanded) has elapsed, so an in-progress turn always wins.
 * Values above the 0–100 range (e.g. the 0xFF "unchanged" sentinel) are ignored.
 */
void encoder_set_mic_value(uint8_t value);

/**
 * True for a short window after the knob last changed the intensity locally.
 *
 * The bridge uses this to avoid repainting the on-screen level from a laggy wand
 * echo while the user is actively turning the knob (which otherwise fights the
 * local value and jitters).
 */
bool encoder_recently_commanded(void);
