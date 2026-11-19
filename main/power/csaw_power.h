/*
 * CsAW power / RTC / low-power service (AGENTS.md 8).
 *
 * Owns the AXP2101 battery policy created in the HAL, the PCF85063A alarm and
 * countdown timer, energy-save transitions, wake-source bookkeeping and the
 * staged wake-word -> agent-loop bridge. High-churn battery polling stays in
 * RAM; only state changes are written to the SD log via csaw_storage_log().
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CSAW_POWER_ACTIVE = 0,
    CSAW_POWER_SAVE,
    CSAW_POWER_SLEEP,
    CSAW_POWER_CRITICAL,
} csaw_power_mode_t;

typedef enum {
    CSAW_WAKE_NONE = 0,
    CSAW_WAKE_TOUCH,
    CSAW_WAKE_SHAKE,
    CSAW_WAKE_RTC_ALARM,
    CSAW_WAKE_RTC_TIMER,
    CSAW_WAKE_WORD,
    CSAW_WAKE_CLI,
} csaw_power_wake_reason_t;

typedef struct {
    uint8_t percent;
    uint16_t voltage_mv;
    bool charging;
    bool discharging;
    bool battery_present;
    bool backup_charging;
    csaw_power_mode_t mode;
    csaw_power_wake_reason_t last_wake;
    bool manual_mode;
    bool wom_enabled;
    bool light_sleep_supported;
    uint32_t light_sleep_cycles;
    char rtc_alarm[80];
} csaw_power_status_t;

/** Initialize PMU/RTC/IMU policies and start the power task. Call once after
 *  the HAL, message bus and cron service are initialized. */
esp_err_t csaw_power_init(void);

/** Mark user/agent activity so energy-save modes can idle correctly. */
void csaw_power_notify_activity(void);

/** Select an energy mode. Manual selection persists until the next call. */
esp_err_t csaw_power_set_mode(csaw_power_mode_t mode);

/** Read the current RAM-only power snapshot. */
esp_err_t csaw_power_get_status(csaw_power_status_t *status);

/** Human-readable mode / wake-reason names. */
const char *csaw_power_mode_name(csaw_power_mode_t mode);
const char *csaw_power_wake_name(csaw_power_wake_reason_t reason);

/** Format a compact one-line status for the serial CLI or on-screen UI. */
esp_err_t csaw_power_format_status(char *buf, size_t len);

/** Arm the PCF85063A alarm for a cron job's next_run timestamp. */
esp_err_t csaw_power_rtc_set_alarm(time_t when);
esp_err_t csaw_power_rtc_clear_alarm(void);

/**
 * Create a cron AT job and arm the PCF85063A countdown timer. Timers up to
 * 255 s use the 1 Hz clock; 60..15300 s use the 1/60 Hz clock. Longer spans
 * are scheduled in cron without a hardware countdown and use the RTC alarm.
 */
esp_err_t csaw_power_schedule_timer(uint32_t seconds, const char *message);

/**
 * Staged wake-word hook. The audio front-end is not yet shipped, but this is
 * the wired entry point: it injects the recognized phrase into the same
 * inbound message bus used by the on-screen UI and injects scheduled tasks.
 */
esp_err_t csaw_power_notify_wake_word(const char *phrase);

#ifdef __cplusplus
}
#endif
