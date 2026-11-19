#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "csaw_config.h"
#include "csaw_hal.h"
#include "csaw_ui.h"
#include "bus/message_bus.h"
#include "wifi/wifi_manager.h"
#include "channels/telegram/telegram_bot.h"
#include "channels/feishu/feishu_bot.h"
#include "channels/legacy_channels.h"
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "storage/csaw_storage.h"
#include "gateway/ws_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "cron/cron_service.h"
#include "power/csaw_power.h"
#include "heartbeat/heartbeat.h"
#include "skills/skill_loader.h"
#include "onboard/wifi_onboard.h"

static const char *TAG = "csaw";

/* Bridge the on-screen keyboard to the same inbound queue every other route
 * uses. The agent loop echoes the channel/chat_id back, so replies from the
 * provider chain are dispatched to csaw_ui_show_response(). */
static void ui_send_bridge(const char *text)
{
    if (!text || !text[0]) return;
    csaw_power_notify_activity();
    csaw_msg_t msg = {0};
    snprintf(msg.channel, sizeof(msg.channel), "%s", CSAW_CHAN_UI);
    snprintf(msg.chat_id, sizeof(msg.chat_id), "%s", CSAW_UI_CHAT_ID);
    msg.content = strdup(text);
    if (!msg.content) return;
    if (message_bus_push_inbound(&msg) != ESP_OK) free(msg.content);
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CSAW_SPIFFS_BASE,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/* Outbound dispatch task: reads from outbound queue and routes to channels */
static void outbound_dispatch_task(void *arg)
{
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        csaw_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != ESP_OK) continue;

        ESP_LOGI(TAG, "Dispatching response to %s:%s", msg.channel, msg.chat_id);

        if (strcmp(msg.channel, CSAW_CHAN_UI) == 0) {
            /* Primary UX: render assistant replies on the SH8601 chat screen. */
            esp_err_t ui_err = csaw_ui_show_response(msg.content);
            if (ui_err != ESP_OK) {
                ESP_LOGW(TAG, "On-screen UI reply failed: %s", esp_err_to_name(ui_err));
            } else {
                ESP_LOGI(TAG, "On-screen UI reply shown (%d bytes)", (int)strlen(msg.content));
            }
        } else if (strcmp(msg.channel, CSAW_CHAN_TELEGRAM) == 0) {
            if (!csaw_legacy_channel_enabled("telegram")) {
                ESP_LOGW(TAG, "Dropping Telegram reply: legacy channel disabled by default");
            } else {
                esp_err_t send_err = telegram_send_message(msg.chat_id, msg.content);
                if (send_err != ESP_OK) {
                    ESP_LOGE(TAG, "Telegram send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
                } else {
                    ESP_LOGI(TAG, "Telegram send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
                }
            }
        } else if (strcmp(msg.channel, CSAW_CHAN_FEISHU) == 0) {
            if (!csaw_legacy_channel_enabled("feishu")) {
                ESP_LOGW(TAG, "Dropping Feishu reply: legacy channel disabled by default");
            } else {
                esp_err_t send_err = feishu_send_message(msg.chat_id, msg.content);
                if (send_err != ESP_OK) {
                    ESP_LOGE(TAG, "Feishu send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
                } else {
                    ESP_LOGI(TAG, "Feishu send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
                }
            }
        } else if (strcmp(msg.channel, CSAW_CHAN_WEBSOCKET) == 0) {
            esp_err_t ws_err = ws_server_send(msg.chat_id, msg.content);
            if (ws_err != ESP_OK) {
                ESP_LOGW(TAG, "WS send failed for %s: %s", msg.chat_id, esp_err_to_name(ws_err));
            }
        } else if (strcmp(msg.channel, CSAW_CHAN_SYSTEM) == 0) {
            ESP_LOGI(TAG, "System message [%s]: %.128s", msg.chat_id, msg.content);
        } else {
            ESP_LOGW(TAG, "Unknown channel: %s", msg.channel);
        }

        free(msg.content);
    }
}

void app_main(void)
{
    /* Silence noisy components */
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  CsAW - ESP32-S3 AI Agent");
    ESP_LOGI(TAG, "========================================");

    /* Print memory info */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Phase 0: Board hardware (AGENTS.md 4.1: IO expander -> PMU -> SH8601).
     * Must complete before any UI is drawn; brightness >=30% is asserted here. */
    bool brightness_asserted = false;
    esp_err_t hal_err = csaw_hal_init(&brightness_asserted);
    if (hal_err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware bringup FAILED: %s", esp_err_to_name(hal_err));
    } else if (!brightness_asserted) {
        ESP_LOGE(TAG, "Hardware bringup FAILED: brightness gate not asserted");
    } else {
        ESP_LOGI(TAG, "Hardware bringup OK (brightness >= 30%% asserted before UI)");
    }

    /* Phase 1: Core infrastructure */
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_spiffs());

    /* Storage policy (AGENTS.md 7): prepare the SD path schema before any
     * memory/session/tool writes. A missing card is logged, not fatal: the UI
     * and diagnostics still work, but high-churn writes are refused. */
    esp_err_t storage_err = csaw_storage_init();
    if (storage_err != ESP_OK) {
        ESP_LOGE(TAG, "SD storage init FAILED: %s (high-churn writes disabled)",
                 esp_err_to_name(storage_err));
    }

    /* Initialize subsystems */
    ESP_ERROR_CHECK(message_bus_init());

    /* Primary UX: create the LVGL chat UI immediately after the hardware
     * bringup/brightness gate and before WiFi onboarding, so the panel is
     * never black while the device is waiting for a network. */
    esp_err_t ui_err = csaw_ui_init();
    if (ui_err != ESP_OK) {
        ESP_LOGE(TAG, "On-device UI init FAILED: %s", esp_err_to_name(ui_err));
    } else {
        csaw_ui_set_send_cb(ui_send_bridge);
        csaw_ui_show_status("CsAW ready - tap the input field to type");
        ESP_LOGI(TAG, "Primary UX: on-screen keyboard/chat UI on channel '%s'", CSAW_CHAN_UI);
    }

    ESP_ERROR_CHECK(memory_store_init());
    ESP_ERROR_CHECK(skill_loader_init());
    ESP_ERROR_CHECK(session_mgr_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_proxy_init());
    /* Optional legacy channels: inert unless explicitly opted in. */
    if (csaw_legacy_channel_enabled("telegram")) {
        ESP_ERROR_CHECK(telegram_bot_init());
    } else {
        ESP_LOGI(TAG, "Legacy Telegram channel disabled (default); on-screen UI is primary");
    }
    if (csaw_legacy_channel_enabled("feishu")) {
        ESP_ERROR_CHECK(feishu_bot_init());
    } else {
        ESP_LOGI(TAG, "Legacy Feishu channel disabled (default); on-screen UI is primary");
    }
    ESP_ERROR_CHECK(llm_proxy_init());
    ESP_ERROR_CHECK(tool_registry_init());
    ESP_ERROR_CHECK(cron_service_init());
    /* PMU/RTC/low-power service: battery policy, alarm/timer wiring and
     * wake-word -> agent-loop bridge (AGENTS.md 8). */
    esp_err_t power_err = csaw_power_init();
    if (power_err != ESP_OK) {
        ESP_LOGE(TAG, "Power service init FAILED: %s", esp_err_to_name(power_err));
    }
    ESP_ERROR_CHECK(heartbeat_init());
    ESP_ERROR_CHECK(agent_loop_init());

    /* Start Serial CLI first (works without WiFi) */
    ESP_ERROR_CHECK(serial_cli_init());

    /* Start WiFi */
    esp_err_t wifi_err = wifi_manager_start();
    bool wifi_ok = false;
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Scanning nearby APs on boot...");
        wifi_manager_scan_and_print();
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        if (wifi_manager_wait_connected(30000) == ESP_OK) {
            wifi_ok = true;
            ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_get_ip());
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout");
        }
    } else {
        ESP_LOGW(TAG, "No WiFi credentials configured");
    }

    if (!wifi_ok) {
        ESP_LOGW(TAG, "Entering WiFi onboarding mode...");
        wifi_onboard_start(WIFI_ONBOARD_MODE_CAPTIVE);  /* blocks, restarts on success */
        return;  /* unreachable */
    }

    if (wifi_onboard_start(WIFI_ONBOARD_MODE_ADMIN) != ESP_OK) {
        ESP_LOGW(TAG, "Local admin portal unavailable; continuing without config hotspot");
    }

    {
        /* Outbound dispatch task should start first to avoid dropping early replies. */
        ESP_ERROR_CHECK((xTaskCreatePinnedToCore(
            outbound_dispatch_task, "outbound",
            CSAW_OUTBOUND_STACK, NULL,
            CSAW_OUTBOUND_PRIO, NULL, CSAW_OUTBOUND_CORE) == pdPASS)
            ? ESP_OK : ESP_FAIL);

        /* Start network-dependent services */
        ESP_ERROR_CHECK(agent_loop_start());
        if (csaw_legacy_channel_enabled("telegram")) ESP_ERROR_CHECK(telegram_bot_start());
        if (csaw_legacy_channel_enabled("feishu")) ESP_ERROR_CHECK(feishu_bot_start());
        cron_service_start();
        ESP_ERROR_CHECK(heartbeat_start());
        ESP_ERROR_CHECK(ws_server_start());

        ESP_LOGI(TAG, "All services started!");
    }

    ESP_LOGI(TAG, "CsAW ready. Primary UX: on-screen chat UI; CLI is diagnostics-only.");
    ESP_LOGI(TAG, "Type 'help' for CLI commands; 'legacy_channel_set' manages optional IM channels.");
}
