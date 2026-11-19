# Hardware Source Evidence — ESP32-S3-TOUCH-AMOLED-1.8 Rev 1

**Exact demo-derived bringup sequence, QSPI/RGB/brightness details:** see [DISPLAY_BRINGUP.md](DISPLAY_BRINGUP.md). That document is the authoritative reference for AGENTS.md §4 implementation.

## Board identity (source: vendor README / wiki)

| Feature | Device / interface |
| --- | --- |
| MCU | ESP32-S3 |
| Display | 1.8-inch 368 x 448 QSPI AMOLED |
| **Original / Rev 1** | **SH8601 with FT3168 capacitive touch** |
| V2 (do not use for this build) | CO5300 with CST820 capacitive touch |
| PMU | AXP2101 (I2C `AXP2101_SLAVE_ADDRESS`) |
| RTC | PCF85063A |
| IMU | QMI8658 six-axis |
| Audio | ES8311 codec, mic input, speaker amplifier |
| Storage | microSD over SDMMC (1-bit: CLK=2, CMD=1, DATA=3) |
| IO expander | TCA9554 / XCA9554 at I2C `0x20` |
| I2C bus | SCL=14, SDA=15 |

## Rev 1 BSP source (v1.1.4)

Path: `third_party/espressif/waveshare_bsp_v1.1.4/esp32_s3_touch_amoled_1_8.c`

- `#include "esp_lcd_sh8601.h"` (line 15)
- `sh8601_lcd_init_cmd_t lcd_init_cmds[]` (line 53) — vendor panel init command table (see DISPLAY_BRINGUP.md §4)
- `esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle)` (line 406)
- `esp_lcd_panel_reset()` → `esp_lcd_panel_init()` → `esp_lcd_panel_disp_on_off(true)` (~lines 407–409)
- `bsp_io_expander_init()` (line 455) — `bsp_i2c_init()` then `esp_io_expander_new_i2c_tca9554(...)`
- `bsp_display_brightness_init()` (line 333) — calls `bsp_display_brightness_set(100)`
- `bsp_display_brightness_set()` (line 339) — validates 0–100, sends register `0x51` with `percent*255/100`
- `bsp_display_start_with_config()` (line 563) order: `lvgl_port_init` → `bsp_display_lcd_init` → `bsp_display_indev_init` → `bsp_display_brightness_init`
- Panel config: `.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB`, `.bits_per_pixel = 16`, `.flags.use_qspi_interface = 1`

## Mandated constraints vs vendor evidence

1. **Brightness floor >= 30% immediately post init** — supported by vendor `bsp_display_brightness_set(0..100)` and demo calls at 80%/85%/255. CsAW must call it at >= 30% before any UI draw and assert in logs. Full evidence in DISPLAY_BRINGUP.md §2.

2. **Exact power-up chain: IO Expander -> PMU -> SH8601 AMOLED** — vendor BSP defines but does not invoke `bsp_io_expander_init()` from `bsp_display_start()`, and AXP2101 setup lives in a separate example. The wiki demo `04_GFX_FT3168_Image` supplies the missing line-by-line evidence: I2C → expander probe `0x20` → pins 0/1/2 `LOW → 20 ms → HIGH` rail enable → touch readiness → `gfx->begin()`. CsAW composes this with the AXP2101 `PMU.begin()` reference into the mandated three-stage order (DISPLAY_BRINGUP.md §5). Operator directive confirms following the demo sequence exactly.

3. **IDF 6.1** — vendor example manifest `>=5.5,<6.1` is explicitly non-binding per operator; BSP allows `>=5.3`. CsAW uses Warroom `env-6.1.sh` (verified `ESP-IDF v6.1`) and will adapt/recreate non-conforming components (DISPLAY_BRINGUP.md §6).

## Reference sources

- https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8 (commit `01fd3200...`)
- https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8 (saved to `docs/sources/waveshare-wiki.{html,txt}`)
- https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8
- https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8
