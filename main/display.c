// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025-2026 AMOK / Europe Magic Wand
/**
 * ST77916 LCD Display + CST816 Touch + LVGL tileview UI.
 *
 * Hardware: ST77916 360x360 LCD via QSPI (SPI2_HOST), PWM backlight on GPIO47
 *           CST816 touch via I2C (GPIO11 SDA, GPIO12 SCL, addr 0x15)
 * 3 screens: Patterns (left) | Main (center) | Settings (right)
 */

#include "display.h"
#include "version.h"
#include "ble_client.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "lvgl.h"

static const char *TAG = "display";

/* Boot-splash brand logo (generated L8 image in sv_logo.c). */
LV_IMAGE_DECLARE(sv_logo);

/* -- Pin definitions ------------------------------------------------ */

#define LCD_SCLK        13
#define LCD_CS          14
#define LCD_D0          15
#define LCD_D1          16
#define LCD_D2          17
#define LCD_D3          18
#define LCD_RST         21
#define LCD_BL          47

#define TOUCH_SDA       11
#define TOUCH_SCL       12

#define LCD_H_RES       360
#define LCD_V_RES       360

/* -- Brand accent --------------------------------------------------- */

/* Single unified brand blue (#1EA5DE) used for every "active/positive"
 * accent on screen: MIC arc + value, BLE-activity indicator, connected
 * status labels, and the running motor state. Amber (warning) and red
 * (error) state colors are intentionally NOT this — they stay distinct.
 * A macro, not a const: lv_color_make() is not a constant expression, so
 * it cannot initialize a file-scope const and is needed inside a ternary. */
#define SV_ACCENT       lv_color_make(0x1E, 0xA5, 0xDE)

/* -- Waveshare ST77916 init commands (from 08_LVGL_Test example) ---- */

static const st77916_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0},
    {0xF2, (uint8_t[]){0x28}, 1, 0},
    {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0},
    {0x83, (uint8_t[]){0xE0}, 1, 0},
    {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0},
    {0xB0, (uint8_t[]){0x56}, 1, 0},
    {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0},
    {0xB4, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0},
    {0xB7, (uint8_t[]){0x40}, 1, 0},
    {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x08}, 1, 0},
    {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x80}, 1, 0},
    {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0},
    {0xC3, (uint8_t[]){0x80}, 1, 0},
    {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0},
    {0xC6, (uint8_t[]){0xA9}, 1, 0},
    {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0},
    {0xC9, (uint8_t[]){0xA9}, 1, 0},
    {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0},
    {0xD0, (uint8_t[]){0x91}, 1, 0},
    {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0},
    {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0},
    {0xF1, (uint8_t[]){0x10}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0},
    {0xF3, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0},
    {0xE2, (uint8_t[]){0x00}, 1, 0},
    {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0},
    {0xE5, (uint8_t[]){0x06}, 1, 0},
    {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0},
    {0xE8, (uint8_t[]){0x05}, 1, 0},
    {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0},
    {0xEB, (uint8_t[]){0x00}, 1, 0},
    {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0},
    {0xEE, (uint8_t[]){0x00}, 1, 0},
    {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0},
    {0xF9, (uint8_t[]){0x00}, 1, 0},
    {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0},
    {0xFC, (uint8_t[]){0x00}, 1, 0},
    {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0},
    {0x62, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0},
    {0x65, (uint8_t[]){0x00}, 1, 0},
    {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0},
    {0x68, (uint8_t[]){0x00}, 1, 0},
    {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0},
    {0x6B, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0},
    {0x72, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0},
    {0x75, (uint8_t[]){0x00}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0},
    {0x78, (uint8_t[]){0x00}, 1, 0},
    {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0},
    {0x7B, (uint8_t[]){0x00}, 1, 0},
    {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0},
    {0x82, (uint8_t[]){0x06}, 1, 0},
    {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0},
    {0x85, (uint8_t[]){0x04}, 1, 0},
    {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0},
    {0x88, (uint8_t[]){0x48}, 1, 0},
    {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0},
    {0x8B, (uint8_t[]){0x02}, 1, 0},
    {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0},
    {0x8E, (uint8_t[]){0x00}, 1, 0},
    {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0},
    {0x91, (uint8_t[]){0x00}, 1, 0},
    {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0},
    {0x94, (uint8_t[]){0xDA}, 1, 0},
    {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0},
    {0x97, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0},
    {0x9A, (uint8_t[]){0x0C}, 1, 0},
    {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0},
    {0x9D, (uint8_t[]){0x04}, 1, 0},
    {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0},
    {0xA0, (uint8_t[]){0x48}, 1, 0},
    {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0},
    {0xA3, (uint8_t[]){0x02}, 1, 0},
    {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0},
    {0xA6, (uint8_t[]){0x00}, 1, 0},
    {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0},
    {0xA9, (uint8_t[]){0x00}, 1, 0},
    {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0},
    {0xAC, (uint8_t[]){0xD7}, 1, 0},
    {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0},
    {0xAF, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x09}, 1, 0},
    {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0},
    {0xB5, (uint8_t[]){0x04}, 1, 0},
    {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0},
    {0xB8, (uint8_t[]){0x48}, 1, 0},
    {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0},
    {0xBB, (uint8_t[]){0x02}, 1, 0},
    {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0},
    {0xC1, (uint8_t[]){0x47}, 1, 0},
    {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0},
    {0xC4, (uint8_t[]){0x74}, 1, 0},
    {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0},
    {0xC7, (uint8_t[]){0x01}, 1, 0},
    {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0},
    {0xD0, (uint8_t[]){0x10}, 1, 0},
    {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0},
    {0xD3, (uint8_t[]){0x65}, 1, 0},
    {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0},
    {0xD6, (uint8_t[]){0x99}, 1, 0},
    {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0},
    {0xD9, (uint8_t[]){0xAA}, 1, 0},
    {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0x21, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
};

/* -- LVGL UI elements ----------------------------------------------- */

static lv_obj_t *arc_mic;
static lv_obj_t *lbl_mic;
static lv_obj_t *lbl_activity;
static lv_obj_t *lbl_motor_state;
static lv_obj_t *lbl_webapp;
static lv_obj_t *lbl_emw;

static esp_timer_handle_t activity_timer;
static knob_mode_t knob_mode = KNOB_MODE_POWER;

/* Set true only once LVGL and the UI widgets exist. The public display_*
 * setters are called from BLE GAP callbacks, which can fire before (or during)
 * display_init(); taking the LVGL lock before lvgl_port_init() asserts. Guard
 * every setter on this so an early call is a harmless no-op instead of a crash. */
static bool display_ready;

/* -- Backlight PWM -------------------------------------------------- */

static void backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_3,
        .freq_hz         = 50000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_conf = {
        .gpio_num   = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_1,
        .timer_sel  = LEDC_TIMER_3,
        .duty       = 200,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));
}

/* -- LVGL coordinate rounder (ST77916 needs even-aligned coords) ---- */

static void rounder_cb(lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

/* -- Activity timer callback ---------------------------------------- */

static void activity_timer_cb(void *arg)
{
    if (lvgl_port_lock(0)) {
        if (lbl_activity) {
            lv_obj_add_flag(lbl_activity, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
    }
}

/* -- Settings radio button callback --------------------------------- */

static void settings_radio_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (lv_obj_has_state(btn, LV_STATE_CHECKED)) {
        knob_mode = (knob_mode_t)idx;
        ESP_LOGI(TAG, "Knob mode: %d", (int)knob_mode);
    }
}

/* -- Create Patterns tile (col=0) ----------------------------------- */

static void create_patterns_tile(lv_obj_t *tile)
{
    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "Patterns");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *sub = lv_label_create(tile);
    lv_label_set_text(sub, "Coming soon");
    lv_obj_set_style_text_color(sub, lv_color_make(0x66, 0x66, 0x66), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(tile);
    lv_label_set_text(hint, "Swipe right for main");
    lv_obj_set_style_text_color(hint, lv_color_make(0x44, 0x44, 0x44), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);
}

/* -- Create Main tile (col=1) --------------------------------------- */

static void create_main_tile(lv_obj_t *tile)
{
    /* Title */
    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "Strong Vibes Knob");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* MIC arc gauge */
    arc_mic = lv_arc_create(tile);
    lv_obj_set_size(arc_mic, 200, 200);
    lv_arc_set_rotation(arc_mic, 135);
    lv_arc_set_bg_angles(arc_mic, 0, 270);
    lv_arc_set_range(arc_mic, 0, 100);
    lv_arc_set_value(arc_mic, 50);
    lv_obj_remove_style(arc_mic, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc_mic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_mic, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_mic, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_mic, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_mic, SV_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(arc_mic, LV_ALIGN_CENTER, 0, -15);

    /* MIC value */
    lbl_mic = lv_label_create(tile);
    lv_label_set_text(lbl_mic, "50");
    lv_obj_set_style_text_color(lbl_mic, SV_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_mic, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_mic, LV_ALIGN_CENTER, 0, -25);

    /* MIC label */
    lv_obj_t *mic_label = lv_label_create(tile);
    lv_label_set_text(mic_label, "MIC");
    lv_obj_set_style_text_color(mic_label, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_set_style_text_font(mic_label, &lv_font_montserrat_14, 0);
    lv_obj_align(mic_label, LV_ALIGN_CENTER, 0, 10);

    /* BLE activity */
    lbl_activity = lv_label_create(tile);
    lv_label_set_text(lbl_activity, "BLE");
    lv_obj_set_style_text_color(lbl_activity, SV_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_activity, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_activity, LV_ALIGN_TOP_RIGHT, -30, 30);
    lv_obj_add_flag(lbl_activity, LV_OBJ_FLAG_HIDDEN);

    /* Motor state */
    lbl_motor_state = lv_label_create(tile);
    lv_label_set_text(lbl_motor_state, "Idle");
    lv_obj_set_style_text_color(lbl_motor_state, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_set_style_text_font(lbl_motor_state, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_motor_state, LV_ALIGN_BOTTOM_MID, 0, -90);

    /* BLE status */
    lbl_webapp = lv_label_create(tile);
    lv_label_set_text(lbl_webapp, "Webapp: --");
    lv_obj_set_style_text_color(lbl_webapp, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_set_style_text_font(lbl_webapp, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_webapp, LV_ALIGN_BOTTOM_MID, 0, -65);

    lbl_emw = lv_label_create(tile);
    lv_label_set_text(lbl_emw, "EMW: --");
    lv_obj_set_style_text_color(lbl_emw, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_set_style_text_font(lbl_emw, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_emw, LV_ALIGN_BOTTOM_MID, 0, -40);
}

/* -- Create Settings tile (col=2) ----------------------------------- */

static lv_obj_t *radio_btns[3];

static void create_settings_tile(lv_obj_t *tile)
{
    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *sub = lv_label_create(tile);
    lv_label_set_text(sub, "Knob controls:");
    lv_obj_set_style_text_color(sub, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 70);

    static const char *labels[] = {"Power", "Speed", "Power & Speed"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cb = lv_checkbox_create(tile);
        lv_checkbox_set_text(cb, labels[i]);
        lv_obj_set_style_text_color(cb, lv_color_white(), 0);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_14, 0);
        lv_obj_align(cb, LV_ALIGN_TOP_MID, -40, 110 + i * 45);
        lv_obj_add_event_cb(cb, settings_radio_cb, LV_EVENT_VALUE_CHANGED,
                            (void *)(uintptr_t)i);

        /* Simulate radio: check first by default */
        if (i == 0) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        radio_btns[i] = cb;
    }

    lv_obj_t *note = lv_label_create(tile);
    lv_label_set_text(note, "(Phase 2)");
    lv_obj_set_style_text_color(note, lv_color_make(0x44, 0x44, 0x44), 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -60);

    lv_obj_t *hint = lv_label_create(tile);
    lv_label_set_text(hint, "Swipe left for main");
    lv_obj_set_style_text_color(hint, lv_color_make(0x44, 0x44, 0x44), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* Firmware release version (open-source support / regression attribution). */
    lv_obj_t *ver = lv_label_create(tile);
    lv_label_set_text_fmt(ver, "fw %s", SV_KNOB_FW_VERSION);
    lv_obj_set_style_text_color(ver, lv_color_make(0x66, 0x66, 0x66), 0);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_14, 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/* -- Radio button mutual exclusion ---------------------------------- */

static void enforce_radio(lv_event_t *e)
{
    lv_obj_t *active = lv_event_get_target(e);
    if (!lv_obj_has_state(active, LV_STATE_CHECKED)) {
        /* Don't allow unchecking — re-check it */
        lv_obj_add_state(active, LV_STATE_CHECKED);
        return;
    }
    /* Uncheck others */
    for (int i = 0; i < 3; i++) {
        if (radio_btns[i] && radio_btns[i] != active) {
            lv_obj_remove_state(radio_btns[i], LV_STATE_CHECKED);
        }
    }
}

/* -- Boot splash ---------------------------------------------------- */

static esp_timer_handle_t splash_timer;
static lv_obj_t *splash_overlay;

/* A connect prompt requested while the splash is still on screen is deferred:
 * its device name is stashed here and the modal is raised by splash_done_cb
 * once the splash is gone, so the prompt can never cover the brand splash.
 * build_connect_modal() is defined lower down and assumes the LVGL lock is
 * already held (splash_done_cb runs inside lv_timer_handler, under the lock). */
static bool connect_prompt_pending;
static char pending_connect_name[32];
static void build_connect_modal(const char *name);

/* Fade animation exec: object-level opa composites the whole subtree, so this
 * fades both the black backdrop and the logo together. */
static void splash_fade_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/* Runs on the LVGL task when the fade finishes; safe to delete the overlay.
 * If a wand was discovered during the splash, its connect prompt was deferred
 * (see display_show_connect_prompt) — raise it now that the splash is gone. */
static void splash_done_cb(lv_anim_t *a)
{
    lv_obj_t *o = (lv_obj_t *)a->var;
    lv_obj_delete(o);
    if (splash_overlay == o) splash_overlay = NULL;

    if (connect_prompt_pending) {
        connect_prompt_pending = false;
        build_connect_modal(pending_connect_name);
    }
}

/* One-shot esp_timer ~3 s after boot: fade the splash out, then delete it. */
static void splash_timer_cb(void *arg)
{
    (void)arg;
    if (lvgl_port_lock(0)) {
        if (splash_overlay) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, splash_overlay);
            lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
            lv_anim_set_duration(&a, 400);
            lv_anim_set_exec_cb(&a, splash_fade_cb);
            lv_anim_set_completed_cb(&a, splash_done_cb);
            lv_anim_start(&a);
        }
        lvgl_port_unlock();
    }
}

/* Full-screen black overlay with the centered brand logo, on the top layer
 * above the tileview. Created under the LVGL lock by the caller. */
static void create_splash(void)
{
    splash_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(splash_overlay, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(splash_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(splash_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_overlay, 0, 0);
    lv_obj_set_style_radius(splash_overlay, 0, 0);
    lv_obj_remove_flag(splash_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_image_create(splash_overlay);
    lv_image_set_src(logo, &sv_logo);
    lv_obj_center(logo);
}

/* -- Create UI with tileview ---------------------------------------- */

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    /* Tileview: 3 horizontal tiles */
    lv_obj_t *tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(tv, lv_color_black(), 0);

    lv_obj_t *t_patterns = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *t_main     = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    lv_obj_t *t_settings = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);

    create_patterns_tile(t_patterns);
    create_main_tile(t_main);
    create_settings_tile(t_settings);

    /* Wire radio button mutual exclusion after creation */
    for (int i = 0; i < 3; i++) {
        lv_obj_add_event_cb(radio_btns[i], enforce_radio, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* Start on main tile */
    lv_tileview_set_tile(tv, t_main, LV_ANIM_OFF);

    /* Brand splash on the top layer, above the tiles. Created last so it is
     * the topmost object; a one-shot timer fades it out shortly after boot. */
    create_splash();
}

/* -- Touch init ----------------------------------------------------- */

static void touch_init(lv_display_t *disp)
{
    /* I2C master bus (new API, shared with DRV2605 in future) */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    /* Touch panel IO */
    esp_lcd_panel_io_handle_t touch_io;
    esp_lcd_panel_io_i2c_config_t touch_io_cfg =
        ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &touch_io_cfg, &touch_io));

    /* CST816 touch handle */
    esp_lcd_touch_handle_t touch_handle;
    esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(
        touch_io, &touch_cfg, &touch_handle));

    /* Register with LVGL port */
    const lvgl_port_touch_cfg_t touch_port_cfg = {
        .disp = disp,
        .handle = touch_handle,
    };
    lv_indev_t *indev = lvgl_port_add_touch(&touch_port_cfg);
    if (indev) {
        ESP_LOGI(TAG, "Touch initialized (CST816 on I2C 0x15)");
    } else {
        ESP_LOGW(TAG, "Touch registration failed");
    }
}

/* -- Public API ----------------------------------------------------- */

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST77916 LCD...");

    /* 1. SPI bus (QSPI) */
    spi_bus_config_t bus_cfg = ST77916_PANEL_BUS_QSPI_CONFIG(
        LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3,
        LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* 2. Panel IO (QSPI) */
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_io_spi_config_t io_cfg =
        ST77916_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io_handle));

    /* 3. ST77916 panel with Waveshare init commands */
    st77916_vendor_config_t vendor_cfg = {
        .init_cmds      = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = LCD_RST,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel  = 16,
        .vendor_config   = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io_handle, &panel_cfg, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* 4. Backlight */
    backlight_init();

    /* 5. LVGL port */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = io_handle,
        .panel_handle = panel_handle,
        .buffer_size  = LCD_H_RES * 36,
        .double_buffer = true,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rounder_cb    = rounder_cb,
        .flags = {
            .buff_dma    = true,
            .swap_bytes  = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    assert(disp);

    /* 6. Touch input */
    touch_init(disp);

    /* 7. Create tileview UI */
    if (lvgl_port_lock(0)) {
        create_ui();
        lvgl_port_unlock();
    }

    /* 8. Activity flash timer */
    const esp_timer_create_args_t act_args = {
        .callback = activity_timer_cb,
        .name     = "ble_act",
    };
    esp_timer_create(&act_args, &activity_timer);

    /* Boot splash: hold the brand logo for ~3 s, then fade into the UI. */
    const esp_timer_create_args_t splash_args = {
        .callback = splash_timer_cb,
        .name     = "splash",
    };
    esp_timer_create(&splash_args, &splash_timer);
    esp_timer_start_once(splash_timer, 3000000);  /* 3 s */

    /* UI + LVGL are up; setters below may now take the LVGL lock safely. */
    display_ready = true;

    ESP_LOGI(TAG, "Display ready (%dx%d) with touch", LCD_H_RES, LCD_V_RES);
}

void display_set_mic(int value)
{
    if (!display_ready || !lbl_mic) return;
    if (lvgl_port_lock(0)) {
        lv_label_set_text_fmt(lbl_mic, "%d", value);
        if (arc_mic) {
            lv_arc_set_value(arc_mic, value);
        }
        lvgl_port_unlock();
    }
}

void display_set_ble_status(bool webapp_connected, bool emw_connected)
{
    if (!display_ready) return;
    if (lvgl_port_lock(0)) {
        if (lbl_webapp) {
            lv_label_set_text(lbl_webapp, webapp_connected
                              ? "Webapp: Connected"
                              : "Webapp: --");
            lv_obj_set_style_text_color(lbl_webapp,
                webapp_connected ? SV_ACCENT
                                 : lv_color_make(0x99, 0x99, 0x99), 0);
        }
        if (lbl_emw) {
            lv_label_set_text(lbl_emw, emw_connected
                              ? "EMW: Connected"
                              : "EMW: --");
            lv_obj_set_style_text_color(lbl_emw,
                emw_connected ? SV_ACCENT
                               : lv_color_make(0x99, 0x99, 0x99), 0);
        }
        lvgl_port_unlock();
    }
}

void display_notify_ble_activity(void)
{
    if (!display_ready || !lbl_activity) return;
    if (lvgl_port_lock(0)) {
        lv_obj_remove_flag(lbl_activity, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }
    esp_timer_stop(activity_timer);
    esp_timer_start_once(activity_timer, 300000);
}

void display_set_motor_state(uint8_t state, uint8_t pattern_id)
{
    if (!display_ready || !lbl_motor_state) return;
    if (lvgl_port_lock(0)) {
        const char *state_str;
        lv_color_t color;
        switch (state) {
        case 0:
            state_str = "Idle";
            color = lv_color_make(0x99, 0x99, 0x99);
            break;
        case 1:
            state_str = "Repeat";
            color = SV_ACCENT;
            break;
        case 2:
            state_str = "Once";
            color = lv_color_make(0xFB, 0xBF, 0x24);
            break;
        default:
            state_str = "Err";
            color = lv_color_make(0xF8, 0x71, 0x71);
            break;
        }
        /* Show "Flat" for both the wand's server-dictated flat (0xFF) and the
         * knob's own flat (KNOB_PATTERN_ID = 0x01 in vibe_protocol.h) — both are
         * flat patterns; the distinct id is only so the two can be told apart on
         * the wire. Literal 0x01 here to avoid pulling protocol headers into the
         * LVGL unit; keep in sync with KNOB_PATTERN_ID. */
        if (state > 0 && (pattern_id == 0xFF || pattern_id == 0x01)) {
            lv_label_set_text_fmt(lbl_motor_state, "%s Flat", state_str);
        } else if (state > 0 && pattern_id > 0) {
            lv_label_set_text_fmt(lbl_motor_state, "%s P:%d", state_str, pattern_id);
        } else {
            lv_label_set_text(lbl_motor_state, state_str);
        }
        lv_obj_set_style_text_color(lbl_motor_state, color, 0);
        lvgl_port_unlock();
    }
}

knob_mode_t display_get_knob_mode(void)
{
    return knob_mode;
}

/* -- Connect-prompt modal (ask before connecting to a wand) --------- */

static lv_obj_t *connect_modal;

/* Both button callbacks run on the LVGL task while the modal's own event is
 * being dispatched, so the object is deleted asynchronously (next timer pass)
 * to avoid freeing an ancestor mid-event. */
static void connect_yes_cb(lv_event_t *e)
{
    (void)e;
    if (connect_modal) {
        lv_obj_delete_async(connect_modal);
        connect_modal = NULL;
    }
    ble_client_confirm_connect();
}

static void connect_no_cb(lv_event_t *e)
{
    (void)e;
    if (connect_modal) {
        lv_obj_delete_async(connect_modal);
        connect_modal = NULL;
    }
    ble_client_dismiss_connect();
}

/* Build the ask-before-connect modal on the top layer. Assumes the LVGL lock
 * is held; a no-op if a modal is already up. */
static void build_connect_modal(const char *name)
{
    if (connect_modal) return;

    /* Full-screen panel on the top layer so it covers the tiles and takes
     * touch priority over them. */
    connect_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(connect_modal, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(connect_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(connect_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(connect_modal, 0, 0);
    lv_obj_set_style_radius(connect_modal, 0, 0);
    lv_obj_remove_flag(connect_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(connect_modal);
    lv_label_set_text(title, "EMW found");
    lv_obj_set_style_text_color(title, SV_ACCENT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t *sub = lv_label_create(connect_modal);
    lv_label_set_text(sub, name ? name : "EMW device");
    lv_obj_set_style_text_color(sub, lv_color_white(), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -45);

    lv_obj_t *yes = lv_button_create(connect_modal);
    lv_obj_set_size(yes, 150, 60);
    lv_obj_align(yes, LV_ALIGN_CENTER, 0, 15);
    lv_obj_set_style_bg_color(yes, SV_ACCENT, 0);
    lv_obj_add_event_cb(yes, connect_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yl = lv_label_create(yes);
    lv_label_set_text(yl, "Connect");
    lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
    lv_obj_center(yl);

    lv_obj_t *no = lv_button_create(connect_modal);
    lv_obj_set_size(no, 150, 50);
    lv_obj_align(no, LV_ALIGN_CENTER, 0, 90);
    lv_obj_set_style_bg_color(no, lv_color_make(0x33, 0x33, 0x33), 0);
    lv_obj_add_event_cb(no, connect_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = lv_label_create(no);
    lv_label_set_text(nl, "Not now");
    lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, 0);
    lv_obj_center(nl);

    ESP_LOGI(TAG, "Connect prompt shown for \"%s\"", name ? name : "?");
}

void display_show_connect_prompt(const char *name)
{
    if (!display_ready) return;
    if (!lvgl_port_lock(0)) return;

    if (splash_overlay) {
        /* Splash still on screen — defer the prompt until it fades out
         * (raised by splash_done_cb) so it can't cover the brand splash. */
        connect_prompt_pending = true;
        snprintf(pending_connect_name, sizeof pending_connect_name, "%s",
                 name ? name : "EMW device");
        ESP_LOGI(TAG, "Connect prompt deferred until splash ends");
    } else {
        build_connect_modal(name);
    }

    lvgl_port_unlock();
}

void display_hide_connect_prompt(void)
{
    if (!display_ready) return;
    if (!lvgl_port_lock(0)) return;
    connect_prompt_pending = false;   /* also cancel a still-deferred prompt */
    if (connect_modal) {
        lv_obj_delete(connect_modal);
        connect_modal = NULL;
    }
    lvgl_port_unlock();
}
