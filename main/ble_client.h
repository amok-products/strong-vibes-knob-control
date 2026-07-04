// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * BLE GATT Client (Central role)
 *
 * Scans for and connects to the EMW-APP device's Vibe Service 0x1221.
 *   Writes to 0x1225  — forward datagrams to EMW-APP
 *   Subscribes to 0x1227 — receive notifications from EMW-APP
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Nothing critical before host start; placeholder for symmetry. */
void ble_client_init(void);

/** Begin scanning for EMW-APP.  Call from on_sync callback. */
void ble_client_start(void);

/** Write data to EMW-APP's 0x1225 (Write Without Response). */
void ble_client_forward(const uint8_t *data, uint16_t len);

/** True if connected to EMW-APP and service discovery is complete
 *  (i.e. ready to forward datagrams — the write handle is known). */
bool ble_client_is_connected(void);

/** True as soon as the GAP link to EMW-APP is up, before discovery completes.
 *  Use for the on-screen "EMW" connection status, not for forwarding. */
bool ble_client_is_linked(void);

/** User tapped "Connect" on the discovery prompt: connect to the pending wand
 *  and remember it as approved for this power-on. Call from the UI. */
void ble_client_confirm_connect(void);

/** User tapped "Not now" on the discovery prompt: stay standalone until the
 *  knob reboots. Call from the UI. */
void ble_client_dismiss_connect(void);
