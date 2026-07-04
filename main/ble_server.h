// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * BLE GATT Server (Peripheral role)
 *
 * Exposes Vibe Service 0x1221 with:
 *   0x1225 Vibe Write [W, Wn]  — receives data from Webapp
 *   0x1227 Vibe Read  [R, N]   — sends notifications to Webapp
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Register GATT services.  Must be called before NimBLE host starts. */
void ble_server_init(void);

/** Start advertising.  Call from on_sync callback. */
void ble_server_start(void);

/** Send notification on 0x1227 to connected Webapp. */
void ble_server_notify(const uint8_t *data, uint16_t len);

/** True if a Webapp is connected to our GATT server. */
bool ble_server_is_connected(void);
