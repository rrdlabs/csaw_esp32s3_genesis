/*
 * CsAW power / RTC / low-power service (AGENTS.md 8).
 *
 * All high-churn state lives in RAM. Only mode changes, wake events and timer
 * scheduling are written to the SD log through the central storage policy.
 */
#include "power/csaw_power.h"
#include "csaw_config.h"
#include "csaw_hal.h"
#include "bus/message_bus.h"
#include "cron/cron_service.h"
#include "csaw_ui.h"
#include "storage/csaw_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "csaw_power";

#define CSAW_POWER_TASK_STACK       (5 * 1024)
#define CSAW_POWER_TASK_PRIO        4
#define CSAW_POWER_POLL_MS          1000
#define CSAW_POWER_STATUS_UI_MS     15000

static csaw_power_status_t s_status;
static TaskHandle_t s_task = NULL;
static TickType_t s_last_activity = 0;
static TickType_t s_last_ui_update = 0;
static time_t s_last_alarm_refresh = 0;
static bool s_initialized = false;

const char *csaw_power_mode_name(csaw_power_mode_t mode)
{
    switch (mode) {
        case CSAW_POWER_ACTIVE:   return "active";
        case CSAW_POWER_SAVE:     return "save";
        case CSAW_POWER_SLEEP:    return "sleep";
        case CSAW_POWER_CRITICAL: return "critical";
        default:                  return "unknown";
    }
}

const char *csaw_power_wake_name(csaw_power_wake_reason_t reason)
{
    switch (reason) {
        case CSAW_WAKE_TOUCH:      return "touch";
        case CSAW_WAKE_SHAKE:      return "shake";
        case CSAW_WAKE_RTC_ALARM:  return "rtc-alarm";
        case CSAW_WAKE_RTC_TIMER:  return "rtc-timer";
        case CSAW_WAKE_WORD:       return "wake-word";
        case CSAW_WAKE_CLI:        return "cli";
        case CSAW_WAKE_NONE:
        default:                   return "none";
    }
}

void csaw_power_notify_activity(void)
{
    s_last_activity = xTaskGetTickCount();
}

static void power_mark_wake(csaw_power_wake_reason_t reason, const char *detail)
{
    s_status.last_wake = reason;
    s_last_activity = xTaskGetTickCount();
    csaw_storage_log(TAG, "wake reason=%s detail=%s", csaw_power_wake_name(reason),
                     detail ? detail : "-");
}

static esp_err_t power_apply_mode(csaw_power_mode_t mode, bool manual, const char *reason)
{
    uint8_t brightness;
    switch (mode) {
        case CSAW_POWER_ACTIVE:   brightness = 60; break;
        case CSAW_POWER_SAVE:     brightness = 40; break;
        case CSAW_POWER_SLEEP:    brightness = CSAW_LCD_BRIGHTNESS_MIN; break;
        case CSAW_POWER_CRITICAL: brightness = CSAW_LCD_BRIGHTNESS_MIN; break;
        default: return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = csaw_hal_display_brightness_set(brightness);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Brightness adjust for %s failed: %s",
                 csaw_power_mode_name(mode), esp_err_to_name(err));
        return err;
    }

    if (s_status.mode != mode || reason) {
        ESP_LOGI(TAG, "Power mode -> %s (brightness=%u%%, manual=%d, reason=%s)",
                 csaw_power_mode_name(mode), brightness, manual ? 1 : 0,
                 reason ? reason : "auto");
        csaw_storage_log(TAG, "mode=%s brightness=%u manual=%d reason=%s",
                         csaw_power_mode_name(mode), brightness,
                         manual ? 1 : 0, reason ? reason : "auto");
    }
    s_status.mode = mode;
    s_status.manual_mode = manual;
    return ESP_OK;
}

static uint32_t power_idle_timeout_ms(csaw_power_mode_t mode)
{
    switch (mode) {
        case CSAW_POWER_SAVE:     return CSAW_POWER_IDLE_SAVE_MS;
        case CSAW_POWER_SLEEP:    return CSAW_POWER_IDLE_SLEEP_MS;
        case CSAW_POWER_CRITICAL: return CSAW_POWER_IDLE_CRITICAL_MS;
        case CSAW_POWER_ACTIVE:
        default:                  return CSAW_POWER_IDLE_ACTIVE_MS;
    }
}

static void power_configure_touch_wake(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << CSAW_TOUCH_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch wake GPIO config failed: %s", esp_err_to_name(err));
        return;
    }
    err = gpio_wakeup_enable(CSAW_TOUCH_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) {
        esp_sleep_enable_gpio_wakeup();
        ESP_LOGI(TAG, "Wake on touch armed (GPIO%d, active low)", CSAW_TOUCH_INT_GPIO);
    } else {
        ESP_LOGW(TAG, "Touch wake enable failed: %s", esp_err_to_name(err));
    }
}

static void power_sync_rtc_from_system(void)
{
    time_t now = time(NULL);
    if (now < 1700000000) {   /* SNTP/system clock not valid yet */
        ESP_LOGI(TAG, "System time not valid; keeping PCF85063A calendar");
        return;
    }
    struct tm rtc_tm;
    if (csaw_hal_rtc_get_time(&rtc_tm) != ESP_OK) return;
    time_t rtc_epoch = mktime(&rtc_tm);
    if (llabs((long long)rtc_epoch - (long long)now) > 120) {
        struct tm sys_tm;
        localtime_r(&now, &sys_tm);
        if (csaw_hal_rtc_set_time(&sys_tm) == ESP_OK) {
            ESP_LOGI(TAG, "PCF85063A calendar synchronized from system time");
        }
    }
}

static void power_refresh_alarm(void)
{
    time_t next = 0;
    if (cron_service_next_due_time(&next) != ESP_OK || next <= 0) {
        csaw_hal_rtc_clear_alarm();
        return;
    }
    if (next == s_last_alarm_refresh) return;
    if (csaw_power_rtc_set_alarm(next) == ESP_OK) {
        s_last_alarm_refresh = next;
    }
}

static void power_poll_battery(void)
{
    csaw_hal_battery_status_t b;
    if (csaw_hal_pmu_battery_status(&b) != ESP_OK) return;

    s_status.percent = b.percent;
    s_status.voltage_mv = b.voltage_mv;
    s_status.charging = b.charging;
    s_status.discharging = b.discharging;
    s_status.battery_present = b.battery_present;
    s_status.backup_charging = b.backup_charging;

    if (s_status.manual_mode) return;

    csaw_power_mode_t target = CSAW_POWER_ACTIVE;
    const char *reason = NULL;
    if (b.battery_present && !b.charging && b.percent <= CSAW_BATTERY_CRITICAL_PCT) {
        target = CSAW_POWER_CRITICAL;
        reason = "battery-critical";
    } else if (b.battery_present && !b.charging && b.percent <= CSAW_BATTERY_SAVE_PCT) {
        target = CSAW_POWER_SAVE;
        reason = "battery-low";
    }
    if (target != s_status.mode) {
        power_apply_mode(target, false, reason);
    }
}

static void power_poll_wake_sources(void)
{
    bool pending = false;
    if (csaw_hal_rtc_alarm_pending(&pending) == ESP_OK && pending) {
        power_mark_wake(CSAW_WAKE_RTC_ALARM, "PCF85063A alarm");
        cron_service_process_due_now();
        s_last_alarm_refresh = 0;
        power_refresh_alarm();
    }

    pending = false;
    if (csaw_hal_rtc_timer_pending(&pending) == ESP_OK && pending) {
        power_mark_wake(CSAW_WAKE_RTC_TIMER, "PCF85063A countdown");
        cron_service_process_due_now();
        s_last_alarm_refresh = 0;
        power_refresh_alarm();
    }

    pending = false;
    if (csaw_hal_imu_wake_on_motion_pending(&pending) == ESP_OK && pending) {
        power_mark_wake(CSAW_WAKE_SHAKE, "QMI8658 WoM");
    }
}

static void power_maybe_sleep(void)
{
    if (!s_initialized) return;
    if (s_status.mode != CSAW_POWER_SAVE && s_status.mode != CSAW_POWER_SLEEP) return;

    uint32_t idle_ms = (uint32_t)(xTaskGetTickCount() - s_last_activity) *
                       portTICK_PERIOD_MS;
    if (idle_ms < power_idle_timeout_ms(s_status.mode)) return;

    esp_sleep_enable_timer_wakeup(CSAW_POWER_SLEEP_POLL_MS * 1000ULL);
    esp_err_t err = esp_light_sleep_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Light sleep failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(CSAW_POWER_SLEEP_POLL_MS));
        return;
    }

    s_status.light_sleep_cycles++;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        power_mark_wake(CSAW_WAKE_TOUCH, "GPIO21");
    }
    /* Wake-on-shake has no verified ESP interrupt pin on Rev 1. A short timer
     * wake keeps the CPU responsive while the WoM flag is polled by
     * power_poll_wake_sources() on the next loop (HARDWARE_BRINGUP.md). */
}

static void power_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Power task started");
    while (1) {
        power_poll_battery();
        power_poll_wake_sources();
        power_refresh_alarm();

        TickType_t now = xTaskGetTickCount();
        if ((now - s_last_ui_update) * portTICK_PERIOD_MS >= CSAW_POWER_STATUS_UI_MS) {
            char line[128];
            if (csaw_power_format_status(line, sizeof(line)) == ESP_OK &&
                csaw_ui_ready()) {
                csaw_ui_show_status(line);
            }
            s_last_ui_update = now;
        }

        power_maybe_sleep();
        vTaskDelay(pdMS_TO_TICKS(CSAW_POWER_POLL_MS));
    }
}

esp_err_t csaw_power_init(void)
{
    if (s_initialized) return ESP_OK;

    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = CSAW_POWER_ACTIVE;
    s_status.last_wake = CSAW_WAKE_NONE;
    s_status.light_sleep_supported = true;
    s_last_activity = xTaskGetTickCount();
    s_last_ui_update = s_last_activity;

    power_configure_touch_wake();
    power_sync_rtc_from_system();

    if (csaw_hal_imu_wake_on_motion_enable(true) == ESP_OK) {
        s_status.wom_enabled = true;
    } else {
        ESP_LOGW(TAG, "Wake-on-motion unavailable; touch/RTC wake remain active");
    }

    power_refresh_alarm();
    csaw_hal_rtc_alarm_trace(s_status.rtc_alarm, sizeof(s_status.rtc_alarm));

    if (xTaskCreate(power_task, "csaw_power", CSAW_POWER_TASK_STACK, NULL,
                    CSAW_POWER_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power task");
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Power service ready: mode=%s wom=%d backup_charge=%d",
             csaw_power_mode_name(s_status.mode), s_status.wom_enabled ? 1 : 0,
             s_status.backup_charging ? 1 : 0);
    return ESP_OK;
}

esp_err_t csaw_power_set_mode(csaw_power_mode_t mode)
{
    csaw_power_notify_activity();
    return power_apply_mode(mode, true, "cli");
}

esp_err_t csaw_power_get_status(csaw_power_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    *status = s_status;
    return ESP_OK;
}

esp_err_t csaw_power_format_status(char *buf, size_t len)
{
    if (!buf || len < 32) return ESP_ERR_INVALID_ARG;
    snprintf(buf, len,
             "BAT %u%% %s | %s | wake=%s | %s",
             s_status.percent,
             s_status.charging ? "CHG" : (s_status.discharging ? "DIS" : "STBY"),
             csaw_power_mode_name(s_status.mode),
             csaw_power_wake_name(s_status.last_wake),
             s_status.backup_charging ? "RTC cell charging" : "RTC cell idle");
    return ESP_OK;
}

esp_err_t csaw_power_rtc_set_alarm(time_t when)
{
    struct tm tm_value;
    if (when <= 0 || !localtime_r(&when, &tm_value)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = csaw_hal_rtc_set_alarm((uint8_t)tm_value.tm_hour,
                                           (uint8_t)tm_value.tm_min,
                                           (uint8_t)tm_value.tm_sec,
                                           (uint8_t)tm_value.tm_mday,
                                           (uint8_t)tm_value.tm_wday);
    if (err == ESP_OK) {
        csaw_hal_rtc_alarm_trace(s_status.rtc_alarm, sizeof(s_status.rtc_alarm));
        ESP_LOGI(TAG, "RTC alarm set for %04d-%02d-%02d %02d:%02d:%02d",
                 tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday,
                 tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec);
    }
    return err;
}

esp_err_t csaw_power_rtc_clear_alarm(void)
{
    esp_err_t err = csaw_hal_rtc_clear_alarm();
    if (err == ESP_OK) {
        s_last_alarm_refresh = 0;
        csaw_hal_rtc_alarm_trace(s_status.rtc_alarm, sizeof(s_status.rtc_alarm));
    }
    return err;
}

esp_err_t csaw_power_schedule_timer(uint32_t seconds, const char *message)
{
    if (!message || !message[0] || seconds == 0) return ESP_ERR_INVALID_ARG;

    time_t now = time(NULL);
    cron_job_t job;
    memset(&job, 0, sizeof(job));
    job.kind = CRON_KIND_AT;
    job.at_epoch = (int64_t)now + seconds;
    snprintf(job.name, sizeof(job.name), "rtc-timer-%us", (unsigned)seconds);
    snprintf(job.message, sizeof(job.message), "%s", message);
    snprintf(job.channel, sizeof(job.channel), "%s", CSAW_CHAN_SYSTEM);
    snprintf(job.chat_id, sizeof(job.chat_id), "rtc_timer");
    job.delete_after_run = true;

    esp_err_t err = cron_add_job(&job);
    if (err != ESP_OK) return err;

    if (seconds <= 15300) {
        err = csaw_hal_rtc_timer_start(seconds);
    } else {
        err = csaw_power_rtc_set_alarm(job.at_epoch);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Timer cron job created but RTC arming failed: %s",
                 esp_err_to_name(err));
    }
    csaw_storage_log(TAG, "rtc_timer seconds=%u message=%.64s",
                     (unsigned)seconds, message);
    power_mark_wake(CSAW_WAKE_CLI, "rtc_timer scheduled");
    return ESP_OK;
}

esp_err_t csaw_power_notify_wake_word(const char *phrase)
{
    if (!phrase || !phrase[0]) return ESP_ERR_INVALID_ARG;

    char *content = NULL;
    int len = snprintf(NULL, 0, "Wake word detected: %s", phrase);
    if (len < 0) return ESP_ERR_INVALID_SIZE;
    content = malloc((size_t)len + 1);
    if (!content) return ESP_ERR_NO_MEM;
    snprintf(content, (size_t)len + 1, "Wake word detected: %s", phrase);

    csaw_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.channel, sizeof(msg.channel), "%s", CSAW_CHAN_SYSTEM);
    snprintf(msg.chat_id, sizeof(msg.chat_id), "wake_word");
    msg.content = content;

    esp_err_t err = message_bus_push_inbound(&msg);
    if (err != ESP_OK) {
        free(content);
        return err;
    }
    ESP_LOGI(TAG, "Wake-word event routed to agent loop: %.64s", phrase);
    power_mark_wake(CSAW_WAKE_WORD, phrase);
    return ESP_OK;
}
