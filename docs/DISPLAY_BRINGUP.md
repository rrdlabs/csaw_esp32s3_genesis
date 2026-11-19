# Exact Rev 1 Display Bringup Sequence (vendor demo extraction)

**Board:** ESP32-S3-TOUCH-AMOLED-1.8, **Revision 1** (SH8601 + FT3168).
**Purpose:** Authoritative preflight reference for implementation of AGENTS.md §4. Do not improvise this sequence — follow it line by line, then apply the CsAW brightness floor and explicit expander/PMU staging described below.

Sources (all local copies staged):

| Ref | File |
| --- | --- |
| D1 | `third_party/waveshare/esp32-s3-touch-amoled-1.8-rev1/examples/arduino/examples/01_HelloWorld/01_HelloWorld.ino` |
| D2 | `third_party/waveshare/esp32-s3-touch-amoled-1.8-rev1/examples/arduino/examples/04_GFX_FT3168_Image/04_GFX_FT3168_Image.ino` |
| D3 | `third_party/waveshare/esp32-s3-touch-amoled-1.8-rev1/examples/arduino/libraries/Mylibrary/pin_config.h` |
| D4 | `third_party/waveshare/esp32-s3-touch-amoled-1.8-rev1/examples/arduino/libraries/GFX_Library_for_Arduino/src/display/Arduino_SH8601.{h,cpp}` |
| D5 | `third_party/espressif/waveshare_bsp_v1.1.4/esp32_s3_touch_amoled_1_8.c` (Rev 1 ESP-IDF BSP) |
| D6 | `third_party/espressif/waveshare_bsp_v1.1.4/include/bsp/esp32_s3_touch_amoled_1_8.h` |
| D7 | `third_party/waveshare/.../examples/esp-idf/90_axp2101_pmu/main/port_axp2101.cpp` (PMU reference) |
| W | `docs/sources/waveshare-wiki.txt` (saved official wiki: §5 demos 01_AXP2101 … 05_LVGL_WITH_RAM) |

## 1. Pin / bus map (D3, D6)

| Signal | GPIO | Notes |
| --- | --- | --- |
| QSPI `LCD_SCLK` / `BSP_LCD_PCLK` | 11 | SPI2_HOST (`BSP_LCD_SPI_NUM`) |
| QSPI `LCD_CS` / `BSP_LCD_CS` | 12 | active low |
| QSPI `LCD_SDIO0` / `DATA0` | 4 | |
| QSPI `LCD_SDIO1` / `DATA1` | 5 | |
| QSPI `LCD_SDIO2` / `DATA2` | 6 | |
| QSPI `LCD_SDIO3` / `DATA3` | 7 | |
| Hardware reset | **not connected** (`GFX_NOT_DEFINED` / `BSP_LCD_RST = GPIO_NUM_NC`) | software reset 0x01 must be used |
| I2C SCL / SDA | 14 / 15 | shared by expander, PMU, touch, RTC, IMU, codec |
| FT3168 touch INT (`TP_INT`) | 21 | |
| SDMMC CLK / CMD / DATA | 2 / 1 / 3 | 1-bit mode |
| I2S MCLK / BCLK / WS / DO / DI | 16 / 9 / 45 / 10 / 8 | ES8311; PA enable 46 |
| Panel resolution / format | 368 x 448, 16 bpp (RGB565) | `BSP_LCD_BITS_PER_PIXEL = 16` |

## 2. Exact demo call sequence (D2 — the demo that uses display, expander and touch)

```c
/* 1. I2C bus */
Wire.begin(IIC_SDA /*15*/, IIC_SCL /*14*/);

/* 2. IO expander (XCA9554/TCA9554 at 0x20) — MUST succeed or abort */
if (!expander.begin(0x20)) { /* "Failed to find XCA9554 chip" -> while(1); */ }

/* 3. Expander pins 0,1,2 -> power rails */
expander.pinMode(0, OUTPUT); expander.pinMode(1, OUTPUT); expander.pinMode(2, OUTPUT);
expander.digitalWrite(0, LOW); expander.digitalWrite(1, LOW); expander.digitalWrite(2, LOW);
delay(20);
expander.digitalWrite(0, HIGH); expander.digitalWrite(1, HIGH); expander.digitalWrite(2, HIGH);

/* 4. Touch controller readiness (FT3168) */
while (FT3168->begin() == false) { log("FT3168 initialization fail"); delay(2000); }

/* 5. QSH8601 display init, then first frame, then brightness */
gfx->begin();
gfx->fillScreen(RGB565_WHITE);
for (int i = 0; i <= 255; i++) { gfx->setBrightness(i); delay(5); }
```

D1 (`01_HelloWorld`) confirms the display-only path: `gfx->begin()` → `gfx->fillScreen(RGB565_BLACK)` → `gfx->setBrightness(255)` (100%). The QSPI bus object is `Arduino_ESP32QSPI(LCD_CS=12, LCD_SCLK=11, SDIO0=4, SDIO1=5, SDIO2=6, SDIO3=7)` and the panel object is `Arduino_SH8601(bus, RST=GFX_NOT_DEFINED, rotation=0, 368, 448)`.

**CsAW adaptation:** the D2 brightness ramp is a demo effect, not a runtime requirement. CsAW must set **>= 30% (>= 77/255)** immediately after `gfx->begin()`/panel init and **before any UI is drawn**, and assert the value in logs (AGENTS.md §4.2). Vendor examples otherwise use 80% (`00_bsp_quickstart`) and 85% (`14_lvgl_demo_v9`); either satisfies the floor, but the init call itself is mandatory.

## 3. QSPI / color / endianness (D4, D5)

- Bus: `SH8601_PANEL_BUS_QSPI_CONFIG(PCLK=11, DATA0=4, DATA1=5, DATA2=6, DATA3=7, 368*448*16/8)`; `spi_bus_initialize(SPI2_HOST, ..., SPI_DMA_CH_AUTO)`.
- Panel IO: `SH8601_PANEL_IO_QSPI_CONFIG(CS=12, NULL, NULL)`; `esp_lcd_new_panel_io_spi()`.
- Vendor config: `.init_cmds = lcd_init_cmds`, `.flags.use_qspi_interface = 1` (QSPI mode, not single-SPI).
- Panel config: `.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB`, `.bits_per_pixel = 16`, `.reset_gpio_num = GPIO_NUM_NC`.
- Arduino driver: `SH8601_MADCTL_COLOR_ORDER = SH8601_MADCTL_RGB` — **RGB element order, not BGR** (D4). `tftInit()` emits software reset `0x01` when RST is not connected, then the init operations, then `invertDisplay(false)`.
- Brightness command: register `0x51` (`SH8601_W_WDBRIGHTNESSVALNOR`), 8-bit value 0–255. ESP-IDF BSP maps percent → duty as `brightness_percent * 255 / 100` and transmits via `esp_lcd_panel_io_tx_param(io_handle, (0x02 << 24) | 0x51 << 8, &param, 1)` (D5).

## 4. Register init table with delays (D5, Rev 1 BSP v1.1.4)

| # | Command | Payload | Delay after |
| --- | --- | --- | --- |
| 1 | `0x11` (SLPOUT) | — | 120 ms |
| 2 | `0x44` (SETTSL) | `01 D1` | 0 |
| 3 | `0x35` (TEARON) | `00` | 0 |
| 4 | `0x53` (WCTRLD1) | `20` | 10 ms |
| 5 | `0x2A` (CASET) | `00 00 01 6F` (0..367) | 0 |
| 6 | `0x2B` (PASET) | `00 00 01 BF` (0..447) | 0 |
| 7 | `0x51` (brightness) | `00` (dark during init) | 10 ms |
| 8 | `0x29` (DISPON) | — | 10 ms |
| 9 | `0x51` (brightness) | `FF` (full) | 0 |

Arduino-side reset/standby timings (D4): `SH8601_RST_DELAY=200 ms`, `SH8601_SLPIN_DELAY=120 ms`, `SH8601_SLPOUT_DELAY=120 ms`. `displayOn()` sends `0x29` then `0x11` with the above delays.

Vendor ESP-IDF start order (D5, `bsp_display_start_with_config`):
`lvgl_port_init` → `bsp_display_lcd_init` (→ `bsp_display_new`: SPI bus → panel IO → `esp_lcd_new_panel_sh8601` → reset → init → disp_on) → `bsp_display_indev_init` → `bsp_display_brightness_init` (100%).

## 5. PMU / expander power-rail prerequisite (D2, D7, AGENTS.md §4.1)

- D2 proves the expander must be brought up **first** (I2C → `expander.begin(0x20)`), then pins 0/1/2 asserted `LOW → 20 ms → HIGH` to enable the panel/touch power rails. Without this the QSPI panel never responds.
- D7 (`port_axp2101.cpp`) shows the AXP2101 PMU reference init: `PMU.begin(AXP2101_SLAVE_ADDRESS, pmu_register_read, pmu_register_write_byte)` with readiness logging, followed by rail configuration (DCDC/ALDO/BLDO enable + voltage setters). `XPOWERS_CHIP_AXP2101` is declared in D3.

**CsAW mandated composed order (AGENTS.md §4.1 + operator directive):** each stage completes reset, register setup and readiness check before the next:

1. **IO Expander** — I2C init, TCA9554/XCA9554 probe at `0x20`, configure pins 0–2 as outputs, `LOW → 20 ms → HIGH` rail enable, verify.
2. **PMU** — AXP2101 `PMU.begin()`, verify power rails (including those feeding the expander/display) are enabled and at target voltage.
3. **SH8601 AMOLED** — QSPI bus init, panel IO, vendor init table with delays, RGB order/16 bpp, display on.
4. **Brightness gate** — `0x51` with value `>= 77` (>= 30%) immediately, before any LVGL/UI draw; assert in logs.
5. Touch (FT3168) and remaining peripherals (RTC, IMU, audio, SDMMC) after display readiness.

The vendor BSP does **not** call `bsp_io_expander_init()` from `bsp_display_start()`; CsAW must call stages 1–3 explicitly rather than relying on that helper.

## 6. IDF 6.1 adaptation notes (operator directive B2)

- Use Warroom `env-6.1.sh` (`ESP-IDF v6.1`); do not use the vendor example's `idf: ">=5.5,<6.1"` manifest.
- The Rev 1 BSP component itself declares `idf: '>=5.3'`, so the driver is the correct base. Any component that fails against 6.1 is to be modified/recreated in the target repo and recorded here; the intended outcome is a republished, definitive CsAW BSP for this board on IDF 6.1+.
- `14_lvgl_demo_v9/main.c` already guards `esp_log_level_get("i2c.master")` with `ESP_IDF_VERSION < 6.0.0` — evidence that the vendor examples anticipate IDF 6 API changes. Preserve such guards when adapting.
