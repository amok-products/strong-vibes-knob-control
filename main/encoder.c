// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * Rotary encoder — uses Waveshare bidi_switch_knob driver.
 *
 * In standalone use (no Webapp attached) the encoder auto-starts a flat
 * continuous repeating pattern when the wand is known-idle and MIC rises
 * above 0, so the vibration responds immediately to a knob turn. When a
 * Webapp is connected it owns pattern authorship, so the knob only sends
 * MIC (intensity) updates and never auto-starts its own pattern.
 */

#include "encoder.h"
#include "ble_client.h"
#include "ble_server.h"
#include "bridge.h"
#include "display.h"
#include "vibe_protocol.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "bidi_switch_knob.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "encoder";

#define ENC_A_GPIO      8
#define ENC_B_GPIO      7
#define MIC_STEP        2       /* intensity change per detent */
#define MIC_MAX         100     /* protocol range 0-100 */

/* How long after a local knob turn the knob keeps ownership of the level. For
 * this window the rotation "rules": neither a laggy wand echo nor a Webapp
 * update repaints or moves the value (which would fight the turn and jitter).
 * Once it elapses the knob re-syncs to whatever the wand/Webapp report, so the
 * knob's own counter can't drift out of sync with the real intensity. Kept short
 * so the re-sync (and the reconcile backstop) recover quickly after a turn; on
 * hardware 200 ms is snappy without the echo re-introducing jitter. */
#define ECHO_HOLD_US    (200 * 1000)  /* 200 ms */

/* KNOB_PATTERN_ID (the id the knob stamps on its own patterns) now lives in
 * vibe_protocol.h — deliberately distinct from the wand's server-dictated 0xFF. */

/* How often the reconcile timer checks whether a lost "stop" needs re-asserting.
 * Fires at most once per ECHO_HOLD window (each re-send opens a new window), so
 * a dropped MIC=0 is recovered within ~1-2 s without spamming the wand. */
#define RECONCILE_PERIOD_US  (500 * 1000)  /* 500 ms */

static uint8_t mic_value = 50;
static knob_handle_t knob;
static int64_t last_local_cmd_us = 0;   /* esp_timer time of last knob turn; 0 = never */
static esp_timer_handle_t reconcile_timer;

/* mic_value is a read-modify-write shared between the knob callbacks (esp_timer
 * task) and encoder_set_mic_value() (bridge task) on a dual-core part, so guard
 * the RMW with a spinlock. Keep the critical sections tiny — never call a BLE or
 * display function while holding it. */
static portMUX_TYPE mic_mux = portMUX_INITIALIZER_UNLOCKED;

/* -- Send flat continuous repeating pattern to EMW ------------------- */

static void send_flat_pattern(uint8_t mic)
{
    /*
     * RepeatingPattern: linear timbre, 1 segment at 100% constant, 1000ms period
     * [VER=0x10][SIZE=0x09][Motor=0x01][Repeating=0x00]
     * [Timbre=0x00 linear][PatternID=0xFF][MIC=mic]
     * [Begin=0x64(100%)][End=0x64(100%)][Period=0x03E8(1000ms)]
     */
    uint8_t pattern[] = {
        VIBE_DATAGRAM_VER,      /* 0x10 */
        0x09,                   /* SIZE: 9 payload bytes */
        VIBE_MOD_MOTOR,         /* 0x01 */
        VIBE_MSG_REPEATING,     /* 0x00 */
        0x00,                   /* Timbre: linear */
        KNOB_PATTERN_ID,        /* PatternID: knob-initiated */
        mic,                    /* MIC */
        0x64,                   /* Begin: 100% */
        0x64,                   /* End: 100% */
        0x03, 0xE8,             /* Period: 1000ms */
    };

    ble_client_forward(pattern, sizeof(pattern));

    /* Record that the wand is now running our flat. The flat we just sent is a
     * Motor.Repeating, and neither it nor its wand echo updates the cached
     * state (the bridge only caches Motor.State), so without this every further
     * detent would see "idle" again and re-fire another flat. */
    bridge_note_emw_running();

    /* Also notify Webapp so it knows what we did */
    if (ble_server_is_connected()) {
        ble_server_notify(pattern, sizeof(pattern));
    }

    ESP_LOGI(TAG, "Auto-started flat pattern, MIC=%d", mic);
}

/* -- MIC datagram + auto-start logic --------------------------------- */

static void send_mic(uint8_t value)
{
    /* The knob now owns the on-screen level: show it immediately and hold off
     * wand-echo repaints for a moment (see encoder_recently_commanded). This is
     * done up-front so it applies even in the flat-pattern branch below, which
     * returns early. */
    display_set_mic(value);
    last_local_cmd_us = esp_timer_get_time();

    if (ble_client_is_connected()) {
        /* Auto-start a flat pattern only in standalone use: no Webapp attached
         * (a connected Webapp owns pattern authorship — we must not preempt its
         * designer session), and only once the wand has actually reported an
         * idle state. bridge_emw_state_known() guards the power-on / reconnect
         * default where the cache reads 0 before any real state has arrived. */
        if (value > 0 && !ble_server_is_connected()
                      && bridge_emw_state_known()
                      && bridge_get_emw_motor_state() == 0) {
            send_flat_pattern(value);
            return;  /* Pattern includes MIC, no separate MIC datagram needed */
        }

        /* EMW is running a pattern — just send MIC update */
        uint8_t mic_dgram[] = {
            VIBE_DATAGRAM_VER,      /* 0x10 */
            0x03,                   /* SIZE: 3 payload bytes */
            VIBE_MOD_MOTOR,         /* 0x01 */
            VIBE_MSG_MIC,           /* 0x04 */
            value,                  /* intensity 0-100 */
        };
        ble_client_forward(mic_dgram, sizeof(mic_dgram));

        /* Double-check the wand is actually off at zero. MIC=0 scales the
         * running flat pattern's output to zero, but the pattern itself keeps
         * running and the wand can idle at a small residual (it "still runs on
         * 2"). In standalone use — where the knob authored the pattern — follow
         * the MIC=0 with Control.Reset, which stops any running pattern and
         * returns the wand to idle. A connected Webapp owns pattern authorship,
         * so never reset its session; there we only pass the MIC through. */
        if (value == 0 && !ble_server_is_connected()) {
            static const uint8_t reset_dgram[] = {
                VIBE_DATAGRAM_VER,  /* 0x10 */
                0x02,               /* SIZE: 2 payload bytes */
                VIBE_MOD_CONTROL,   /* 0x00 */
                VIBE_MSG_RESET,     /* 0x00 */
            };
            ble_client_forward(reset_dgram, sizeof(reset_dgram));
            bridge_note_emw_idle();  /* known-idle now → next turn-up re-arms flat */
            display_set_motor_state(0, 0);
            ESP_LOGI(TAG, "MIC=0 -> Control.Reset (ensure wand off)");
        }
    } else {
        /* No EMW connection — send synthetic Motor.State to Webapp */
        uint8_t state_dgram[] = {
            VIBE_DATAGRAM_VER,  /* 0x10 */
            0x05,               /* SIZE: 5 payload bytes */
            VIBE_MOD_MOTOR,     /* 0x01 */
            VIBE_MSG_STATE,     /* 0x02 */
            0x00,               /* State: idle */
            0x00,               /* PatternID: none */
            value,              /* MIC */
        };
        ble_server_notify(state_dgram, sizeof(state_dgram));
    }
}

/* -- Reconcile: heal a "stop" the wand never received ---------------- */

static void reconcile_cb(void *arg)
{
    /* The knob->wand link is write-without-response (unacknowledged), so a MIC=0
     * that drops leaves the wand running while the screen already shows 0; the
     * wand only echoes on a real state change, so nothing self-corrects. When the
     * knob is at 0 (the user wants it OFF) but the wand's ECHO-CONFIRMED level is
     * still non-zero, re-assert the stop. This is the only mechanism that heals a
     * lost command regardless of loss cause. Self-terminating: once the wand
     * echoes MIC=0 the confirmed level drops to 0 and this stops firing.
     *
     * Only enforces the STOP (mic_value==0): for non-zero levels the v1.3.3 echo
     * sync already reconciles, and re-sending a level would risk fighting a
     * Webapp that owns the pattern. Suppressed during the post-turn window so it
     * never fights an active turn. */
    if (!ble_client_is_connected())    return;
    if (encoder_recently_commanded())  return;
    if (mic_value != 0)                return;   /* only enforce the stop */
    if (!bridge_emw_mic_known())       return;   /* no wand ground truth yet */
    if (bridge_get_emw_mic() == 0)     return;   /* wand already confirmed off */

    ESP_LOGW(TAG, "Reconcile: knob=0 but wand MIC=%d -> re-assert stop",
             bridge_get_emw_mic());
    send_mic(0);
}

/* -- Knob event callbacks -------------------------------------------- */

static void on_knob_right(void *arg, void *data)
{
    portENTER_CRITICAL(&mic_mux);
    uint8_t cur = mic_value;
    int val = (int)cur + MIC_STEP;
    if (val > MIC_MAX) val = MIC_MAX;
    mic_value = (uint8_t)val;
    portEXIT_CRITICAL(&mic_mux);

    if ((uint8_t)val != cur)
        send_mic((uint8_t)val);
}

static void on_knob_left(void *arg, void *data)
{
    portENTER_CRITICAL(&mic_mux);
    uint8_t cur = mic_value;
    int val = (int)cur - MIC_STEP;
    if (val < 0) val = 0;
    mic_value = (uint8_t)val;
    portEXIT_CRITICAL(&mic_mux);

    /* Send when the level changed, and ALWAYS when it lands on 0 — reaching the
     * floor must (re-)issue the stop even from an already-0 over-rotation, since
     * a prior stop may have been dropped on the unacknowledged wand link. This is
     * the immediate response to the user's gesture; the reconcile timer is the
     * automatic backstop for a stop the user does not manually retry. */
    if ((uint8_t)val != cur || val == 0)
        send_mic((uint8_t)val);
}

/* -- Public API ------------------------------------------------------ */

void encoder_init(void)
{
    knob_config_t cfg = {
        .gpio_encoder_a = ENC_A_GPIO,
        .gpio_encoder_b = ENC_B_GPIO,
    };

    knob = iot_knob_create(&cfg);
    assert(knob);

    iot_knob_register_cb(knob, KNOB_RIGHT, on_knob_right, NULL);
    iot_knob_register_cb(knob, KNOB_LEFT,  on_knob_left,  NULL);

    /* Periodic backstop that re-asserts a dropped stop (see reconcile_cb). */
    const esp_timer_create_args_t targs = {
        .callback = reconcile_cb,
        .name     = "mic_reconcile",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &reconcile_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(reconcile_timer, RECONCILE_PERIOD_US));

    ESP_LOGI(TAG, "Encoder on GPIO%d/%d, range 0-%d, step %d",
             ENC_A_GPIO, ENC_B_GPIO, MIC_MAX, MIC_STEP);
}

uint8_t encoder_get_mic_value(void)
{
    return mic_value;
}

void encoder_set_mic_value(uint8_t value)
{
    /* Re-sync the knob's internal counter to an externally-reported level (a
     * wand echo or a Webapp update), so the next detent steps from the real
     * value instead of a stale local one. The bridge only calls this once the
     * post-turn ownership window has elapsed, so it never overrides an
     * in-progress turn. */
    if (value > MIC_MAX) return;   /* ignore 0xFF "unchanged" / out-of-range */
    portENTER_CRITICAL(&mic_mux);
    mic_value = value;
    portEXIT_CRITICAL(&mic_mux);
}

bool encoder_recently_commanded(void)
{
    if (last_local_cmd_us == 0) return false;
    return (esp_timer_get_time() - last_local_cmd_us) < ECHO_HOLD_US;
}
