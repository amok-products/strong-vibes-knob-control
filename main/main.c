// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * Strong Vibes Knob Control — BLE Bridge + Tactile Controller
 *
 * ESP32-S3 firmware for the Europe Magic Wand Knob
 * (ESP32-S3-Knob-Touch-LCD-1.8 board — GUITION or Waveshare hardware).
 * Acts as a BLE bridge between the Vibe Webapp and EMW-APP device,
 * with standalone tactile control via rotary encoder and touch LCD.
 *
 * BLE Architecture:
 *   - NimBLE Server (Peripheral): Exposes Vibe Service 0x1221 for Webapp
 *   - NimBLE Client (Central):    Connects to EMW-APP's Vibe Service 0x1221
 *   - Bridge Task:                Inspect-and-forward between the two roles
 *
 * FreeRTOS Tasks:
 *   1. NimBLE host task (auto)  — BLE stack, GAP, GATT
 *   2. Bridge task              — Event queue, datagram routing
 *   3. UI task (Phase 7)        — Encoder, touch, LVGL, haptics
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"

#include "ble_server.h"
#include "ble_client.h"
#include "bridge.h"
#include "encoder.h"
#include "display.h"
#include "version.h"

static const char *TAG = "knob-ctl";

/* ── NimBLE host callbacks ──────────────────────────────────────── */

static void on_sync(void)
{
    /* Ensure we have a usable BLE address */
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);

    ESP_LOGI(TAG, "BLE host synced — starting server + client");
    ble_server_start();
    ble_client_start();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset: reason=%d", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();              /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* ── Entry point ────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "Strong Vibes Knob Control starting... (fw %s)", SV_KNOB_FW_VERSION);

    /* Reduce log noise: only warnings+ from chatty components */
    esp_log_level_set("Knob", ESP_LOG_WARN);
    esp_log_level_set("bridge", ESP_LOG_INFO);
    esp_log_level_set("encoder", ESP_LOG_WARN);
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
    esp_log_level_set("display", ESP_LOG_WARN);

    /* NVS init — used by the NimBLE host for its persistent storage */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* NimBLE controller + host init (ESP-IDF 5.x) */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %d", ret);
        return;
    }

    /* Host configuration */
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* Preferred ATT MTU — ensures 121-byte datagrams fit */
    ble_att_set_preferred_mtu(256);

    /* Phase 4+5: Register GATT server (must happen before host start) */
    ble_server_init();
    ble_client_init();

    /* Phase 6: Bridge task + event queue */
    bridge_init();

    /* ST77916 LCD display + LVGL status screen.
     * Must come up BEFORE the NimBLE host task: on_sync() begins advertising
     * and scanning, and an inbound/outbound connection can complete within a
     * few ms — the GAP callbacks then call display_set_ble_status(), which
     * takes the LVGL lock. If LVGL is not yet initialized that asserts and the
     * device reboots. Initialize every subsystem a GAP callback may touch
     * first, then start the host task last. */
    display_init();

    /* Encoder: rotation → MIC datagrams to EMW-APP */
    encoder_init();

    /* Haptic feedback (DRV2605L) is not yet wired up. */

    /* Start NimBLE host task LAST — advertising + scanning (and therefore the
     * first connections) begin in on_sync, after the display is ready. */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "Strong Vibes Knob Control initialized");
}
