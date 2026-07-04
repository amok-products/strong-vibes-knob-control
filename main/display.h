// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * ST77916 LCD Display + LVGL integration + CST816 Touch.
 *
 * 360x360 QSPI LCD on GPIO13-18, RST=21, backlight PWM=47.
 * CST816 touch on I2C GPIO11/12 (addr 0x15).
 * 3-screen tileview: Patterns | Main | Settings
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Knob control mode (stored from settings screen). */
typedef enum {
    KNOB_MODE_POWER = 0,
    KNOB_MODE_SPEED,
    KNOB_MODE_POWER_AND_SPEED,
} knob_mode_t;

/** Initialize display + touch hardware, LVGL port, create UI screens. */
void display_init(void);

/** Update the MIC value shown on screen.  Thread-safe. */
void display_set_mic(int value);

/** Update BLE connection status labels. */
void display_set_ble_status(bool webapp_connected, bool emw_connected);

/** Flash the BLE activity indicator. */
void display_notify_ble_activity(void);

/** Update motor state info. state: 0=idle, 1=repeat, 2=non-repeat */
void display_set_motor_state(uint8_t state, uint8_t pattern_id);

/** Get current knob control mode from settings screen. */
knob_mode_t display_get_knob_mode(void);

/** Show a modal asking the user to confirm connecting to a discovered wand.
 *  Thread-safe; a no-op until the display is initialized. */
void display_show_connect_prompt(const char *name);

/** Hide the connect prompt if it is showing.  Thread-safe. */
void display_hide_connect_prompt(void);
