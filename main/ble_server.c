// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * BLE GATT Server — Peripheral role for Webapp connections.
 *
 * Advertises Vibe Service 0x1221.  When the Webapp writes to 0x1225,
 * the data is posted to the bridge queue for forwarding to the EMW-APP.
 */

#include "ble_server.h"
#include "ble_client.h"
#include "bridge.h"
#include "display.h"
#include "vibe_protocol.h"

#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble-srv";

/* ── UUID instances ─────────────────────────────────────────────── */

static const ble_uuid128_t svc_uuid       = VIBE_SVC_UUID;
static const ble_uuid128_t chr_write_uuid  = VIBE_CHR_WRITE_UUID;
static const ble_uuid128_t chr_read_uuid   = VIBE_CHR_READ_UUID;

/* ── Connection state ───────────────────────────────────────────── */

static uint16_t conn_handle;
static uint16_t read_attr_handle;   /* value handle for 0x1227 notifications */
static bool     connected;

/* ── GATT access callbacks ──────────────────────────────────────── */

static int vibe_write_cb(uint16_t ch, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_UNLIKELY;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > VIBE_MAX_DATAGRAM)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    bridge_evt_t evt = {
        .type = BRIDGE_EVT_WEBAPP_WRITE,
        .len  = len,
    };
    os_mbuf_copydata(ctxt->om, 0, len, evt.data);
    bridge_post(&evt);

    return 0;
}

static int vibe_read_cb(uint16_t ch, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Read returns empty; real data arrives via notifications. */
    return 0;
}

/* ── GATT service definition ────────────────────────────────────── */

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid       = &chr_write_uuid.u,
                .access_cb  = vibe_write_cb,
                .flags      = BLE_GATT_CHR_F_WRITE |
                              BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid       = &chr_read_uuid.u,
                .access_cb  = vibe_read_cb,
                .val_handle = &read_attr_handle,
                .flags      = BLE_GATT_CHR_F_READ |
                              BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

/* ── Forward declarations ───────────────────────────────────────── */

static int ble_server_gap_event(struct ble_gap_event *event, void *arg);

/* ── Advertising ────────────────────────────────────────────────── */

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128          = &svc_uuid;
    fields.num_uuids128      = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    /* Put name in scan response to save space in ADV packet for UUID */
    struct ble_hs_adv_fields rsp = { 0 };
    const char *name = ble_svc_gap_device_name();
    rsp.name             = (uint8_t *)name;
    rsp.name_len         = strlen(name);
    rsp.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &params, ble_server_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    }
}

/* ── GAP event handler (server connections) ─────────────────────── */

static int ble_server_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            struct ble_gap_conn_desc desc;
            conn_handle = event->connect.conn_handle;
            connected   = true;
            /* Log the peer address so it is clear WHO connected inbound (the
               web controller, a phone, or another Vibe central). */
            if (ble_gap_conn_find(conn_handle, &desc) == 0) {
                ESP_LOGI(TAG, "Webapp connected (h=%d) peer=%02x:%02x:%02x:%02x:%02x:%02x",
                         conn_handle,
                         desc.peer_id_addr.val[5], desc.peer_id_addr.val[4],
                         desc.peer_id_addr.val[3], desc.peer_id_addr.val[2],
                         desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
            } else {
                ESP_LOGI(TAG, "Webapp connected (h=%d)", conn_handle);
            }
            /* Use the LINK state for the EMW label, not is_connected(): during
               GATT discovery the wand link is up but the write handle is not
               yet known, and we must not repaint "EMW: --" over a live link. */
            display_set_ble_status(true, ble_client_is_linked());
        } else {
            ESP_LOGW(TAG, "Webapp connect failed: %d", event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        connected = false;
        ESP_LOGI(TAG, "Webapp disconnected (reason=%d)",
                 event->disconnect.reason);
        /* Link state (not is_connected()) so a webapp drop mid-discovery does
           not repaint "EMW: --" over a live wand link. */
        display_set_ble_status(false, ble_client_is_linked());
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: attr=%d notify=%d indicate=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU: conn=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────── */

void ble_server_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svcs);
    assert(rc == 0);

    ble_svc_gap_device_name_set(VIBE_DEVICE_NAME);
    ESP_LOGI(TAG, "GATT server registered (%s)", VIBE_DEVICE_NAME);
}

void ble_server_start(void)
{
    start_advertising();
    ESP_LOGI(TAG, "Advertising started");
}

bool ble_server_is_connected(void)
{
    return connected;
}

void ble_server_notify(const uint8_t *data, uint16_t len)
{
    if (!connected) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGE(TAG, "mbuf alloc failed for notify");
        return;
    }

    int rc = ble_gatts_notify_custom(conn_handle, read_attr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Notify failed: %d", rc);
    }
}
