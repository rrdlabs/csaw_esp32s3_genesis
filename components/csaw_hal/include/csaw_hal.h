/*
 * CsAW hardware abstraction layer — ESP32-S3-TOUCH-AMOLED-1.8 Rev 1 (SH8601 + FT3168).
 * Native ESP-IDF 6.1 implementation of the vendor demo bringup sequence.
 * See docs/DISPLAY_BRINGUP.md and docs/HARDWARE_BRINGUP.md.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSAW_LCD_H_RES          368
#define CSAW_LCD_V_RES          448
#define CSAW_LCD_BITS_PER_PIXEL 16
#define CSAW_LCD_BRIGHTNESS_MIN 30
#define CSAW_LCD_BRIGHTNESS_MAX 100

/** Panels are kept dark until this call sets >= CSAW_LCD_BRIGHTNESS_MIN. */
esp_err_t csaw_hal_display_brightness_set(uint8_t percent);
esp_err_t csaw_hal_display_brightness_get(uint8_t *out_percent);

/** Panel IO and panel handles for LVGL integration. May be NULL before init. */
esp_lcd_panel_io_handle_t csaw_hal_display_io(void);
esp_lcd_panel_handle_t csaw_hal_display_panel(void);

/** Creates the native SH8601 panel handle used by the LVGL flush callback. */
esp_err_t csaw_hal_display_panel_create(void);

/** FT3168 single-touch read (vendor FT3x68 registers 0x02..0x06). */
esp_err_t csaw_hal_touch_read(uint16_t *x, uint16_t *y, bool *pressed);

/**
 * Mandated bringup: IO Expander -> PMU -> SH8601 AMOLED, each stage with its
 * own reset/register setup/readiness check, then brightness >= 30%.
 * Remaining peripherals (touch, audio, RTC, SDMMC, IMU) are initialized after
 * the display is ready. Set *brightness_asserted to true when the gate passed.
 */
esp_err_t csaw_hal_init(bool *brightness_asserted);

/** Individual stages (exposed for diagnostics and tests). */
esp_err_t csaw_hal_i2c_init(void);
esp_err_t csaw_hal_io_expander_init(void);
esp_err_t csaw_hal_pmu_init(void);
esp_err_t csaw_hal_display_init(void);
esp_err_t csaw_hal_touch_init(void);
esp_err_t csaw_hal_audio_init(void);
esp_err_t csaw_hal_rtc_init(void);
esp_err_t csaw_hal_sdcard_init(void);
esp_err_t csaw_hal_imu_init(void);

/* ---- PMU / AXP2101 (AGENTS.md 8) --------------------------------------- */

typedef struct {
    uint8_t percent;          /* 0..100 from AXP2101 fuel gauge (0xA4) */
    uint16_t voltage_mv;      /* battery ADC, 0 when no battery is connected */
    bool charging;            /* STATUS2[6:5] == 01 (vendor isCharging()) */
    bool discharging;         /* STATUS2[6:5] == 10 */
    bool battery_present;     /* BAT_DET_CTRL[0] */
    bool gauge_enabled;       /* CHARGE_GAUGE_WDT_CTRL[3] */
    bool backup_charging;     /* CHARGE_GAUGE_WDT_CTRL[2] (RTC/button cell) */
} csaw_hal_battery_status_t;

/** Configure fuel gauge + RTC backup-cell charging; safe to call repeatedly. */
esp_err_t csaw_hal_pmu_configure_battery(bool backup_charging, uint16_t backup_mv);

/** Read live AXP2101 battery/charging state (safe to poll from a task). */
esp_err_t csaw_hal_pmu_battery_status(csaw_hal_battery_status_t *status);

/** Convenience helpers used by the on-screen UI and CLI. */
bool csaw_hal_battery_charging(void);
uint8_t csaw_hal_battery_percent(void);

/* ---- RTC / PCF85063A (AGENTS.md 8) ------------------------------------- */

/** Read the RTC calendar into tm (BCD registers 0x04..0x0A). */
esp_err_t csaw_hal_rtc_get_time(struct tm *out);

/** Set the RTC calendar from tm (BCD registers 0x04..0x0A). */
esp_err_t csaw_hal_rtc_set_time(const struct tm *in);

/**
 * Program the PCF85063A alarm for a wall-clock time. Fields set to
 * CSAW_RTC_ALARM_IGNORE are masked with the vendor NO_ALARM (0x80) value.
 */
#define CSAW_RTC_ALARM_IGNORE 0xFF
esp_err_t csaw_hal_rtc_set_alarm(uint8_t hour, uint8_t minute, uint8_t second,
                                 uint8_t day, uint8_t weekday);
esp_err_t csaw_hal_rtc_clear_alarm(void);
esp_err_t csaw_hal_rtc_alarm_pending(bool *pending);
esp_err_t csaw_hal_rtc_alarm_trace(char *buf, size_t len);

/** Countdown timer: 1..255 s at the PCF85063A 1 Hz clock, or 60..15300 s
 *  at 1/60 Hz. 0 cancels the timer. The flag is polled (INT wiring is not
 *  used); TF is cleared by writing Control_2[3] to 0. */
esp_err_t csaw_hal_rtc_timer_start(uint32_t seconds);
esp_err_t csaw_hal_rtc_timer_cancel(void);
esp_err_t csaw_hal_rtc_timer_pending(bool *pending);

/* ---- IMU / QMI8658 wake-on-motion (AGENTS.md 8) ------------------------ */

/** Configure low-power 128 Hz wake-on-motion (vendor threshold 200 mg). */
esp_err_t csaw_hal_imu_wake_on_motion_enable(bool enable);
/** Read and clear the latched WoM flag in STATUS1[2]. */
esp_err_t csaw_hal_imu_wake_on_motion_pending(bool *pending);

#ifdef __cplusplus
}
#endif
