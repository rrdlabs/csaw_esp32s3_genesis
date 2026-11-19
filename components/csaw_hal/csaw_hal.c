/*
 * CsAW HAL — Rev 1 board (SH8601 + FT3168), native ESP-IDF 6.1.
 *
 * Bringup order is literal (AGENTS.md 4.1): IO Expander -> PMU -> SH8601 AMOLED.
 * Each stage completes reset/register setup/readiness before the next starts.
 * Brightness >= 30% is applied in this init path before any UI is drawn.
 * Sources: docs/DISPLAY_BRINGUP.md, docs/HARDWARE_SOURCES.md.
 */
#include <string.h>
#include <stdlib.h>
#include <sys/unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_bit_defs.h"
#include "esp_heap_caps.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/i2s_std.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_io_spi.h"

#include "csaw_hal.h"

static const char *TAG = "csaw_hal";

/* ---- Rev 1 pin map (vendor pin_config.h / BSP v1.1.4) ---- */
#define CSAW_I2C_PORT           I2C_NUM_0
#define CSAW_I2C_SCL            14
#define CSAW_I2C_SDA            15
#define CSAW_I2C_FREQ_HZ        400000

#define CSAW_LCD_SPI_HOST       SPI2_HOST
#define CSAW_LCD_PCLK           11
#define CSAW_LCD_CS             12
#define CSAW_LCD_DATA0          4
#define CSAW_LCD_DATA1          5
#define CSAW_LCD_DATA2          6
#define CSAW_LCD_DATA3          7
#define CSAW_LCD_PCLK_HZ        (40 * 1000 * 1000)

#define CSAW_EXPANDER_ADDR      0x20    /* TCA9554 / XCA9554 */
#define CSAW_PMU_ADDR           0x34    /* AXP2101 */
#define CSAW_TOUCH_ADDR         0x38    /* FT3168 */
#define CSAW_TOUCH_INT_GPIO     21      /* vendor pin_config.h: TP_INT */
#define CSAW_RTC_ADDR           0x51    /* PCF85063A */
#define CSAW_IMU_ADDR_PRIMARY   0x6B    /* vendor schematic/LCDWizard net */
#define CSAW_IMU_ADDR_FALLBACK  0x6A    /* some module revisions */
#define CSAW_CODEC_ADDR         0x18    /* ES8311 */

/* AXP2101 registers (vendor XPowersLib REG/AXP2101Constants.h) */
#define AXP2101_STATUS1         0x00
#define AXP2101_STATUS2         0x01
#define AXP2101_IC_TYPE         0x03
#define AXP2101_CHIP_ID         0x4A
#define AXP2101_RESET_FUEL_GAUGE 0x17
#define AXP2101_GAUGE_WDT_CTRL  0x18
#define AXP2101_LOW_BAT_WARN    0x1A
#define AXP2101_ADC_CHANNEL_CTRL 0x30
#define AXP2101_ADC_DATA0       0x34    /* ADC_DATA_RELUST0 (vendor) */
#define AXP2101_ADC_DATA1       0x35    /* ADC_DATA_RELUST1 (vendor) */
#define AXP2101_BAT_DET_CTRL    0x68
#define AXP2101_BTN_BAT_CHG_VOL 0x6A
#define AXP2101_BAT_PERCENT     0xA4

/* PCF85063A registers/flags (NXP datasheet + vendor SensorPCF85063.hpp) */
#define PCF_CTRL1               0x00
#define PCF_CTRL2               0x01
#define PCF_SEC                 0x04
#define PCF_ALRM_SEC            0x0B
#define PCF_TIMER_VAL           0x10
#define PCF_TIMER_MODE          0x11
#define PCF_CTRL2_AIE           BIT(7)
#define PCF_CTRL2_AF            BIT(6)
#define PCF_CTRL2_TF            BIT(3)
#define PCF_ALARM_DISABLE       0x80
#define PCF_TIMER_1HZ_1S        (0x02 << 3)  /* TCF=10, TE cleared */
#define PCF_TIMER_1_60HZ        (0x03 << 3)  /* TCF=11, TE cleared */
#define PCF_TIMER_TE            BIT(2)

/* QMI8658 registers/flags (vendor SensorQMI8658.hpp) */
#define QMI_WHOAMI              0x00
#define QMI_CTRL1               0x02
#define QMI_CTRL2               0x03
#define QMI_CTRL7               0x08
#define QMI_CTRL9               0x0A
#define QMI_CAL1_L              0x0B
#define QMI_CAL1_H              0x0C
#define QMI_RESET               0x60
#define QMI_RST_RESULT          0x4D
#define QMI_STATUS_INT          0x2D
#define QMI_STATUS1             0x2F
#define QMI_WHOAMI_VALUE        0x05
#define QMI_RESET_VALUE         0xB0
#define QMI_RESET_RESULT        0x80
#define QMI_CMD_WRITE_WOM       0x08
#define QMI_WOM_THRESHOLD_MG    200
#define QMI_WOM_BLANKING        0x20
#define QMI_STATUS1_WOM         BIT(2)
#define QMI_STATUS_INT_CMD_DONE BIT(7)
#define QMI_CTRL1_INT2_EN       BIT(4)
#define QMI_CTRL1_ADDR_AI       BIT(6)
#define QMI_CTRL7_ACC_EN        BIT(0)

/* TCA9554 registers */
#define TCA9554_REG_INPUT       0x00
#define TCA9554_REG_OUTPUT      0x01
#define TCA9554_REG_CONFIG      0x03

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_expander = NULL;
static i2c_master_dev_handle_t s_pmu = NULL;
static i2c_master_dev_handle_t s_touch = NULL;
static i2c_master_dev_handle_t s_codec = NULL;
static i2c_master_dev_handle_t s_rtc = NULL;
static i2c_master_dev_handle_t s_imu = NULL;
static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;
static i2s_chan_handle_t s_i2s_tx = NULL;
static sdmmc_card_t *s_sd_card = NULL;
static uint8_t s_brightness_percent = 0;
static bool s_stage_expander = false;
static bool s_stage_pmu = false;
static bool s_stage_display = false;
static bool s_stage_touch = false;
static bool s_stage_sdcard = false;
static bool s_stage_rtc = false;
static bool s_stage_imu = false;
static uint8_t s_imu_addr = CSAW_IMU_ADDR_PRIMARY;
static bool s_rtc_alarm_armed = false;
static bool s_rtc_timer_armed = false;

/* ------------------------------ I2C helpers ------------------------------ */

static esp_err_t add_i2c_dev(uint16_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = CSAW_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &cfg, out);
}

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, sizeof(buf), 200);
}

static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, 200);
}

/** Read-modify-write helper used for vendor register bit programming. */
static esp_err_t reg_update(i2c_master_dev_handle_t dev, uint8_t reg,
                            uint8_t mask, uint8_t value)
{
    uint8_t cur = 0;
    esp_err_t err = reg_read(dev, reg, &cur);
    if (err != ESP_OK) return err;
    return reg_write(dev, reg, (uint8_t)((cur & (uint8_t)~mask) | (value & mask)));
}

static uint8_t bin2bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static uint8_t bcd2bin(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

esp_err_t csaw_hal_i2c_init(void)
{
    if (s_i2c_bus) return ESP_OK;
    i2c_master_bus_config_t cfg = {
        .i2c_port = CSAW_I2C_PORT,
        .sda_io_num = CSAW_I2C_SDA,
        .scl_io_num = CSAW_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "I2C0 ready (SDA=%d SCL=%d, %d Hz)", CSAW_I2C_SDA, CSAW_I2C_SCL, CSAW_I2C_FREQ_HZ);
    return ESP_OK;
}

/* ------------------------ Stage 1: IO Expander -------------------------- */

esp_err_t csaw_hal_io_expander_init(void)
{
    esp_err_t err = csaw_hal_i2c_init();
    if (err != ESP_OK) return err;

    err = i2c_master_probe(s_i2c_bus, CSAW_EXPANDER_ADDR, 200);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IO expander not found at 0x%02x: %s", CSAW_EXPANDER_ADDR, esp_err_to_name(err));
        return err;
    }
    if (!s_expander) {
        err = add_i2c_dev(CSAW_EXPANDER_ADDR, &s_expander);
        if (err != ESP_OK) return err;
    }

    /* Vendor demo 04_GFX_FT3168_Image: pins 0,1,2 outputs LOW -> 20 ms -> HIGH.
     * Config register 0x03: 0 = output. Output register 0x01: initial LOW. */
    err = reg_write(s_expander, TCA9554_REG_CONFIG, 0xF8);
    if (err != ESP_OK) return err;
    err = reg_write(s_expander, TCA9554_REG_OUTPUT, 0x00);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    err = reg_write(s_expander, TCA9554_REG_OUTPUT, 0x07);
    if (err != ESP_OK) return err;

    uint8_t out = 0;
    err = reg_read(s_expander, TCA9554_REG_OUTPUT, &out);
    if (err != ESP_OK || (out & 0x07) != 0x07) {
        ESP_LOGE(TAG, "Expander rail readiness check failed (out=0x%02x, err=%s)", out, esp_err_to_name(err));
        return err == ESP_OK ? ESP_FAIL : err;
    }
    s_stage_expander = true;
    ESP_LOGI(TAG, "STAGE 1 OK: IO expander 0x%02x, pins 0-2 rails enabled (out=0x%02x)", CSAW_EXPANDER_ADDR, out);
    return ESP_OK;
}

/* ---------------------------- Stage 2: PMU ------------------------------ */

esp_err_t csaw_hal_pmu_init(void)
{
    if (!s_stage_expander) {
        ESP_LOGE(TAG, "PMU init requires IO expander stage first");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_probe(s_i2c_bus, CSAW_PMU_ADDR, 200);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 PMU not found at 0x%02x: %s", CSAW_PMU_ADDR, esp_err_to_name(err));
        return err;
    }
    if (!s_pmu) {
        err = add_i2c_dev(CSAW_PMU_ADDR, &s_pmu);
        if (err != ESP_OK) return err;
    }

    uint8_t chip_id = 0;
    err = reg_read(s_pmu, AXP2101_IC_TYPE, &chip_id);
    if (err != ESP_OK) return err;
    if (chip_id != AXP2101_CHIP_ID) {
        ESP_LOGE(TAG, "AXP2101 chip ID mismatch: got 0x%02x, expected 0x%02x", chip_id, AXP2101_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }

    /* Vendor XPowersLib initImpl(): disable NTC/TS pin measurement (clear bit 1). */
    uint8_t adc_ctrl = 0;
    err = reg_read(s_pmu, AXP2101_ADC_CHANNEL_CTRL, &adc_ctrl);
    if (err != ESP_OK) return err;
    err = reg_write(s_pmu, AXP2101_ADC_CHANNEL_CTRL, (uint8_t)(adc_ctrl & ~BIT(1)));
    if (err != ESP_OK) return err;

    s_stage_pmu = true;

    /* Configure the main pack fuel gauge and the RTC backup-cell charger as
     * part of the PMU stage, before display/UI power-up (AGENTS.md 8). */
    err = csaw_hal_pmu_configure_battery(true, 3000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 battery policy setup failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "STAGE 2 OK: AXP2101 PMU id=0x%02x, rails and charging verified", chip_id);
    return ESP_OK;
}

esp_err_t csaw_hal_pmu_configure_battery(bool backup_charging, uint16_t backup_mv)
{
    if (!s_stage_pmu || !s_pmu) return ESP_ERR_INVALID_STATE;

    /* Vendor XPowersLib getBatteryPercent()/getBattVoltage() prerequisites. */
    esp_err_t err = reg_update(s_pmu, AXP2101_BAT_DET_CTRL, BIT(0), BIT(0));
    if (err != ESP_OK) return err;

    err = reg_update(s_pmu, AXP2101_GAUGE_WDT_CTRL,
                     BIT(3) | BIT(2),
                     BIT(3) | (backup_charging ? BIT(2) : 0));
    if (err != ESP_OK) return err;

    /* BTN_BAT_CHG_VOL_SET[2:0]: 2600 mV + 100 mV per step (vendor driver). */
    if (backup_charging) {
        if (backup_mv < 2600) backup_mv = 2600;
        if (backup_mv > 3300) backup_mv = 3300;
        uint8_t step = (uint8_t)((backup_mv - 2600) / 100);
        err = reg_update(s_pmu, AXP2101_BTN_BAT_CHG_VOL, 0x07, step);
        if (err != ESP_OK) return err;
    }

    /* LOW_BAT_WARN_SET[7:4]: 5-20% in 1% steps. */
    err = reg_update(s_pmu, AXP2101_LOW_BAT_WARN, 0xF0, (uint8_t)(15 << 4));
    if (err != ESP_OK) return err;

    /* Reset the fuel gauge once so percentage starts from a known state. */
    err = reg_update(s_pmu, AXP2101_RESET_FUEL_GAUGE, BIT(3), BIT(3));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    err = reg_update(s_pmu, AXP2101_RESET_FUEL_GAUGE, BIT(3), 0);
    if (err != ESP_OK) return err;

    uint8_t ctl = 0, btn = 0;
    reg_read(s_pmu, AXP2101_GAUGE_WDT_CTRL, &ctl);
    reg_read(s_pmu, AXP2101_BTN_BAT_CHG_VOL, &btn);
    ESP_LOGI(TAG, "AXP2101 battery policy: gauge=%d backup_charge=%d backup_mv=%u (ctl=0x%02x btn=0x%02x)",
             (ctl & BIT(3)) ? 1 : 0, (ctl & BIT(2)) ? 1 : 0,
             (unsigned)(2600 + (btn & 0x07) * 100), ctl, btn);
    return ESP_OK;
}

esp_err_t csaw_hal_pmu_battery_status(csaw_hal_battery_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (!s_stage_pmu || !s_pmu) return ESP_ERR_INVALID_STATE;

    uint8_t det = 0, status2 = 0, ctl = 0, percent = 0, adc0 = 0, adc1 = 0;
    esp_err_t err = reg_read(s_pmu, AXP2101_BAT_DET_CTRL, &det);
    if (err != ESP_OK) return err;
    err = reg_read(s_pmu, AXP2101_STATUS2, &status2);
    if (err != ESP_OK) return err;
    err = reg_read(s_pmu, AXP2101_GAUGE_WDT_CTRL, &ctl);
    if (err != ESP_OK) return err;
    err = reg_read(s_pmu, AXP2101_ADC_DATA0, &adc0);
    if (err != ESP_OK) return err;
    err = reg_read(s_pmu, AXP2101_ADC_DATA1, &adc1);
    if (err != ESP_OK) return err;

    status->battery_present = (det & BIT(0)) != 0;
    status->gauge_enabled = (ctl & BIT(3)) != 0;
    status->backup_charging = (ctl & BIT(2)) != 0;
    status->charging = ((status2 >> 5) & 0x03) == 0x01;
    status->discharging = ((status2 >> 5) & 0x03) == 0x02;

    /* H5L8 layout used by XPowersLib readRegisterH5L8(0x34, 0x35). */
    status->voltage_mv = (uint16_t)(((adc1 & 0x1F) << 8) | adc0);

    if (status->battery_present) {
        err = reg_read(s_pmu, AXP2101_BAT_PERCENT, &percent);
        if (err != ESP_OK) return err;
        status->percent = percent > 100 ? 100 : percent;
    }
    return ESP_OK;
}

bool csaw_hal_battery_charging(void)
{
    csaw_hal_battery_status_t status;
    if (csaw_hal_pmu_battery_status(&status) != ESP_OK) return false;
    return status.charging;
}

uint8_t csaw_hal_battery_percent(void)
{
    csaw_hal_battery_status_t status;
    if (csaw_hal_pmu_battery_status(&status) != ESP_OK) return 0;
    return status.percent;
}

/* --------------------- Stage 3: SH8601 AMOLED --------------------------- */

typedef struct {
    uint8_t cmd;
    uint8_t param[4];
    size_t len;
    uint32_t delay_ms;
} csaw_lcd_cmd_t;

/* Exact Rev 1 vendor table (BSP v1.1.4 lcd_init_cmds, DISPLAY_BRINGUP.md 4). */
static const csaw_lcd_cmd_t s_sh8601_init[] = {
    { 0x11, { 0x00 }, 0, 120 },
    { 0x44, { 0x01, 0xD1 }, 2, 0 },
    { 0x35, { 0x00 }, 1, 0 },
    { 0x53, { 0x20 }, 1, 10 },
    { 0x2A, { 0x00, 0x00, 0x01, 0x6F }, 4, 0 },
    { 0x2B, { 0x00, 0x00, 0x01, 0xBF }, 4, 0 },
    { 0x51, { 0x00 }, 1, 10 },
    { 0x29, { 0x00 }, 0, 10 },
    { 0x51, { 0xFF }, 1, 0 },
};

esp_err_t csaw_hal_display_brightness_set(uint8_t percent)
{
    if (!s_lcd_io) return ESP_ERR_INVALID_STATE;
    if (percent > CSAW_LCD_BRIGHTNESS_MAX) percent = CSAW_LCD_BRIGHTNESS_MAX;
    uint8_t raw = (uint8_t)(percent * 255 / 100);
    esp_err_t err = esp_lcd_panel_io_tx_param(s_lcd_io, 0x51, &raw, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SH8601 brightness write failed: %s", esp_err_to_name(err));
        return err;
    }
    s_brightness_percent = percent;
    ESP_LOGI(TAG, "SH8601 brightness=%u%% (raw=%u, cmd 0x51)", percent, raw);
    return ESP_OK;
}

esp_err_t csaw_hal_display_brightness_get(uint8_t *out_percent)
{
    if (!out_percent) return ESP_ERR_INVALID_ARG;
    *out_percent = s_brightness_percent;
    return ESP_OK;
}

esp_lcd_panel_io_handle_t csaw_hal_display_io(void)
{
    return s_lcd_io;
}

esp_err_t csaw_hal_display_init(void)
{
    if (!s_stage_pmu) {
        ESP_LOGE(TAG, "Display init requires PMU stage first");
        return ESP_ERR_INVALID_STATE;
    }

    spi_bus_config_t buscfg = {
        .data0_io_num = CSAW_LCD_DATA0,
        .data1_io_num = CSAW_LCD_DATA1,
        .sclk_io_num = CSAW_LCD_PCLK,
        .data2_io_num = CSAW_LCD_DATA2,
        .data3_io_num = CSAW_LCD_DATA3,
        .max_transfer_sz = CSAW_LCD_H_RES * CSAW_LCD_V_RES * CSAW_LCD_BITS_PER_PIXEL / 8,
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    esp_err_t err = spi_bus_initialize(CSAW_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "QSPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Recreated SH8601_PANEL_IO_QSPI_CONFIG with native IDF 6.1 fields.
     * 8-bit command/parameter, quad data phase, no D/C line, mode 0. */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = CSAW_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = CSAW_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.quad_mode = 1,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)CSAW_LCD_SPI_HOST, &io_cfg, &s_lcd_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SH8601 panel IO init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Software reset: hardware RST is not connected on Rev 1. */
    uint8_t dummy = 0;
    err = esp_lcd_panel_io_tx_param(s_lcd_io, 0x01, &dummy, 0);   /* SWRESET */
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(200));

    for (size_t i = 0; i < sizeof(s_sh8601_init) / sizeof(s_sh8601_init[0]); i++) {
        const csaw_lcd_cmd_t *c = &s_sh8601_init[i];
        err = esp_lcd_panel_io_tx_param(s_lcd_io, c->cmd, c->len ? c->param : NULL, c->len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SH8601 init cmd 0x%02x failed: %s", c->cmd, esp_err_to_name(err));
            return err;
        }
        if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }

    s_stage_display = true;
    ESP_LOGI(TAG, "STAGE 3 OK: SH8601 QSPI %ux%u RGB565, RGB element order",
             CSAW_LCD_H_RES, CSAW_LCD_V_RES);

    /* Mandatory brightness gate — before any UI is drawn (AGENTS.md 4.2). */
    err = csaw_hal_display_brightness_set(CSAW_LCD_BRIGHTNESS_MIN);
    if (err != ESP_OK) return err;
    if (s_brightness_percent < CSAW_LCD_BRIGHTNESS_MIN) {
        ESP_LOGE(TAG, "BRIGHTNESS_GATE FAILED: %u%% < %u%%", s_brightness_percent, CSAW_LCD_BRIGHTNESS_MIN);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BRIGHTNESS_GATE OK: %u%% asserted before UI draw", s_brightness_percent);

    err = csaw_hal_display_panel_create();
    if (err != ESP_OK) return err;
    return ESP_OK;
}

/* ---------- Native SH8601 panel handle for the LVGL flush path ---------- */

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int x_gap;
    int y_gap;
    bool mirror_x;
    bool mirror_y;
    bool swap_xy;
} csaw_sh8601_panel_t;

static esp_err_t csaw_panel_reset(esp_lcd_panel_t *panel)
{
    (void)panel;
    /* Reset/register table was already completed in csaw_hal_display_init(). */
    return ESP_OK;
}

static esp_err_t csaw_panel_init(esp_lcd_panel_t *panel)
{
    (void)panel;
    return ESP_OK;
}

static esp_err_t csaw_panel_del(esp_lcd_panel_t *panel)
{
    free(panel);
    return ESP_OK;
}

static esp_err_t csaw_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start,
                                        int x_end, int y_end, const void *color_data)
{
    csaw_sh8601_panel_t *p = (csaw_sh8601_panel_t *)panel;
    if (!p || !color_data || x_end <= x_start || y_end <= y_start) return ESP_ERR_INVALID_ARG;

    int x1 = x_start + p->x_gap;
    int x2 = x_end - 1 + p->x_gap;
    int y1 = y_start + p->y_gap;
    int y2 = y_end - 1 + p->y_gap;
    if (p->swap_xy) {
        int t = x1; x1 = y1; y1 = t;
        t = x2; x2 = y2; y2 = t;
    }
    if (p->mirror_x) { int t = x1; x1 = x2; x2 = t; }
    if (p->mirror_y) { int t = y1; y1 = y2; y2 = t; }

    uint8_t param[4];
    param[0] = (uint8_t)(x1 >> 8); param[1] = (uint8_t)(x1 & 0xFF);
    param[2] = (uint8_t)(x2 >> 8); param[3] = (uint8_t)(x2 & 0xFF);
    esp_err_t err = esp_lcd_panel_io_tx_param(p->io, 0x2A, param, sizeof(param));
    if (err != ESP_OK) return err;
    param[0] = (uint8_t)(y1 >> 8); param[1] = (uint8_t)(y1 & 0xFF);
    param[2] = (uint8_t)(y2 >> 8); param[3] = (uint8_t)(y2 & 0xFF);
    err = esp_lcd_panel_io_tx_param(p->io, 0x2B, param, sizeof(param));
    if (err != ESP_OK) return err;

    size_t pixels = (size_t)(x_end - x_start) * (size_t)(y_end - y_start);
    return esp_lcd_panel_io_tx_color(p->io, 0x2C, color_data, pixels * 2);
}

static esp_err_t csaw_panel_draw_bitmap_2d(esp_lcd_panel_t *panel, int x_start, int y_start,
                                           int x_end, int y_end, const void *src_data,
                                           size_t src_x_size, size_t src_y_size,
                                           int src_x_start, int src_y_start,
                                           int src_x_end, int src_y_end)
{
    (void)panel; (void)x_start; (void)y_start; (void)x_end; (void)y_end;
    (void)src_data; (void)src_x_size; (void)src_y_size;
    (void)src_x_start; (void)src_y_start; (void)src_x_end; (void)src_y_end;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t csaw_panel_mirror(esp_lcd_panel_t *panel, bool x_axis, bool y_axis)
{
    csaw_sh8601_panel_t *p = (csaw_sh8601_panel_t *)panel;
    p->mirror_x = x_axis;
    p->mirror_y = y_axis;
    return ESP_OK;
}

static esp_err_t csaw_panel_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    csaw_sh8601_panel_t *p = (csaw_sh8601_panel_t *)panel;
    p->swap_xy = swap_axes;
    return ESP_OK;
}

static esp_err_t csaw_panel_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    csaw_sh8601_panel_t *p = (csaw_sh8601_panel_t *)panel;
    p->x_gap = x_gap;
    p->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t csaw_panel_invert_color(esp_lcd_panel_t *panel, bool invert)
{
    csaw_sh8601_panel_t *p = (csaw_sh8601_panel_t *)panel;
    uint8_t dummy = 0;
    return esp_lcd_panel_io_tx_param(p->io, invert ? 0x21 : 0x20, &dummy, 0);
}

static esp_err_t csaw_panel_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    csaw_sh8601_panel_t *p = (csaw_sh8601_panel_t *)panel;
    uint8_t dummy = 0;
    return esp_lcd_panel_io_tx_param(p->io, on_off ? 0x29 : 0x28, &dummy, 0);
}

static esp_err_t csaw_panel_disp_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    csaw_sh8601_panel_t *p = (csaw_sh8601_panel_t *)panel;
    uint8_t dummy = 0;
    return esp_lcd_panel_io_tx_param(p->io, sleep ? 0x10 : 0x11, &dummy, 0);
}

static esp_err_t csaw_panel_set_brightness(esp_lcd_panel_t *panel, int brightness)
{
    (void)panel;
    if (brightness < 0) return ESP_ERR_INVALID_ARG;
    uint8_t percent = brightness > 100 ? (uint8_t)(brightness * 100 / 255) : (uint8_t)brightness;
    return csaw_hal_display_brightness_set(percent);
}

esp_err_t csaw_hal_display_panel_create(void)
{
    if (s_lcd_panel) return ESP_OK;
    if (!s_lcd_io) return ESP_ERR_INVALID_STATE;
    csaw_sh8601_panel_t *p = calloc(1, sizeof(csaw_sh8601_panel_t));
    if (!p) return ESP_ERR_NO_MEM;
    p->base.reset = csaw_panel_reset;
    p->base.init = csaw_panel_init;
    p->base.del = csaw_panel_del;
    p->base.draw_bitmap = csaw_panel_draw_bitmap;
    p->base.draw_bitmap_2d = csaw_panel_draw_bitmap_2d;
    p->base.mirror = csaw_panel_mirror;
    p->base.swap_xy = csaw_panel_swap_xy;
    p->base.set_gap = csaw_panel_set_gap;
    p->base.invert_color = csaw_panel_invert_color;
    p->base.disp_on_off = csaw_panel_disp_on_off;
    p->base.disp_sleep = csaw_panel_disp_sleep;
    p->base.set_brightness = csaw_panel_set_brightness;
    p->io = s_lcd_io;
    s_lcd_panel = (esp_lcd_panel_handle_t)&p->base;
    ESP_LOGI(TAG, "SH8601 panel handle ready for LVGL");
    return ESP_OK;
}

esp_lcd_panel_handle_t csaw_hal_display_panel(void)
{
    return s_lcd_panel;
}

/* --------------------- Post-display peripherals ------------------------- */

esp_err_t csaw_hal_touch_init(void)
{
    if (!s_stage_display) return ESP_ERR_INVALID_STATE;
    esp_err_t err = i2c_master_probe(s_i2c_bus, CSAW_TOUCH_ADDR, 200);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT3168 touch not present at 0x%02x: %s", CSAW_TOUCH_ADDR, esp_err_to_name(err));
        return err;
    }
    if (!s_touch) {
        err = add_i2c_dev(CSAW_TOUCH_ADDR, &s_touch);
        if (err != ESP_OK) return err;
    }
    s_stage_touch = true;
    ESP_LOGI(TAG, "FT3168 touch ready at 0x%02x (INT=21)", CSAW_TOUCH_ADDR);
    return ESP_OK;
}

esp_err_t csaw_hal_touch_read(uint16_t *x, uint16_t *y, bool *pressed)
{
    if (!s_touch || !x || !y || !pressed) return ESP_ERR_INVALID_ARG;
    /* Vendor FT3x68 registers: 0x02 finger count, 0x03/04 X, 0x05/06 Y. */
    uint8_t reg = 0x02;
    uint8_t data[5] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_touch, &reg, 1, data, sizeof(data), 100);
    if (err != ESP_OK) return err;
    uint8_t fingers = data[0] & 0x0F;
    *pressed = fingers > 0;
    if (*pressed) {
        *x = (uint16_t)(((data[1] & 0x0F) << 8) | data[2]);
        *y = (uint16_t)(((data[3] & 0x0F) << 8) | data[4]);
    } else {
        *x = 0;
        *y = 0;
    }
    return ESP_OK;
}

esp_err_t csaw_hal_audio_init(void)
{
    esp_err_t err = i2c_master_probe(s_i2c_bus, CSAW_CODEC_ADDR, 200);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 codec not found at 0x%02x: %s", CSAW_CODEC_ADDR, esp_err_to_name(err));
        return err;
    }
    if (!s_codec) {
        err = add_i2c_dev(CSAW_CODEC_ADDR, &s_codec);
        if (err != ESP_OK) return err;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    err = i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL);
    if (err != ESP_OK) return err;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = 16, .bclk = 9, .ws = 45, .dout = 10, .din = 8,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    err = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "ES8311 audio ready (I2S0 48 kHz, MCLK=16 BCLK=9 WS=45 DO=10 DI=8, PA=46)");
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_init(void)
{
    esp_err_t err = i2c_master_probe(s_i2c_bus, CSAW_RTC_ADDR, 200);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063A RTC not found: %s", esp_err_to_name(err));
        return err;
    }
    if (!s_rtc) {
        err = add_i2c_dev(CSAW_RTC_ADDR, &s_rtc);
        if (err != ESP_OK) return err;
    }

    /* Control_1: 24-hour mode, oscillator running. Alarm/Timer flags in
     * Control_2 are cleared without disturbing other bits. */
    err = reg_update(s_rtc, PCF_CTRL1, BIT(1) | BIT(5), 0);
    if (err != ESP_OK) return err;
    err = reg_update(s_rtc, PCF_CTRL2,
                     PCF_CTRL2_AIE | PCF_CTRL2_AF | PCF_CTRL2_TF, 0);
    if (err != ESP_OK) return err;
    err = reg_write(s_rtc, PCF_TIMER_MODE, PCF_TIMER_1_60HZ);
    if (err != ESP_OK) return err;

    s_stage_rtc = true;
    ESP_LOGI(TAG, "PCF85063A RTC ready at 0x%02x (backup cell charged by AXP2101)",
             CSAW_RTC_ADDR);
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_get_time(struct tm *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t b[7] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_rtc, (uint8_t[]){ PCF_SEC }, 1, b, sizeof(b), 200);
    if (err != ESP_OK) return err;

    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd2bin(b[0] & 0x7F);
    out->tm_min = bcd2bin(b[1] & 0x7F);
    out->tm_hour = bcd2bin(b[2] & 0x3F);
    out->tm_mday = bcd2bin(b[3] & 0x3F);
    out->tm_wday = b[4] & 0x07;
    out->tm_mon = (bcd2bin(b[5] & 0x1F) == 0) ? 0 : bcd2bin(b[5] & 0x1F) - 1;
    out->tm_year = bcd2bin(b[6]) + 100;   /* 2000 + year */
    out->tm_isdst = -1;
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_set_time(const struct tm *in)
{
    if (!in) return ESP_ERR_INVALID_ARG;
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t b[7] = {
        bin2bcd((uint8_t)in->tm_sec),
        bin2bcd((uint8_t)in->tm_min),
        bin2bcd((uint8_t)in->tm_hour),
        bin2bcd((uint8_t)in->tm_mday),
        (uint8_t)(in->tm_wday & 0x07),
        bin2bcd((uint8_t)(in->tm_mon + 1)),
        bin2bcd((uint8_t)(in->tm_year - 100)),
    };
    uint8_t buf[8] = { PCF_SEC, b[0], b[1], b[2], b[3], b[4], b[5], b[6] };
    esp_err_t err = i2c_master_transmit(s_rtc, buf, sizeof(buf), 200);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "PCF85063A time set: %04d-%02d-%02d %02d:%02d:%02d",
             in->tm_year + 1900, in->tm_mon + 1, in->tm_mday,
             in->tm_hour, in->tm_min, in->tm_sec);
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_set_alarm(uint8_t hour, uint8_t minute, uint8_t second,
                                 uint8_t day, uint8_t weekday)
{
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t vals[5] = { second, minute, hour, day, weekday };
    uint8_t buf[6] = { PCF_ALRM_SEC, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 5; i++) {
        if (vals[i] == CSAW_RTC_ALARM_IGNORE) {
            buf[i + 1] = PCF_ALARM_DISABLE;
        } else {
            uint8_t max = (i == 0 || i == 1) ? 59 : ((i == 2) ? 23 : 31);
            if (vals[i] > max) vals[i] = max;
            buf[i + 1] = (uint8_t)(bin2bcd(vals[i]) & (uint8_t)~PCF_ALARM_DISABLE);
        }
    }
    esp_err_t err = i2c_master_transmit(s_rtc, buf, sizeof(buf), 200);
    if (err != ESP_OK) return err;

    /* Clear the flag before enabling AIE (Control_2[7]=AIE, [6]=AF). */
    err = reg_update(s_rtc, PCF_CTRL2, PCF_CTRL2_AIE | PCF_CTRL2_AF, PCF_CTRL2_AIE);
    if (err != ESP_OK) return err;
    s_rtc_alarm_armed = true;
    ESP_LOGI(TAG, "PCF85063A alarm armed: %02d:%02d:%02d day=%02d wday=%02d",
             hour, minute, second, day, weekday);
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_clear_alarm(void)
{
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    esp_err_t err = reg_update(s_rtc, PCF_CTRL2,
                               PCF_CTRL2_AIE | PCF_CTRL2_AF, 0);
    if (err != ESP_OK) return err;
    s_rtc_alarm_armed = false;
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_alarm_pending(bool *pending)
{
    if (!pending) return ESP_ERR_INVALID_ARG;
    *pending = false;
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t ctrl2 = 0;
    esp_err_t err = reg_read(s_rtc, PCF_CTRL2, &ctrl2);
    if (err != ESP_OK) return err;
    if ((ctrl2 & PCF_CTRL2_AF) == 0) return ESP_OK;

    /* One-shot semantics: clear AF and AIE once the event is consumed. */
    err = reg_update(s_rtc, PCF_CTRL2, PCF_CTRL2_AIE | PCF_CTRL2_AF, 0);
    if (err != ESP_OK) return err;
    s_rtc_alarm_armed = false;
    *pending = true;
    ESP_LOGI(TAG, "PCF85063A alarm flag consumed");
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_alarm_trace(char *buf, size_t len)
{
    if (!buf || len < 8) return ESP_ERR_INVALID_ARG;
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t b[5] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_rtc, (uint8_t[]){ PCF_ALRM_SEC }, 1, b, sizeof(b), 200);
    if (err != ESP_OK) return err;

    const char *sec = (b[0] & PCF_ALARM_DISABLE) ? "--" : "";
    char secbuf[8] = "--", minbuf[8] = "--", hrbuf[8] = "--";
    char daybuf[8] = "--", wkbuf[8] = "--";
    if (*sec) {
        snprintf(secbuf, sizeof(secbuf), "%02d", bcd2bin(b[0]));
        snprintf(minbuf, sizeof(minbuf), "%02d", bcd2bin(b[1]));
        snprintf(hrbuf, sizeof(hrbuf), "%02d", bcd2bin(b[2]));
        snprintf(daybuf, sizeof(daybuf), "%02d", bcd2bin(b[3]));
        snprintf(wkbuf, sizeof(wkbuf), "%d", b[4] & 0x07);
        sec = secbuf;
    }
    snprintf(buf, len, "alarm=%s:%s:%s day=%s wday=%s armed=%d",
             (b[2] & PCF_ALARM_DISABLE) ? "--" : hrbuf,
             (b[1] & PCF_ALARM_DISABLE) ? "--" : minbuf,
             (b[0] & PCF_ALARM_DISABLE) ? "--" : secbuf,
             (b[3] & PCF_ALARM_DISABLE) ? "--" : daybuf,
             (b[4] & PCF_ALARM_DISABLE) ? "--" : wkbuf,
             s_rtc_alarm_armed ? 1 : 0);
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_timer_start(uint32_t seconds)
{
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    if (seconds == 0) return csaw_hal_rtc_timer_cancel();

    uint8_t mode = PCF_TIMER_1HZ_1S;
    uint16_t value = 0;
    if (seconds <= 255) {
        value = (uint16_t)seconds;
    } else if (seconds >= 60 && seconds <= 15300) {
        mode = PCF_TIMER_1_60HZ;
        value = (uint16_t)(seconds / 60);
        if (value == 0) value = 1;
        if (value > 255) value = 255;
    } else {
        return ESP_ERR_INVALID_ARG;   /* use the alarm for longer intervals */
    }

    /* Disable timer before loading the countdown value (NXP datasheet 8.6.3). */
    esp_err_t err = reg_write(s_rtc, PCF_TIMER_MODE, mode);
    if (err != ESP_OK) return err;
    err = reg_write(s_rtc, PCF_TIMER_VAL, (uint8_t)value);
    if (err != ESP_OK) return err;
    err = reg_update(s_rtc, PCF_CTRL2, PCF_CTRL2_TF, 0);
    if (err != ESP_OK) return err;
    err = reg_write(s_rtc, PCF_TIMER_MODE, (uint8_t)(mode | PCF_TIMER_TE));
    if (err != ESP_OK) return err;

    s_rtc_timer_armed = true;
    ESP_LOGI(TAG, "PCF85063A timer armed: %u s (T=%u, mode=0x%02x)",
             (unsigned)seconds, (unsigned)value, mode | PCF_TIMER_TE);
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_timer_cancel(void)
{
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    esp_err_t err = reg_write(s_rtc, PCF_TIMER_MODE, PCF_TIMER_1_60HZ);
    if (err != ESP_OK) return err;
    err = reg_update(s_rtc, PCF_CTRL2, PCF_CTRL2_TF, 0);
    if (err != ESP_OK) return err;
    s_rtc_timer_armed = false;
    return ESP_OK;
}

esp_err_t csaw_hal_rtc_timer_pending(bool *pending)
{
    if (!pending) return ESP_ERR_INVALID_ARG;
    *pending = false;
    if (!s_stage_rtc || !s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t ctrl2 = 0;
    esp_err_t err = reg_read(s_rtc, PCF_CTRL2, &ctrl2);
    if (err != ESP_OK) return err;
    if ((ctrl2 & PCF_CTRL2_TF) == 0) return ESP_OK;

    err = reg_write(s_rtc, PCF_TIMER_MODE, PCF_TIMER_1_60HZ);
    if (err != ESP_OK) return err;
    err = reg_update(s_rtc, PCF_CTRL2, PCF_CTRL2_TF, 0);
    if (err != ESP_OK) return err;
    s_rtc_timer_armed = false;
    *pending = true;
    ESP_LOGI(TAG, "PCF85063A timer flag consumed");
    return ESP_OK;
}

esp_err_t csaw_hal_sdcard_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = 2;
    slot.cmd = 1;
    slot.d0 = 3;

    esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &mount, &s_sd_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC mount failed: %s", esp_err_to_name(err));
        return err;
    }
    s_stage_sdcard = true;
    ESP_LOGI(TAG, "SDMMC ready at /sd (1-bit, CLK=2 CMD=1 D0=3, %u MiB)",
             (unsigned)(s_sd_card->csd.capacity / 2048));
    return ESP_OK;
}

esp_err_t csaw_hal_imu_init(void)
{
    /* Vendor schematic uses 0x6B; earlier board/module variants used 0x6A.
     * Probe both instead of guessing a single address. */
    s_imu_addr = CSAW_IMU_ADDR_PRIMARY;
    esp_err_t err = i2c_master_probe(s_i2c_bus, s_imu_addr, 200);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 not found at 0x%02x (%s), trying 0x%02x",
                 s_imu_addr, esp_err_to_name(err), CSAW_IMU_ADDR_FALLBACK);
        s_imu_addr = CSAW_IMU_ADDR_FALLBACK;
        err = i2c_master_probe(s_i2c_bus, s_imu_addr, 200);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 IMU not found: %s", esp_err_to_name(err));
        return err;
    }
    if (!s_imu) {
        err = add_i2c_dev(s_imu_addr, &s_imu);
        if (err != ESP_OK) return err;
    }

    uint8_t whoami = 0;
    err = reg_read(s_imu, QMI_WHOAMI, &whoami);
    if (err != ESP_OK) return err;
    if (whoami != QMI_WHOAMI_VALUE) {
        ESP_LOGW(TAG, "QMI8658 WHOAMI mismatch: 0x%02x (expected 0x%02x)",
                 whoami, QMI_WHOAMI_VALUE);
    }
    /* Vendor reset() enables address auto-increment after reset. */
    err = reg_update(s_imu, QMI_CTRL1, QMI_CTRL1_ADDR_AI, QMI_CTRL1_ADDR_AI);
    if (err != ESP_OK) return err;

    s_stage_imu = true;
    ESP_LOGI(TAG, "QMI8658 IMU ready at 0x%02x (WHOAMI=0x%02x)", s_imu_addr, whoami);
    return ESP_OK;
}

static esp_err_t qmi_command(uint8_t cmd)
{
    if (!s_imu) return ESP_ERR_INVALID_STATE;
    esp_err_t err = reg_write(s_imu, QMI_CTRL9, cmd);
    if (err != ESP_OK) return err;
    for (int i = 0; i < 100; i++) {
        uint8_t status = 0;
        err = reg_read(s_imu, QMI_STATUS_INT, &status);
        if (err != ESP_OK) return err;
        if (status & QMI_STATUS_INT_CMD_DONE) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return reg_write(s_imu, QMI_CTRL9, 0x00);   /* CTRL_CMD_ACK */
}

esp_err_t csaw_hal_imu_wake_on_motion_enable(bool enable)
{
    if (!s_stage_imu || !s_imu) return ESP_ERR_INVALID_STATE;
    if (!enable) {
        esp_err_t err = reg_write(s_imu, QMI_CTRL7, 0x00);
        if (err != ESP_OK) return err;
        return reg_update(s_imu, QMI_CTRL1, QMI_CTRL1_INT2_EN, 0);
    }

    /* Vendor SensorQMI8658::configWakeOnMotion(), parameter-for-parameter. */
    esp_err_t err = reg_write(s_imu, QMI_RESET, QMI_RESET_VALUE);
    if (err != ESP_OK) return err;
    bool reset_ok = false;
    for (int i = 0; i < 100; i++) {
        uint8_t result = 0;
        if (reg_read(s_imu, QMI_RST_RESULT, &result) == ESP_OK &&
            result == QMI_RESET_RESULT) {
            reset_ok = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!reset_ok) {
        ESP_LOGE(TAG, "QMI8658 reset did not complete");
        return ESP_ERR_TIMEOUT;
    }
    err = reg_update(s_imu, QMI_CTRL1, QMI_CTRL1_ADDR_AI, QMI_CTRL1_ADDR_AI);
    if (err != ESP_OK) return err;

    err = reg_write(s_imu, QMI_CTRL7, 0x00);
    if (err != ESP_OK) return err;
    err = reg_update(s_imu, QMI_CTRL2, 0x8F, (uint8_t)(2 << 4));  /* ACC_RANGE_8G */
    if (err != ESP_OK) return err;
    err = reg_update(s_imu, QMI_CTRL2, 0xF0, 12);  /* low-power 128 Hz */
    if (err != ESP_OK) return err;
    err = reg_write(s_imu, QMI_CAL1_L, QMI_WOM_THRESHOLD_MG);
    if (err != ESP_OK) return err;
    /* INT2, initial value high, 0x20-sample blanking time (vendor default). */
    err = reg_write(s_imu, QMI_CAL1_H,
                    (uint8_t)((0x03 << 6) | QMI_WOM_BLANKING));
    if (err != ESP_OK) return err;
    err = qmi_command(QMI_CMD_WRITE_WOM);
    if (err != ESP_OK) return err;

    err = reg_update(s_imu, QMI_CTRL7, QMI_CTRL7_ACC_EN, QMI_CTRL7_ACC_EN);
    if (err != ESP_OK) return err;
    err = reg_update(s_imu, QMI_CTRL1, QMI_CTRL1_INT2_EN, QMI_CTRL1_INT2_EN);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "QMI8658 wake-on-motion enabled (threshold=%d mg, ODR=128 Hz)",
             QMI_WOM_THRESHOLD_MG);
    return ESP_OK;
}

esp_err_t csaw_hal_imu_wake_on_motion_pending(bool *pending)
{
    if (!pending) return ESP_ERR_INVALID_ARG;
    *pending = false;
    if (!s_stage_imu || !s_imu) return ESP_ERR_INVALID_STATE;
    uint8_t status1 = 0;
    esp_err_t err = reg_read(s_imu, QMI_STATUS1, &status1);
    if (err != ESP_OK) return err;
    if (status1 & QMI_STATUS1_WOM) {
        *pending = true;
        ESP_LOGI(TAG, "QMI8658 wake-on-motion flag detected (STATUS1=0x%02x)", status1);
    }
    return ESP_OK;
}

esp_err_t csaw_hal_init(bool *brightness_asserted)
{
    if (brightness_asserted) *brightness_asserted = false;

    esp_err_t err;
    err = csaw_hal_io_expander_init();
    if (err != ESP_OK) return err;
    err = csaw_hal_pmu_init();
    if (err != ESP_OK) return err;
    err = csaw_hal_display_init();
    if (err != ESP_OK) return err;

    if (brightness_asserted) *brightness_asserted = (s_brightness_percent >= CSAW_LCD_BRIGHTNESS_MIN);

    /* Non-fatal peripherals: report but do not block the display/UI path. */
    csaw_hal_touch_init();
    csaw_hal_audio_init();
    csaw_hal_rtc_init();
    csaw_hal_sdcard_init();
    csaw_hal_imu_init();

    ESP_LOGI(TAG, "Bringup complete: expander=%d pmu=%d display=%d touch=%d sd=%d",
             s_stage_expander, s_stage_pmu, s_stage_display, s_stage_touch, s_stage_sdcard);
    return ESP_OK;
}
