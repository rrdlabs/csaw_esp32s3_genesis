# Hardware Configuration Records (preflight)

Board: **ESP32-S3-TOUCH-AMOLED-1.8, Revision 1 (original SH8601 + FT3168)**.
Sources: `docs/HARDWARE_SOURCES.md`, `docs/DISPLAY_BRINGUP.md`, `docs/PREFLIGHT_MANIFEST.md`.

| Variable | Value / source |
| --- | --- |
| `CSAW_BOARD_REVISION` | `1` (original SH8601 + FT3168; not V2 CO5300/CST820) |
| `CSAW_IDF_ACTIVATION` | `source /home/d31337m3/devenv/env-6.1.sh` (Warroom); verified `ESP-IDF v6.1` |
| Display controller | SH8601, 368x448 QSPI AMOLED, 16 bpp RGB565 |
| QSPI pins | PCLK=11, CS=12, DATA0..3=4,5,6,7; SPI2_HOST; DMA auto |
| RGB element order | `LCD_RGB_ELEMENT_ORDER_RGB` (MADCTL RGB, not BGR) |
| Reset | hardware RST not connected (`GPIO_NUM_NC`); software reset `0x01` |
| Touch controller | FT3168 (`esp_lcd_touch_ft5x06` driver), I2C, INT=21 |
| I2C bus | SCL=14, SDA=15 |
| IO expander | TCA9554/XCA9554 @ `0x20`; pins 0–2 LOW → 20 ms → HIGH enable rails |
| PMU | AXP2101, `PMU.begin(AXP2101_SLAVE_ADDRESS, ...)` + rail verification |
| RTC | PCF85063A |
| IMU | QMI8658 six-axis |
| Audio codec | ES8311; I2S MCLK=16, BCLK=9, WS=45, DO=10, DI=8, PA=46 |
| Storage | microSD over SDMMC, 1-bit mode (CLK=2, CMD=1, DATA=3), 8 GB card installed |
| Brightness floor | 30% (`>=77/255` on register `0x51`) immediately after AMOLED init, before UI draw |
| Init order (mandated) | IO Expander → PMU → SH8601 AMOLED, each with reset/register setup/readiness check |
| BSP version | `waveshare/esp32_s3_touch_amoled_1_8` 1.1.4 (Rev 1); 2.0.3 is V2 reference only |
| Panel driver component | `waveshare/esp_lcd_sh8601` (registry versions 2.0.0, 1.0.2, 1.0.1, 1.0.0) |

Full register table, delays and demo line-by-line sequence: `docs/DISPLAY_BRINGUP.md`.
All original blockers resolved; see `docs/PREFLIGHT_MANIFEST.md` §4.
