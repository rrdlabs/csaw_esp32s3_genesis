# CsAW Hardware Bringup — ESP32-S3-TOUCH-AMOLED-1.8 Rev 1 (ESP-IDF 6.1)

Implementation: `components/csaw_hal/csaw_hal.{c,h}`. Exact vendor demo extraction: [DISPLAY_BRINGUP.md](DISPLAY_BRINGUP.md). Source evidence: [HARDWARE_SOURCES.md](HARDWARE_SOURCES.md). This component **recreates** the vendor BSP bringup with native ESP-IDF 6.1 APIs per operator directive (BSP allows `idf >=5.3`; vendor example's `<6.1` manifest is non-binding).

## 1. Init ordering (AGENTS.md §4.1 — literal, no collapse/reorder)

`app_main()` in `main/csaw.c` calls `csaw_hal_init()` **before** NVS/SPIFFS/app services:

1. `csaw_hal_i2c_init()` — I2C0 bus (SDA 15, SCL 14, 400 kHz, internal pullups, glitch filter 7).
2. **IO Expander** `csaw_hal_io_expander_init()` — probe TCA9554/XCA9554 @ `0x20`; write config reg `0x03 = 0xF8` (pins 0–2 outputs); write output reg `0x01 = 0x00` (LOW); **20 ms**; write `0x01 = 0x07` (HIGH); read back and verify bits 0–2. Release only on success.
3. **PMU** `csaw_hal_pmu_init()` — probe AXP2101 @ `0x34`; read IC_TYPE reg `0x03`, require `0x4A`; clear bit 1 of ADC_CHANNEL_CTRL `0x30` (disable NTC/TS measurement, matching vendor XPowersLib `initImpl()`). Battery policy is then applied before the display stage: enable battery detection (`0x68[0]`), fuel gauge and RTC backup-cell charge (`0x18[3:2]`), set backup cell to 3000 mV (`0x6A[2:0]=4`), set low-battery warning to 15% (`0x1A[7:4]=15`) and pulse the fuel-gauge reset (`0x17[3]`). See [POWER_RTC.md](POWER_RTC.md).
4. **SH8601 AMOLED** `csaw_hal_display_init()` — QSPI bus + IO + software reset + vendor register table + display on.
5. **Brightness gate** — `csaw_hal_display_brightness_set(30)` immediately after panel init; register `0x51` raw `77` (`30*255/100`); logs `BRIGHTNESS_GATE OK` and returns failure if < 30%. No UI is drawn before this.
6. Post-display (non-fatal): FT3168 touch, ES8311 audio, PCF85063A RTC, 1-bit SDMMC, QMI8658 IMU. The RTC stage clears Control_1 STOP/12-hour bits, clears Control_2 AIE/AF/TF and sets the timer to 1/60 Hz idle. The IMU stage probes `0x6B` first (vendor schematic/LCDWizard) and falls back to `0x6A`; wake-on-motion is programmed later by the power service. No QMI INT GPIO is guessed — see [POWER_RTC.md](POWER_RTC.md).

Each of stages 2–4 returns `ESP_ERR_INVALID_STATE` if its predecessor did not complete. Stage status is logged in the final `Bringup complete:` line.

## 2. Pin map / bus settings

| Signal | GPIO | Setting |
| --- | --- | --- |
| QSPI PCLK | 11 | SPI2_HOST, 40 MHz, mode 0, DMA auto |
| QSPI CS | 12 | active low |
| QSPI DATA0–3 | 4, 5, 6, 7 | `SPICOMMON_BUSFLAG_QUAD`, `flags.quad_mode = 1` |
| Panel IO | — | `lcd_cmd_bits = 8`, `lcd_param_bits = 8`, `dc_gpio_num = -1` |
| Panel size/format | 368×448, 16 bpp | `LCD_RGB_ELEMENT_ORDER_RGB` (same as vendor MADCTL RGB) |
| Hardware reset | **NC** | software reset `0x01` + 200 ms (vendor `SH8601_RST_DELAY`) |
| I2C SCL/SDA | 14 / 15 | I2C0, 400 kHz |
| Touch INT | 21 | FT3168 @ `0x38`; active-low light-sleep wake source |
| Audio | MCLK 16, BCLK 9, WS 45, DO 10, DI 8, PA 46 | I2S0 std 48 kHz, 16-bit stereo |
| SDMMC | CLK 2, CMD 1, D0 3 | 1-bit, `/sd`, FAT, 8 files max |
| RTC / IMU / Codec | `0x51` / `0x6B` (fallback `0x6A`) / `0x18` | I2C probes; QMI INT-to-ESP GPIO not established |

## 3. SH8601 register writes (in exact order, BSP v1.1.4 table)

| Cmd | Payload | Delay | Meaning |
| --- | --- | --- | --- |
| `0x01` | — | 200 ms | software reset (RST NC) |
| `0x11` | — | 120 ms | sleep out |
| `0x44` | `01 D1` | — | tearing scan line |
| `0x35` | `00` | — | tearing on |
| `0x53` | `20` | 10 ms | display ctrl 1 |
| `0x2A` | `00 00 01 6F` | — | column 0..367 |
| `0x2B` | `00 00 01 BF` | — | page 0..447 |
| `0x51` | `00` | 10 ms | brightness dark during init |
| `0x29` | — | 10 ms | display on |
| `0x51` | `FF` | — | vendor full brightness (overridden by CsAW gate to `4D` = 30%) |

Brightness uses the same register `0x51` as vendor `bsp_display_brightness_set()`: `raw = percent * 255 / 100`. The CsAW init path sets 30% (`0x4D`); UI may raise it later.

## 4. IDF 6.1 adaptations (recreated components)

- `SH8601_PANEL_BUS_QSPI_CONFIG` / `SH8601_PANEL_IO_QSPI_CONFIG` vendor macros recreated inline with native `spi_bus_config_t.data{0..3}_io_num`, `SPICOMMON_BUSFLAG_QUAD`, and `esp_lcd_panel_io_spi_config_t.flags.quad_mode`.
- Vendor `esp_lcd_new_panel_sh8601()` + `esp_lcd_panel_*` wrappers replaced by direct `esp_lcd_panel_io_tx_param()` sequence above (no external registry dependency at build time).
- `main/idf_component.yml` idf constraint changed from `>=5.5.0,<5.6.0` to `>=6.1`; new `components/csaw_hal/idf_component.yml` declares `idf >=6.1`.
- Every adaptation is recorded here for the planned definitive CsAW BSP republication.

## 5. References

- https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8 (commit `01fd3200…`) demos `01_HelloWorld`, `04_GFX_FT3168_Image`, `00_bsp_quickstart`, `14_lvgl_demo_v9`, `90_axp2101_pmu`
- https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8
- Waveshare `XPowersLib/src/REG/AXP2101Constants.h`, `XPowersAXP2101.tpp`, `SensorPCF85063.hpp`, `SensorQMI8658.hpp`, `pin_config.h`, `QMI8658_WakeOnMotion.ino`, `PCF85063_AlarmByUnits.ino` (retrieved verbatim during agent-preflight; see `docs/HARDWARE_SOURCES.md`)
- NXP PCF85063A datasheet Rev. 7 (alarm/Timer_mode registers)
- Espressif ESP-IDF 6.1 `esp_lcd_io_spi.h`, `i2c_master.h`, `spi_common.h`, `sdmmc_host.h`, `i2s_std.h`, `esp_sleep.h`
