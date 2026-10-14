// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * Bridge task — inspect-and-forward datagram routing.
 */

#include "bridge.h"
#include "ble_server.h"
#include "ble_client.h"
#include "display.h"
#include "encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "bridge";

#define BRIDGE_QUEUE_LEN   16
#define BRIDGE_STACK_SIZE  4096
#define BRIDGE_TASK_PRIO   5

/* Level for the wand power-button response when there is no resume level (fresh
 * connect, or after a Webapp session): deliberately gentle. */
#define KNOB_DEFAULT_MIC   20
/* MIC 1 is silent on the wand, so a power-button restart never goes below 2. */
#define KNOB_MIN_RUN_MIC   2

static QueueHandle_t evt_queue;

/* ── Datagram inspection + display sync ─────────────────────────── */

/* Cached EMW motor state for encoder auto-start logic */
static uint8_t emw_motor_state = 0;   /* 0=idle, 1=repeat, 2=non-repeat */
static uint8_t emw_pattern_id  = 0;   /* last reported patternId (0xFF=wand-local) */
static bool    emw_state_known = false; /* true once a real Motor.State seen */

/* Ground truth: the last MIC the WAND actually echoed (0-100). Updated ONLY from
 * inbound EMW datagrams, never from optimistic local marks, so the reconcile
 * logic can trust it to tell whether the wand really received the knob's command
 * (a dropped MIC=0 leaves this non-zero while the knob shows 0). */
static uint8_t emw_mic       = 0;
static bool    emw_mic_known = false;

/* The level a wand power-button restart resumes at: the last NON-ZERO level the
 * wand echoed for the knob's own flat in standalone use (0 = none: use the
 * default). Unlike emw_mic it survives the idle report the wand sends when its
 * power button turns it off — that report carries MIC 0 — so the restart returns
 * to the level the user was at. Cleared on wand disconnect and at the end of any
 * Webapp session, so it only ever holds a level from the current standalone run. */
static uint8_t emw_resume_mic = 0;

/* True only while the pattern on the wand is known to be one the knob wrote
 * itself (its auto-flat or the power-button reply). A Webapp pattern write or a
 * disconnect clears it — conservatively, even if the knob's flat may still be
 * running. Only then is an echoed MIC the plain intensity of a 100% flat; the
 * pattern id alone can't tell, since a Webapp may use the same id. */
static bool knob_owns_pattern = false;

uint8_t bridge_get_emw_motor_state(void)
{
    return emw_motor_state;
}

bool bridge_emw_state_known(void)
{
    return emw_state_known;
}

uint8_t bridge_get_emw_mic(void)
{
    return emw_mic;
}

bool bridge_emw_mic_known(void)
{
    return emw_mic_known;
}

void bridge_note_emw_running(void)
{
    /* The knob just authored a repeating (flat) pattern itself, stamped with
     * KNOB_PATTERN_ID (not 0xFF). Record it as running with our pattern id so
     * the wand's echo of it is recognized as ours and never mistaken for the
     * wand's own 0xFF power-button pattern. */
    emw_motor_state   = 1;   /* repeat */
    emw_pattern_id    = KNOB_PATTERN_ID;
    emw_state_known   = true;
    knob_owns_pattern = true;
}

void bridge_note_emw_idle(void)
{
    /* The knob just sent Control.Reset (turned to 0). Mark the wand KNOWN idle
     * (unlike bridge_reset_emw_state, keep state_known=true) so the next detent
     * can immediately re-arm the auto-flat without waiting for the wand's idle
     * Motor.State to come back through the bridge. */
    emw_motor_state = 0;
    emw_pattern_id  = 0;
    emw_state_known = true;
}

void bridge_reset_emw_state(void)
{
    emw_motor_state   = 0;
    emw_pattern_id    = 0;
    emw_state_known   = false;
    emw_mic           = 0;
    emw_mic_known     = false;
    emw_resume_mic    = 0;
    knob_owns_pattern = false;
}

void bridge_note_webapp_disconnected(void)
{
    /* Whatever level the knob learned before the session is stale now: the
     * Webapp may have changed the level, run its own patterns, or the knob may
     * have been turned meanwhile (none of which is learned while it is
     * connected). Fall back to the gentle default, and learn nothing more until
     * the knob writes its own flat again — dropping ownership also stops a
     * session echo still queued in the bridge from re-seeding a level. */
    emw_resume_mic    = 0;
    knob_owns_pattern = false;
}

/* Re-sync the knob to an inbound (wand or Webapp) MIC level, but only when safe:
 *   - 0xFF is the protocol's "leave unchanged" sentinel and anything above 100 is
 *     out of the 0-100 range — never apply either (the label would flash 255).
 *   - while the user is turning the knob the rotation rules: it already paints
 *     (and owns) the local value, so applying a laggy echo of an older level
 *     would fight it and jitter; defer to the knob for a window after each turn.
 * Outside that window we update BOTH the screen and the encoder's internal
 * counter so the knob's next step follows from the real intensity — otherwise
 * the counter drifts and the screen can read 0 while the wand still runs. */
static void sync_mic_display(uint8_t mic)
{
    if (mic == 0xFF || mic > 100) return;
    if (encoder_recently_commanded()) return;
    encoder_set_mic_value(mic);
    display_set_mic(mic);
}

/* Record the wand's real MIC (ground truth), independent of the display-sync
 * ownership window, so reconcile always sees what the wand actually reports.
 * Only from inbound EMW echoes; ignores the 0xFF "unchanged" / out-of-range. */
static void note_emw_mic(uint8_t mic)
{
    if (mic > 100) return;
    emw_mic       = mic;
    emw_mic_known = true;
}

/* Record a level for the power-button restart to resume at. Only the knob's own
 * flat in standalone use counts: its MIC is the plain intensity the user chose,
 * whereas a Webapp pattern's MIC is a gain on its own waveform (100 there is not
 * the knob's full-power flat). Nothing is learned while a Webapp is connected,
 * and the session's end clears the level (bridge_note_webapp_disconnected).
 * Skips 0 (a stop) and the 0xFF "unchanged" / out-of-range values. */
static void note_emw_resume_mic(uint8_t mic)
{
    if (ble_server_is_connected() || !knob_owns_pattern) return;
    if (mic == 0 || mic > 100) return;
    emw_resume_mic = mic;
}

static void inspect_and_sync_display(const uint8_t *data, uint16_t len, bool from_emw)
{
    if (len == 0 || data[0] != VIBE_DATAGRAM_VER || len < 4)
        return;

    uint8_t mod = data[2];
    uint8_t msg = data[3];

    /* Motor.State: [VER, SIZE, 0x01, 0x02, state, patternId, mic] */
    if (mod == VIBE_MOD_MOTOR && msg == VIBE_MSG_STATE && len >= 7) {
        uint8_t old_state   = emw_motor_state;
        uint8_t old_pattern = emw_pattern_id;
        uint8_t new_state   = data[4];
        uint8_t new_pattern = data[5];

        emw_motor_state = new_state;
        emw_pattern_id  = new_pattern;
        emw_state_known = true;

        /* Respond to the wand's own power button. The wand reports its LOCAL
         * (power-button) pattern as patternId 0xFF ("server-dictated"), but
         * while latched to a Vibe client it does not drive the motor itself — it
         * waits for the client to send a pattern. The knob's own patterns use
         * KNOB_PATTERN_ID (not 0xFF), so a report of 0xFF-running unambiguously
         * means the wand's button. On the transition INTO wand-0xFF-running,
         * reply with a flat so the motor runs.
         *
         * MIC: never 0xFF. The wand does not implement the protocol's "leave
         * unchanged" meaning — it applies a pattern-header MIC as a raw gain, so
         * 0xFF would be x2.55 = full power. Resume the level the knob's own flat
         * last ran at (emw_resume_mic, not emw_mic: the idle report for a
         * power-button off carries MIC 0), else a gentle default.
         *
         * Edge-guarded (fire only when NOT already in wand-0xFF-running) so a
         * wand that keeps re-reporting 0xFF without adopting our pattern cannot
         * cause a runaway. Gated to standalone; a connected Webapp owns the
         * pattern and sequences its own chunks on state transitions. */
        bool wand_now    = (new_state != 0 && new_pattern == 0xFF);
        bool wand_before = (old_state != 0 && old_pattern == 0xFF);
        uint8_t shown_mic = data[6];
        if (!ble_server_is_connected() && wand_now) {
            /* In standalone the wand never drives its 0xFF pattern itself, so a
             * 0xFF report's MIC is never the running level: don't show it (0xFF
             * is ignored by sync_mic_display). The reply below sets the level. */
            shown_mic = 0xFF;
        }
        if (!ble_server_is_connected() && wand_now && !wand_before) {
            uint8_t mic = emw_resume_mic ? emw_resume_mic : KNOB_DEFAULT_MIC;
            if (mic > 100)              mic = 100;
            if (mic < KNOB_MIN_RUN_MIC) mic = KNOB_MIN_RUN_MIC;

            const uint8_t flat[] = {
                VIBE_DATAGRAM_VER,   /* 0x10 */
                0x09,                /* SIZE: 9 payload bytes */
                VIBE_MOD_MOTOR,      /* 0x01 */
                VIBE_MSG_REPEATING,  /* 0x00 */
                0x00,                /* Timbre: linear */
                KNOB_PATTERN_ID,     /* PatternID: ours, distinct from wand 0xFF */
                mic,                 /* MIC: the resume level chosen above */
                0x64, 0x64,          /* Begin/End: 100% */
                0x03, 0xE8,          /* Period: 1000ms */
            };
            ble_client_forward(flat, sizeof(flat));
            knob_owns_pattern = true;
            ESP_LOGI(TAG, "Wand power-on -> sent flat id=0x%02x MIC=%d",
                     KNOB_PATTERN_ID, mic);

            /* The motor now runs at the level the knob chose, not the one in the
             * wand's 0xFF report, so show (and step the encoder from) that. */
            shown_mic = mic;
        }

        if (from_emw) {
            note_emw_mic(data[6]);
            /* The knob's flat repeats (state 1) under its own id; never take a
             * level from the wand's 0xFF report or an idle/error report. */
            if (new_state == 1 && new_pattern == KNOB_PATTERN_ID)
                note_emw_resume_mic(data[6]);
        }
        sync_mic_display(shown_mic);
        display_set_motor_state(new_state, new_pattern);
    }
    /* Motor.MIC: [VER, SIZE, 0x01, 0x04, mic] */
    else if (mod == VIBE_MOD_MOTOR && msg == VIBE_MSG_MIC && len >= 5) {
        if (from_emw) {
            note_emw_mic(data[4]);
            /* A level while a pattern runs; once the knob has marked the wand
             * idle (a stop), a late echo may only lower it. */
            if (emw_motor_state != 0 || data[4] < emw_resume_mic)
                note_emw_resume_mic(data[4]);
        }
        sync_mic_display(data[4]);
    }
    /* A Webapp-written pattern replaces whatever the knob authored; keep "a resume
     * level exists only while the knob owns the pattern". */
    else if (!from_emw && mod == VIBE_MOD_MOTOR &&
             (msg == VIBE_MSG_REPEATING || msg == VIBE_MSG_NONREPEATING)) {
        knob_owns_pattern = false;
        emw_resume_mic    = 0;
    }
}

/* ── Bridge task ────────────────────────────────────────────────── */

static void bridge_task(void *param)
{
    bridge_evt_t evt;

    ESP_LOGI(TAG, "Bridge task running");

    for (;;) {
        if (xQueueReceive(evt_queue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt.type) {
        case BRIDGE_EVT_WEBAPP_WRITE: {
            /* Log first bytes for debugging */
            char hex[37] = {0};
            int n = evt.len < 12 ? evt.len : 12;
            for (int i = 0; i < n; i++)
                snprintf(hex + i * 3, 4, "%02x ", evt.data[i]);
            ESP_LOGD(TAG, "webapp->emw [%d] %s", evt.len, hex);
            inspect_and_sync_display(evt.data, evt.len, false);
            ble_client_forward(evt.data, evt.len);
            display_notify_ble_activity();
            break;
        }

        case BRIDGE_EVT_EMW_NOTIFY: {
            /* Full-frame dump; flag the wand's private-protocol frames (first
             * byte != 0x10 Vibe and != 0xFF Vibe-error) so non-Vibe traffic is
             * easy to spot. */
            char hex[3 * 32 + 1] = {0};
            int n = evt.len < 32 ? evt.len : 32;
            for (int i = 0; i < n; i++)
                snprintf(hex + i * 3, 4, "%02x ", evt.data[i]);
            const char *kind =
                (evt.len && evt.data[0] != 0x10 && evt.data[0] != 0xFF)
                    ? "wand-private" : "vibe";
            ESP_LOGD(TAG, "emw->knob [%d] %s: %s", evt.len, kind, hex);
            inspect_and_sync_display(evt.data, evt.len, true);
            ble_server_notify(evt.data, evt.len);
            display_notify_ble_activity();
            break;
        }
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void bridge_init(void)
{
    evt_queue = xQueueCreate(BRIDGE_QUEUE_LEN, sizeof(bridge_evt_t));
    assert(evt_queue);

    BaseType_t ok = xTaskCreate(bridge_task, "bridge",
                                BRIDGE_STACK_SIZE, NULL,
                                BRIDGE_TASK_PRIO, NULL);
    assert(ok == pdPASS);

    ESP_LOGI(TAG, "Bridge initialized (queue=%d)", BRIDGE_QUEUE_LEN);
}

void bridge_post(const bridge_evt_t *evt)
{
    if (xQueueSend(evt_queue, evt, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full, event dropped (type=%d)", evt->type);
    }
}
