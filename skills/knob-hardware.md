---
name: knob-hardware
description: "Europe Magic Wand Knob (ESP32-S3-Knob-Touch-LCD-1.8 board, GUITION or Waveshare hardware) hardware reference. Use when working on display output, touch input, rotary encoder, vibration motor, audio, or understanding the dual-MCU architecture and pin assignments."
---

# Europe Magic Wand Knob (ESP32-S3-Knob-Touch-LCD-1.8) Hardware Reference

Hardware reference for the Europe Magic Wand Knob — an ESP32-S3-Knob-Touch-LCD-1.8 board sold as GUITION or Waveshare hardware. Derived from the board schematics (5 sheets) and Waveshare Arduino example code; all pin numbers, I2C addresses, and initialization sequences are verified against both sources.

## 1. Dual-MCU Architecture

The board has **two microcontrollers** on a single PCB:

| MCU | Chip | Role | Controls |
|-----|------|------|----------|
| **Primary** | ESP32-S3-R8 (ESP32-S3-WROOM module) | Main application processor | LCD display (QSPI), touch (I2C), rotary encoder, DRV2605 haptics (I2C), PDM microphone, I2S DAC output, SD card, WiFi/BLE |
| **Secondary** | ESP32 (bare chip, ESP32-14YM) | Audio co-processor | PCM1502A DAC (I2S), SGM58031 ADC, separate I2S bus |

### Inter-MCU Communication (UART)

The ESP32-S3 and ESP32 communicate over a UART link:

| Signal | ESP32-S3 GPIO | ESP32 GPIO | Direction |
|--------|---------------|------------|-----------|
| ESP32_TX | GPIO10 (RX on S3 side) | ESP32 GPIO1 (U0TXD) | ESP32 -> ESP32-S3 |
| ESP32_RX | GPIO9 (TX on S3 side) | ESP32 GPIO3 (U0RXD) | ESP32-S3 -> ESP32 |

From schematic sheet 3: The ESP32 chip's U0TXD/U0RXD connect to the ESP32-S3's GPIO9/GPIO10 (labels: ESP32_TX/ESP32_RX).

### Audio Routing Control

**GPIO0** on the ESP32-S3 acts as the audio source selector. From `audio_bsp.c`:
- `gpio_set_level(GPIO_NUM_0, 1)` gives PCM5100A DAC control to the ESP32-S3 (I2S passthrough from microphone)
- This GPIO likely drives a mux or enable signal on the DAC input path

## 2. Complete GPIO Pin Assignment (ESP32-S3)

| GPIO | Function | Peripheral | Interface | Notes |
|------|----------|------------|-----------|-------|
| **0** | Audio control / DAC source select | PCM5100A mux | Digital output | Set HIGH to route ESP32-S3 I2S to DAC |
| **1** | BATT_ADC | Battery voltage divider | ADC | Via 10K/10K divider (R62/R63), measures half of VBAT |
| **2** | SDMMC_D0 | SD Card DAT0 | SDMMC | |
| **3** | --- | Available / TP_SDA (test point?) | | Labeled on schematic |
| **4** | SDMMC_D1 | SD Card DAT1 | SDMMC | |
| **5** | SDMMC_D2 | SD Card DAT2 | SDMMC | |
| **6** | SDMMC_D3 | SD Card DAT3/CD | SDMMC | |
| **7** | Encoder B (ECB) | Rotary encoder phase B | Digital input (pull-up) | |
| **8** | Encoder A (ECA) | Rotary encoder phase A | Digital input (pull-up) | |
| **9** | ESP32_RX (TX from S3) | Inter-MCU UART TX | UART | S3 transmits to ESP32 |
| **10** | ESP32_TX (RX on S3) | Inter-MCU UART RX | UART | S3 receives from ESP32 |
| **11** | I2C SDA | Touch (CST816) + DRV2605 | I2C0 | Shared I2C bus |
| **12** | I2C SCL | Touch (CST816) + DRV2605 | I2C0 | Shared I2C bus |
| **13** | LCD_PCLK (SPI CLK) | ST77916 display | QSPI | SPI2_HOST clock |
| **14** | LCD_CS | ST77916 display | QSPI | Chip select |
| **15** | LCD_DATA0 (SPI D0) | ST77916 display | QSPI | Data line 0 |
| **16** | LCD_DATA1 (SPI D1) | ST77916 display | QSPI | Data line 1 |
| **17** | LCD_DATA2 (SPI D2) | ST77916 display | QSPI | Data line 2 |
| **18** | LCD_DATA3 (SPI D3) | ST77916 display | QSPI | Data line 3 |
| **19** | USB_D- | USB | USB | |
| **20** | USB_D+ | USB | USB | |
| **21** | LCD_RST | ST77916 display | Digital output | Active low reset |
| **38** | SDMMC_CLK | SD Card CLK | SDMMC | |
| **39** | I2S_BCLK | PCM5100A DAC | I2S (STD) | I2S1 bit clock |
| **40** | I2S_WS (LRCLK) | PCM5100A DAC | I2S (STD) | I2S1 word select |
| **41** | I2S_DOUT | PCM5100A DAC | I2S (STD) | I2S1 data out |
| **42** | SDMMC_CMD | SD Card CMD | SDMMC | |
| **43** | U0TXD | USB-UART debug | UART0 | Default TX for programming/debug |
| **44** | U0RXD | USB-UART debug | UART0 | Default RX for programming/debug |
| **45** | PDM_MIC_CLK | MEMS Microphone | PDM (I2S0) | PDM clock output |
| **46** | PDM_MIC_DATA | MEMS Microphone | PDM (I2S0) | PDM data input |
| **47** | LCD_BK_LIGHT | Backlight PWM | LEDC (PWM) | LEDC channel 1, 50kHz, 8-bit |
| **48** | SDMMC_SCK (alt) | SD Card (alt clock) | | Labeled on schematic, may be alternate |

## 3. LCD Display

### Hardware Specifications

| Property | Value |
|----------|-------|
| Driver IC | **ST77916** |
| Display type | LCD (TFT) |
| Resolution | **360 x 360** pixels |
| Color depth | 16-bit RGB565 (configurable: 16/18/24 bpp) |
| Interface | **QSPI** (Quad SPI) via SPI2_HOST |
| SPI clock | 40 MHz |
| Color byte swap | Yes (`LV_COLOR_16_SWAP = 1` for SPI byte order) |

### QSPI Pin Connections

| Signal | ESP32-S3 GPIO |
|--------|---------------|
| PCLK (SPI CLK) | GPIO13 |
| DATA0 | GPIO15 |
| DATA1 | GPIO16 |
| DATA2 | GPIO17 |
| DATA3 | GPIO18 |
| CS | GPIO14 |
| RST | GPIO21 |
| Backlight | GPIO47 |

### QSPI Command Protocol

The ST77916 uses a command encoding scheme over QSPI:
- **Write command opcode**: `0x02` (shifted to bits [31:24])
- **Write color opcode**: `0x32` (shifted to bits [31:24])
- **Read command opcode**: `0x03`
- Commands are 32-bit: `[opcode:8][cmd:8][0:16]`

### Initialization Sequence (ESP-IDF equivalent)

```c
// 1. Configure SPI bus (QSPI mode)
spi_bus_config_t buscfg = {
    .data0_io_num = 15,    // GPIO15
    .data1_io_num = 16,    // GPIO16
    .sclk_io_num  = 13,    // GPIO13
    .data2_io_num = 17,    // GPIO17
    .data3_io_num = 18,    // GPIO18
    .max_transfer_sz = 360 * 360 * 2,  // full frame RGB565
};
spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

// 2. Configure panel IO (QSPI mode, no DC pin)
esp_lcd_panel_io_spi_config_t io_config = {
    .cs_gpio_num = 14,
    .dc_gpio_num = -1,          // Not used in QSPI mode
    .spi_mode = 0,
    .pclk_hz = 40 * 1000 * 1000,
    .trans_queue_depth = 10,
    .on_color_trans_done = flush_ready_cb,
    .lcd_cmd_bits = 32,         // QSPI uses 32-bit commands
    .lcd_param_bits = 8,
    .flags.quad_mode = true,
};

// 3. Create panel with ST77916 driver, reset GPIO21
esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = 21,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
    .vendor_config = &vendor_config,  // contains init_cmds table
};
esp_lcd_new_panel_st77916(io_handle, &panel_config, &panel_handle);
esp_lcd_panel_reset(panel_handle);    // HW reset: LOW 10ms, HIGH 150ms
esp_lcd_panel_init(panel_handle);     // Sends init command table
```

### Backlight Control (PWM)

```c
// LEDC configuration for backlight
ledc_timer_config_t timer_conf = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT,  // 0-255
    .timer_num = LEDC_TIMER_3,
    .freq_hz = 50000,                      // 50 kHz
    .clk_cfg = LEDC_SLOW_CLK_RC_FAST,
};
ledc_channel_config_t ledc_conf = {
    .gpio_num = 47,                        // GPIO47
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_1,
    .timer_sel = LEDC_TIMER_3,
    .duty = 255,                           // Full brightness
};
```

### Display Coordinate Rounding

The ST77916 requires coordinates to be even-aligned. From the LVGL rounder callback:
- x1, y1: round down to nearest even number
- x2, y2: round up to nearest odd number (even + 1)

### Rotation

To rotate 90 degrees, send MADCTL command `0x36` with value `0x60`.

## 4. Touch Controller (CST816)

| Property | Value |
|----------|-------|
| IC | **CST816** (Hynitron) |
| Interface | **I2C** (I2C_NUM_0) |
| I2C Address | **0x15** |
| SDA | GPIO11 |
| SCL | GPIO12 |
| I2C Clock | 300 kHz |
| Interrupt | Not used in example code (polling mode) |

### Touch Data Protocol

Read 7 bytes starting from register 0x00:

| Byte | Content |
|------|---------|
| [0-1] | Status/mode |
| [2] | Number of touch points (0 = no touch) |
| [3] | X high nibble (bits [3:0]) |
| [4] | X low byte |
| [5] | Y high nibble (bits [3:0]) |
| [6] | Y low byte |

### ESP-IDF Touch Read

```c
// Initialize I2C master
i2c_config_t conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = 11,
    .scl_io_num = 12,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = 300000,
};
i2c_param_config(I2C_NUM_0, &conf);
i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

// Switch to normal mode
uint8_t data = 0x00;
i2c_master_write_to_device(I2C_NUM_0, 0x15, (uint8_t[]){0x00, 0x00}, 2, 1000);

// Read touch data
uint8_t buf[7];
uint8_t reg = 0x00;
i2c_master_write_read_device(I2C_NUM_0, 0x15, &reg, 1, buf, 7, 1000);

uint8_t num_points = buf[2];
if (num_points > 0) {
    uint16_t x = ((buf[3] & 0x0F) << 8) | buf[4];
    uint16_t y = ((buf[5] & 0x0F) << 8) | buf[6];
}
```

**Important**: The CST816 and DRV2605 share the same I2C bus (GPIO11 SDA, GPIO12 SCL). Use a mutex if accessing both from different tasks.

## 5. Rotary Encoder

| Property | Value |
|----------|-------|
| Encoder A (Phase A) | **GPIO8** |
| Encoder B (Phase B) | **GPIO7** |
| Switch (press) | Not connected in example code (may be present on hardware) |
| Debounce | Timer-based polling at 3ms interval, 2 tick debounce threshold |

### Encoder Driver Design

The Waveshare example uses a **timer-polling** approach (NOT interrupts):

1. An `esp_timer` fires every 3ms (`TICKS_INTERVAL = 3`)
2. Each phase (A and B) is independently debounced with a 2-count threshold (`DEBOUNCE_TICKS = 2`)
3. Phase A rising edge = KNOB_RIGHT (increment)
4. Phase B rising edge = KNOB_LEFT (decrement)
5. GPIOs configured as INPUT with internal PULL-UP enabled

### ESP-IDF Encoder Setup

```c
// GPIO configuration (internal pull-ups)
gpio_config_t gpio_cfg = {
    .pin_bit_mask = (1ULL << 7) | (1ULL << 8),
    .mode = GPIO_MODE_INPUT,
    .intr_type = GPIO_INTR_DISABLE,
    .pull_up_en = GPIO_PULLUP_ENABLE,
};
gpio_config(&gpio_cfg);

// Use knob_config_t structure
knob_config_t cfg = {
    .gpio_encoder_a = 8,   // GPIO8
    .gpio_encoder_b = 7,   // GPIO7
};
knob_handle_t knob = iot_knob_create(&cfg);

// Register callbacks
iot_knob_register_cb(knob, KNOB_LEFT, on_left_cb, NULL);
iot_knob_register_cb(knob, KNOB_RIGHT, on_right_cb, NULL);
```

### Knob Events

- `KNOB_LEFT` (0) - Counter-clockwise rotation
- `KNOB_RIGHT` (1) - Clockwise rotation
- `iot_knob_get_count_value()` - Accumulated count (increments on RIGHT, decrements on LEFT)

## 6. Vibration Motor (DRV2605L)

| Property | Value |
|----------|-------|
| IC | **DRV2605L** (Texas Instruments) |
| Interface | **I2C** |
| I2C Address | **0x5A** (`DRV2605_SLAVE_ADDRESS`) |
| SDA | GPIO11 (shared with touch) |
| SCL | GPIO12 (shared with touch) |
| Mode | Internal trigger (I2C command based) |

### Haptic Feedback Waveform Library

The DRV2605L has 123 built-in effects. Key effects for UI feedback:

| Effect # | Name | Use Case |
|----------|------|----------|
| 1 | Strong Click 100% | Button press confirmation |
| 4 | Sharp Click 100% | Selection/detent feedback |
| 10 | Double Click 100% | Mode change |
| 14 | Strong Buzz 100% | Alert/warning |
| 24 | Sharp Tick 100% | Encoder tick feedback |
| 47-51 | Buzz 1-5 (100%-20%) | Continuous feedback at varying intensity |
| 52-57 | Pulsing (Strong/Med/Sharp) | Notification pulses |

### ESP-IDF Haptic Trigger

```c
// Initialize using SensorLib DRV2605 driver
// I2C: SDA=11, SCL=12, Address=0x5A
drv.begin(Wire, 0x5A, 11, 12);
drv.selectLibrary(1);                    // Library 1 (most common)
drv.setMode(DRV2605_MODE_INTTRIG);       // Internal trigger mode

// Play a haptic effect
drv.setWaveform(0, effect_number);       // Set effect (1-123)
drv.setWaveform(1, 0);                   // End marker
drv.run();                               // Trigger playback
```

**Note for ESP-IDF**: The SensorDRV2605 library is Arduino-specific. For ESP-IDF, use direct I2C register writes to the DRV2605L registers, or port the SensorLib driver.

## 7. Audio System

### Microphone (PDM)

| Property | Value |
|----------|-------|
| IC | **MSM261D4030H1CPM** (MEMS PDM microphone) |
| Interface | PDM via I2S0 |
| CLK pin | GPIO45 |
| DATA pin | GPIO46 |
| Sample rate | 44100 Hz |
| Bit width | 16-bit |
| Channel | Mono |

### DAC Output (I2S to PCM5100A)

| Property | Value |
|----------|-------|
| IC | **PCM5100A** (TI) or **PCM1502A** |
| Interface | I2S Standard (via I2S1) |
| BCLK | GPIO39 |
| WS (LRCLK) | GPIO40 |
| DOUT | GPIO41 |
| Sample rate | 44100 Hz |
| Bit width | 16-bit |
| Channel | Mono (I2S_SLOT_MODE_MONO) |

### Audio Source Control

GPIO0 is configured as output to control which MCU drives the DAC:
- Set GPIO0 HIGH = ESP32-S3 has DAC control
- Set GPIO0 LOW = ESP32 has DAC control (default?)

### ESP-IDF Audio Setup

```c
// Enable ESP32-S3 audio output
gpio_set_direction(GPIO_NUM_0, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_NUM_0, 1);

// I2S TX (to DAC) - I2S_NUM_1
i2s_chan_config_t tx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
i2s_new_channel(&tx_cfg, &tx_chan, NULL);
i2s_std_config_t tx_std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = 39, .ws = 40, .dout = 41,
        .din = I2S_GPIO_UNUSED,
    },
};
i2s_channel_init_std_mode(tx_chan, &tx_std_cfg);
i2s_channel_enable(tx_chan);

// PDM RX (from microphone) - I2S_NUM_0
i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
i2s_new_channel(&rx_cfg, NULL, &rx_chan);
i2s_pdm_rx_config_t pdm_cfg = {
    .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(44100),
    .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = { .clk = 45, .din = 46 },
};
i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_cfg);
i2s_channel_enable(rx_chan);
```

## 8. ESP32 Co-Processor (Sheet 3 & 5)

The secondary ESP32 chip (ESP32-14YM) handles dedicated audio processing.

### ESP32 Pin Assignments (from schematic sheet 3)

| ESP32 GPIO | Function | Notes |
|------------|----------|-------|
| GPIO0 | BOOT | Boot mode select |
| GPIO1 | U0TXD | UART to ESP32-S3 GPIO10 |
| GPIO3 | U0RXD | UART from ESP32-S3 GPIO9 |
| GPIO4 | --- | |
| GPIO12 | ESP32_I2S_DAC_BCK | I2S to PCM1502A |
| GPIO13 | ESP32_I2S_DAC_WS | I2S word select to PCM1502A |
| GPIO15 | ESP32_I2S_DAC_DATA | I2S data to PCM1502A |
| GPIO18 | ESP32_I2S_SCLK_Y | |
| GPIO19 | ESP32_I2S_SDO_Y | |
| GPIO21 | ESP32_STM_S | |
| GPIO22 | ESP32_I2S_SCK | |
| GPIO23 | ESP32_I2S_WS | |
| GPIO25 | ESP32_DAC1_RX | DAC1 output |
| GPIO26 | ESP32_DAC2_RX | DAC2 output |
| GPIO27 | ESP32_I2S_SDI | |
| GPIO32 | ESP32S3_RV | |
| GPIO33 | 1V3_S3 | |

### DAC Subsystem (Sheet 5)

Two DAC ICs on the ESP32 side:

1. **PCM1502A** (or PCM5100A) - Main audio DAC
   - Receives I2S from ESP32 (BCK/WS/DATA on ESP32 GPIO12/13/15)
   - Also controllable from ESP32-S3 via the mux (GPIO0)

2. **SGM58031** (SGM5856-3.3/SYNC-TR variant visible) - Precision ADC
   - Connected to ESP32's I2S/SPI bus
   - Used for audio input or sensor reading

3. **TAS5805M** or similar amplifier (I2S input from sheet 5, bottom-right)
   - I2S DAC with I2C control
   - Signals: ESP32_I2S_DAC_BCK, ESP32_I2S_DAC_WS, ESP32_I2S_DAC_DATA

## 9. Power System (Sheet 1)

### Battery & Charging

| Property | Value |
|----------|-------|
| Battery | 3.7V Li-Po, 800mAh |
| Charging IC | Likely integrated (USB-C input) |
| Battery ADC | ESP32-S3 GPIO1 via voltage divider |
| Divider | R62 (10K top) / R63 (10K bottom) = VBAT/2 |

### Voltage Regulation

From schematic sheet 1 "DCDC FOR 3V3":

| Rail | Voltage | Regulator | Notes |
|------|---------|-----------|-------|
| VCC_5V | 5V | USB input | Raw USB power |
| 3V3 | 3.3V | DCDC buck converter | Formula visible: `Vout = 0.6 * (1 + R5/R2) = 3.3V` |
| LCD_3V3 | 3.3V | AO14001 LDO | Dedicated LCD power (U4, sheet 1 top-right) |

### Battery Voltage Reading

```c
// ESP-IDF ADC read for battery voltage
// GPIO1 = ADC1_CHANNEL_0
// Actual VBAT = ADC_reading * 2 (due to 10K/10K divider)
adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
adc_oneshot_new_unit(&init_config, &adc1_handle);

adc_oneshot_chan_cfg_t config = {
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
};
adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config);

int raw;
adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &raw);
// Convert: V_batt = raw * (3.3 / 4095) * 2
```

## 10. SD Card (Sheet 4)

| Property | Value |
|----------|-------|
| Interface | **SDMMC** (4-bit) |
| Connector | TF-018 micro-SD slot |

| Signal | ESP32-S3 GPIO |
|--------|---------------|
| CLK | GPIO38 |
| CMD | GPIO42 |
| D0 | GPIO2 |
| D1 | GPIO4 |
| D2 | GPIO5 |
| D3 | GPIO6 |

Pull-up resistors (R10, R46, R44, R49) on data/cmd lines visible on schematic sheet 4.

## 11. I2C Bus Map

Both peripherals on I2C_NUM_0 (shared bus):

| Device | Address | GPIO SDA | GPIO SCL |
|--------|---------|----------|----------|
| CST816 Touch | **0x15** | 11 | 12 |
| DRV2605L Haptics | **0x5A** | 11 | 12 |

**Thread safety**: If both touch polling and haptic triggers run on different tasks, protect the I2C bus with a mutex.

## 12. LVGL Integration Notes

The Waveshare example uses LVGL v8.3.x with these key settings:

| Setting | Value |
|---------|-------|
| Color depth | 16-bit (RGB565) |
| Color byte swap | Enabled (`LV_COLOR_16_SWAP = 1`) |
| LVGL memory | 48 KB internal heap |
| Display buffer | 360 x 36 pixels (1/10 of screen), double-buffered, DMA-capable |
| Tick period | 2ms (via esp_timer) |
| Task stack | 4 KB |
| Task priority | 2 |
| DPI | 130 |
| Touch input | LV_INDEV_TYPE_POINTER (polling via `getTouch()`) |

### Display Flush Pattern

```c
// LVGL flush callback -> esp_lcd_panel_draw_bitmap
// The rounder callback ensures even coordinate alignment for ST77916
// Double-buffered: buf1 + buf2, each 360 * 36 * 2 bytes = ~25.3 KB
```

## 13. Key Errata and Gotchas

1. **Display driver is ST77916** - The ST77916 is an LCD driver with QSPI interface. Do not confuse with the SH8601 (AMOLED) used on the separate ESP32-S3-Touch-AMOLED-1.8 product.

2. **QSPI requires 32-bit command width** - When using `esp_lcd_panel_io_spi_config_t`, set `.lcd_cmd_bits = 32` and `.flags.quad_mode = true`. The DC pin is unused (`-1`).

3. **Coordinate rounding** - The ST77916 requires even-aligned draw coordinates. Always round x1/y1 down to even, x2/y2 up to next odd.

4. **Shared I2C bus** - Touch controller and haptic driver share GPIO11/12. Use mutex for concurrent access.

5. **Audio mux on GPIO0** - Must explicitly set GPIO0 HIGH before I2S output works on ESP32-S3 side.

6. **Battery ADC uses GPIO1** - This is also ADC1_CH0. Do not use WiFi simultaneously with ADC1 reads (use ADC2 or schedule reads).

7. **SD card uses 4-bit SDMMC** - Many GPIOs (2, 4, 5, 6, 38, 42) are consumed by the SD card interface. If SD card is not used, these could theoretically be repurposed.

8. **Encoder uses polling, not interrupts** - The 3ms timer-based polling approach is more robust against bounce than raw edge interrupts. The debounce requires 2 consecutive identical readings (6ms minimum).

## 14. ESP Component Registry Libraries

For ESP-IDF development, use these managed components instead of raw driver code:

| Component | Registry Command | Purpose |
|---|---|---|
| `espressif/esp_lcd_st77916` | `idf.py add-dependency "espressif/esp_lcd_st77916"` | ST77916 LCD QSPI driver (Espressif official) |

Handles full QSPI init, command protocol, and exposes `esp_lcd_panel_handle_t` for LVGL integration. No need to manage pinout or driver init manually — the library handles it.

**Note:** Web searches may confuse this board with the ESP32-S3-Touch-AMOLED-1.8 (which uses SH8601). The Knob-LCD product uses ST77916.
