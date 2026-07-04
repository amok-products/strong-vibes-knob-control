// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * BLE GATT Client — Central role connecting to EMW-APP.
 *
 * Scans for a peripheral with name prefix "EMW" (excluding ourselves),
 * connects, discovers Vibe Service 0x1221 and its characteristics,
 * subscribes to 0x1227 notifications, and writes forwarded datagrams
 * to 0x1225.
 *
 * Discovery flow:
 *   scan -> connect -> disc_svc_by_uuid -> disc_all_chrs -> disc_all_dscs
 *   -> write CCCD -> ready
 */

#include "ble_client.h"
#include "ble_server.h"
#include "bridge.h"
#include "display.h"
#include "vibe_protocol.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

static const char *TAG = "ble-cli";

/* -- UUID instances ------------------------------------------------- */

static const ble_uuid128_t svc_uuid       = VIBE_SVC_UUID;
static const ble_uuid128_t chr_write_uuid  = VIBE_CHR_WRITE_UUID;
static const ble_uuid128_t chr_read_uuid   = VIBE_CHR_READ_UUID;

/* -- Connection / discovery state ----------------------------------- */

static uint16_t cli_conn_handle;
static bool     cli_connected;
static bool     connecting;

static uint16_t write_val_handle;
static uint16_t read_val_handle;
static uint16_t read_cccd_handle;

static uint16_t svc_start_handle;
static uint16_t svc_end_handle;

/* Track candidate address from scan response name match */
static bool     have_candidate;
static ble_addr_t candidate_addr;

/* Ask-before-connect: the knob prompts on the LCD before connecting to a
 * discovered wand. Once the user approves (once per power-on) it auto-reconnects
 * to that same wand for the rest of the session. */
static bool       connect_approved;   /* user approved a wand this power-on */
static ble_addr_t approved_addr;      /* the approved wand's address */
static bool       prompt_pending;     /* a connect prompt is currently shown */
static bool       prompt_suppressed;  /* user declined; stay standalone this boot */
static ble_addr_t pending_addr;       /* candidate awaiting user confirmation */
static char       pending_name[32];   /* its advertised name (for the prompt) */

static void start_scan(void);
static int  client_gap_event(struct ble_gap_event *event, void *arg);

/* -- Helper: begin a connection to a discovered wand ---------------- */

static void do_connect(const ble_addr_t *addr)
{
    connecting     = true;
    have_candidate = false;
    ble_gap_disc_cancel();
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, addr, 30000, NULL,
                             client_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect call failed: %d", rc);
        connecting = false;
        start_scan();
    }
}

/* -- Helper: reset discovery state ---------------------------------- */

static void reset_handles(void)
{
    write_val_handle = 0;
    read_val_handle  = 0;
    read_cccd_handle = 0;
    svc_start_handle = 0;
    svc_end_handle   = 0;
}

/* -- Descriptor discovery callback ---------------------------------- */

static int dsc_disc_cb(uint16_t conn, const struct ble_gatt_error *err,
                       uint16_t chr_handle, const struct ble_gatt_dsc *dsc,
                       void *arg)
{
    if (err->status == 0) {
        if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            read_cccd_handle = dsc->handle;
            ESP_LOGI(TAG, "CCCD for 0x1227 at handle %d", read_cccd_handle);
        }
    } else if (err->status == BLE_HS_EDONE) {
        if (read_cccd_handle != 0) {
            uint8_t val[2] = { 0x01, 0x00 };
            ble_gattc_write_flat(conn, read_cccd_handle,
                                val, sizeof(val), NULL, NULL);
            ESP_LOGI(TAG, "Subscribed to 0x1227 notifications");
        } else {
            ESP_LOGW(TAG, "CCCD not found for 0x1227");
        }
    }
    return 0;
}

/* -- Characteristic discovery callback ------------------------------ */

static int chr_disc_cb(uint16_t conn, const struct ble_gatt_error *err,
                       const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &chr_write_uuid.u) == 0) {
            write_val_handle = chr->val_handle;
            ESP_LOGI(TAG, "Write char 0x1225 handle=%d", write_val_handle);
        } else if (ble_uuid_cmp(&chr->uuid.u, &chr_read_uuid.u) == 0) {
            read_val_handle = chr->val_handle;
            ESP_LOGI(TAG, "Read char 0x1227 handle=%d", read_val_handle);
        }
    } else if (err->status == BLE_HS_EDONE) {
        if (read_val_handle != 0) {
            ble_gattc_disc_all_dscs(conn, read_val_handle,
                                    svc_end_handle, dsc_disc_cb, NULL);
        } else {
            ESP_LOGW(TAG, "0x1227 characteristic not found on EMW");
        }
    }
    return 0;
}

/* -- Service discovery callback ------------------------------------- */

static int svc_disc_cb(uint16_t conn, const struct ble_gatt_error *err,
                       const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0) {
        svc_start_handle = svc->start_handle;
        svc_end_handle   = svc->end_handle;
        ESP_LOGI(TAG, "Vibe service handles: %d-%d",
                 svc_start_handle, svc_end_handle);
    } else if (err->status == BLE_HS_EDONE) {
        if (svc_start_handle != 0) {
            ble_gattc_disc_all_chrs(conn, svc_start_handle,
                                    svc_end_handle, chr_disc_cb, NULL);
        } else {
            ESP_LOGW(TAG, "Vibe service 0x1221 not found on EMW");
        }
    }
    return 0;
}

/* -- Scan: extract name from adv/scan-response data ----------------- */

static bool extract_name(const uint8_t *data, uint8_t len,
                         char *out, int out_sz)
{
    /* Try NimBLE parser first */
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, len) == 0 &&
        fields.name != NULL && fields.name_len > 0) {
        int n = fields.name_len < (out_sz - 1) ? fields.name_len : (out_sz - 1);
        memcpy(out, fields.name, n);
        out[n] = '\0';
        return true;
    }

    /* Fallback: manually walk AD structures for type 0x08/0x09 (name) */
    int pos = 0;
    while (pos < len) {
        uint8_t ad_len = data[pos];
        if (ad_len == 0 || pos + ad_len >= len)
            break;
        uint8_t ad_type = data[pos + 1];
        if (ad_type == 0x08 || ad_type == 0x09) {   /* Short/Complete Name */
            int name_len = ad_len - 1;
            int n = name_len < (out_sz - 1) ? name_len : (out_sz - 1);
            memcpy(out, &data[pos + 2], n);
            out[n] = '\0';
            return true;
        }
        pos += ad_len + 1;
    }
    return false;
}

/* True if the advertised name looks like an EMW device this knob should bridge
 * to. Coarse candidate classifier only:
 *   - must start with "EMW"  (the EMW device naming prefix), AND
 *   - must NOT be our own advertised name (VIBE_DEVICE_NAME).
 * Since the knob now advertises "Strong Vibes Knob" (no "EMW" prefix), the
 * self-name guard below can never fire against our own current name — the
 * prefix gate already rejects it. We keep an explicit full-name guard anyway so
 * a future name that DOES start with "EMW" stays safe. The real self-connect
 * protection is the MAC check is_own_address() at the scan callback; scanning
 * also matches on the Vibe service UUID independently of name. */
static bool name_is_emw_app(const char *name)
{
    if (strncmp(name, "EMW", 3) != 0)
        return false;
    /* Exclude our own device by its full advertised name. Full compare (not a
     * truncated length) so it can never silently mis-match on a rename. */
    if (strcmp(name, VIBE_DEVICE_NAME) == 0)
        return false;
    return true;
}

/* Check adv data for Vibe UUID match (0x1221 or legacy 0x1315) */
static bool adv_has_vibe_uuid(const uint8_t *data, uint8_t len)
{
    /* Try NimBLE parser */
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, len) == 0) {
        for (int i = 0; i < fields.num_uuids128; i++) {
            if (ble_uuid_cmp(&fields.uuids128[i].u, &svc_uuid.u) == 0)
                return true;
        }
        for (int i = 0; i < fields.num_uuids16; i++) {
            if (fields.uuids16[i].value == 0x1221 ||
                fields.uuids16[i].value == 0x1315)
                return true;
        }
    }

    /* Fallback: manually walk AD structures for type 0x02/0x03 (16-bit UUIDs) */
    int pos = 0;
    while (pos < len) {
        uint8_t ad_len = data[pos];
        if (ad_len == 0 || pos + ad_len >= len)
            break;
        uint8_t ad_type = data[pos + 1];
        if ((ad_type == 0x02 || ad_type == 0x03) && ad_len >= 3) {
            for (int i = 2; i + 1 <= ad_len; i += 2) {
                uint16_t uuid16 = data[pos + i] | (data[pos + i + 1] << 8);
                if (uuid16 == 0x1221 || uuid16 == 0x1315)
                    return true;
            }
        }
        pos += ad_len + 1;
    }
    return false;
}

static bool is_own_address(const ble_addr_t *addr)
{
    uint8_t own[6];
    ble_hs_id_copy_addr(BLE_OWN_ADDR_PUBLIC, own, NULL);
    return memcmp(addr->val, own, 6) == 0;
}

/* -- GAP event handler (client connections) ------------------------- */

static int client_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_DISC: {
        if (connecting || cli_connected)
            break;

        if (is_own_address(&event->disc.addr))
            break;

        /* Try to extract name and check UUID */
        char name[32] = {0};
        bool got_name = extract_name(event->disc.data,
                                     event->disc.length_data, name, sizeof(name));
        bool got_uuid = adv_has_vibe_uuid(event->disc.data,
                                          event->disc.length_data);

        /* Remember EMW candidate from scan response (name may arrive
           in a different packet than UUID) */
        if (got_name && name_is_emw_app(name)) {
            if (!have_candidate) {
                ESP_LOGI(TAG, "Found EMW: \"%s\" addr=%02x:%02x:%02x:%02x:%02x:%02x uuid=%s",
                         name,
                         event->disc.addr.val[5], event->disc.addr.val[4],
                         event->disc.addr.val[3], event->disc.addr.val[2],
                         event->disc.addr.val[1], event->disc.addr.val[0],
                         got_uuid ? "yes" : "no");
            }
            have_candidate = true;
            memcpy(&candidate_addr, &event->disc.addr, sizeof(ble_addr_t));
        }

        /* Connect if: UUID match, name match, or previously-seen candidate */
        bool match = got_uuid;
        if (!match && got_name && name_is_emw_app(name))
            match = true;
        if (!match && have_candidate &&
            memcmp(candidate_addr.val, event->disc.addr.val, 6) == 0)
            match = true;

        if (!match)
            break;

        /* Ask-before-connect. Auto-reconnect only to a wand the user already
         * approved this power-on; otherwise pause the scan and prompt on the
         * LCD, then wait for ble_client_confirm_connect() / _dismiss(). */
        if (connect_approved &&
            memcmp(approved_addr.val, event->disc.addr.val, 6) == 0) {
            ESP_LOGI(TAG, "Auto-reconnecting to approved EMW...");
            do_connect(&event->disc.addr);
        } else if (!prompt_pending && !prompt_suppressed) {
            prompt_pending = true;
            memcpy(&pending_addr, &event->disc.addr, sizeof(ble_addr_t));
            snprintf(pending_name, sizeof(pending_name), "%s",
                     got_name ? name : "EMW device");
            ble_gap_disc_cancel();   /* pause scanning while the user decides */
            ESP_LOGI(TAG, "EMW found — prompting user to connect");
            display_show_connect_prompt(pending_name);
        }
        break;
    }

    case BLE_GAP_EVENT_CONNECT:
        connecting = false;
        if (event->connect.status == 0) {
            cli_conn_handle = event->connect.conn_handle;
            cli_connected   = true;
            ESP_LOGI(TAG, "Connected to EMW (h=%d)", cli_conn_handle);
            display_set_ble_status(ble_server_is_connected(), true);
            /* Request MTU exchange so datagrams > 20 bytes can be forwarded */
            ble_gattc_exchange_mtu(cli_conn_handle, NULL, NULL);
            ble_gattc_disc_svc_by_uuid(cli_conn_handle, &svc_uuid.u,
                                       svc_disc_cb, NULL);
        } else {
            ESP_LOGW(TAG, "EMW connect failed: %d", event->connect.status);
            start_scan();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        cli_connected = false;
        connecting    = false;
        reset_handles();
        /* Drop the cached wand motor state so auto-flat waits for a fresh
         * Motor.State after reconnect instead of trusting a stale/idle value. */
        bridge_reset_emw_state();
        ESP_LOGI(TAG, "EMW disconnected (reason=%d)",
                 event->disconnect.reason);
        display_set_ble_status(ble_server_is_connected(), false);
        start_scan();
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == read_val_handle) {
            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (len > 0 && len <= VIBE_MAX_DATAGRAM) {
                bridge_evt_t evt = {
                    .type = BRIDGE_EVT_EMW_NOTIFY,
                    .len  = len,
                };
                os_mbuf_copydata(event->notify_rx.om, 0, len, evt.data);
                bridge_post(&evt);
            }
        }
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

/* -- Scanning ------------------------------------------------------- */

static void start_scan(void)
{
    have_candidate = false;

    struct ble_gap_disc_params params = { 0 };
    params.filter_duplicates = 0;   /* Need scan responses for name */
    params.passive           = 0;   /* Active scan to get scan responses */

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &params, client_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Scanning for EMW device...");
    } else if (rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
    }
}

/* -- Public API ----------------------------------------------------- */

void ble_client_init(void)
{
    ESP_LOGI(TAG, "BLE client initialized");
}

void ble_client_start(void)
{
    start_scan();
}

void ble_client_forward(const uint8_t *data, uint16_t len)
{
    if (!cli_connected || write_val_handle == 0)
        return;

    /* This is write-WITHOUT-response (no ACK), so a datagram lost here is lost
     * silently — and a dropped level-critical frame (e.g. a MIC=0 stop) leaves
     * the wand running while the knob's screen already shows 0. The most common
     * transient failure during a burst (a fast turn) is BLE_HS_ENOMEM: the
     * NimBLE mbuf pool is momentarily exhausted. Retry a few times with a short
     * yield so the pool can drain, instead of dropping on the first shortage.
     * Note: on-air/link-layer loss is still possible (unacknowledged); the
     * encoder's reconcile timer is the higher-level backstop for that. */
    for (int attempt = 0; attempt < 4; attempt++) {
        int rc = ble_gattc_write_no_rsp_flat(cli_conn_handle,
                                             write_val_handle, data, len);
        if (rc == 0)
            return;
        if (rc != BLE_HS_ENOMEM) {
            ESP_LOGW(TAG, "Forward write failed: %d", rc);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    ESP_LOGW(TAG, "Forward write dropped after retries (ENOMEM)");
}

bool ble_client_is_connected(void)
{
    return cli_connected && write_val_handle != 0;
}

bool ble_client_is_linked(void)
{
    /* Physical link only — true as soon as the GAP connection is up, before
     * GATT discovery finds the write handle. Use this for the on-screen "EMW"
     * status (the wand IS connected); use ble_client_is_connected() when you
     * need to actually forward a datagram (which needs write_val_handle). */
    return cli_connected;
}

/* -- Ask-before-connect: UI confirmation hooks ---------------------- */

void ble_client_confirm_connect(void)
{
    if (!prompt_pending)
        return;
    prompt_pending   = false;
    connect_approved = true;                 /* auto-reconnect this power-on */
    memcpy(&approved_addr, &pending_addr, sizeof(ble_addr_t));
    ESP_LOGI(TAG, "User approved EMW connect");
    do_connect(&pending_addr);
}

void ble_client_dismiss_connect(void)
{
    if (!prompt_pending)
        return;
    prompt_pending    = false;
    prompt_suppressed = true;    /* stay standalone until the knob reboots */
    ESP_LOGI(TAG, "User declined EMW connect — staying standalone");
    /* Scan stays cancelled; the user opted out for this power-on. */
}
