#pragma once

/* CsAW Global Configuration */

/* Build-time secrets (highest priority, override NVS) */
#if __has_include("csaw_secrets.h")
#include "csaw_secrets.h"
#endif

#ifndef CSAW_SECRET_WIFI_SSID
#define CSAW_SECRET_WIFI_SSID       ""
#endif
#ifndef CSAW_SECRET_WIFI_PASS
#define CSAW_SECRET_WIFI_PASS       ""
#endif
#ifndef CSAW_SECRET_TG_TOKEN
#define CSAW_SECRET_TG_TOKEN        ""
#endif
#ifndef CSAW_SECRET_API_KEY
#define CSAW_SECRET_API_KEY         ""
#endif
#ifndef CSAW_SECRET_MODEL
#define CSAW_SECRET_MODEL           ""
#endif
#ifndef CSAW_SECRET_MODEL_PROVIDER
#define CSAW_SECRET_MODEL_PROVIDER  "kilo"
#endif
#ifndef CSAW_SECRET_PROXY_HOST
#define CSAW_SECRET_PROXY_HOST      ""
#endif
#ifndef CSAW_SECRET_PROXY_PORT
#define CSAW_SECRET_PROXY_PORT      ""
#endif
#ifndef CSAW_SECRET_PROXY_TYPE
#define CSAW_SECRET_PROXY_TYPE      ""
#endif
#ifndef CSAW_SECRET_SEARCH_KEY
#define CSAW_SECRET_SEARCH_KEY      ""
#endif
#ifndef CSAW_SECRET_FEISHU_APP_ID
#define CSAW_SECRET_FEISHU_APP_ID   ""
#endif
#ifndef CSAW_SECRET_FEISHU_APP_SECRET
#define CSAW_SECRET_FEISHU_APP_SECRET ""
#endif
#ifndef CSAW_SECRET_TAVILY_KEY
#define CSAW_SECRET_TAVILY_KEY      ""
#endif

/* WiFi */
#define CSAW_WIFI_MAX_RETRY          10
#define CSAW_WIFI_RETRY_BASE_MS      1000
#define CSAW_WIFI_RETRY_MAX_MS       30000

/* Telegram Bot */
#define CSAW_TG_POLL_TIMEOUT_S       30
#define CSAW_TG_MAX_MSG_LEN          4096
#define CSAW_TG_POLL_STACK           (12 * 1024)
#define CSAW_TG_POLL_PRIO            5
#define CSAW_TG_POLL_CORE            0
#define CSAW_TG_CARD_SHOW_MS         3000
#define CSAW_TG_CARD_BODY_SCALE      3

/* Feishu Bot */
#define CSAW_FEISHU_MAX_MSG_LEN          4096
#define CSAW_FEISHU_POLL_STACK           (12 * 1024)
#define CSAW_FEISHU_POLL_PRIO            5
#define CSAW_FEISHU_POLL_CORE            0
#define CSAW_FEISHU_WEBHOOK_PORT         18790
#define CSAW_FEISHU_WEBHOOK_PATH         "/feishu/events"
#define CSAW_FEISHU_WEBHOOK_MAX_BODY     (16 * 1024)

/* Agent Loop */
#define CSAW_AGENT_STACK             (24 * 1024)
#define CSAW_AGENT_PRIO              6
#define CSAW_AGENT_CORE              1
#define CSAW_AGENT_MAX_HISTORY       20
#define CSAW_AGENT_MAX_TOOL_ITER     10
#define CSAW_MAX_TOOL_CALLS          4
#define CSAW_AGENT_SEND_WORKING_STATUS 1

/* Timezone (POSIX TZ format) */
#define CSAW_TIMEZONE                "PST8PDT,M3.2.0,M11.1.0"

/* LLM — free-provider chain (Kilo default, then OpenCode, then OpenRouter).
 * All endpoints use the chat-completions wire format and free-tier models.
 * Verified endpoints/models: docs/PROVIDER_CONFIG.md, config/providers.md. */
#define CSAW_LLM_DEFAULT_MODEL       "kilo-auto/free"
#define CSAW_LLM_PROVIDER_DEFAULT    "kilo"
#define CSAW_LLM_MAX_TOKENS          4096
#define CSAW_PROVIDER_CHAIN_DEFAULT  "kilo,opencode,openrouter"
#define CSAW_KILO_BASE_URL           "https://ai-gateway.kilo.ai/api/v1"
#define CSAW_KILO_MODEL              "kilo-auto/free"
#define CSAW_OPENCODE_BASE_URL       "https://opencode.ai/inference/openai/v1"
#define CSAW_OPENCODE_MODELS_URL     "https://opencode.ai/inference/v1/models"
#define CSAW_OPENCODE_MODEL          "mimo-v2.6-flash-free"
#define CSAW_OPENROUTER_BASE_URL     "https://openrouter.ai/api/v1"
#define CSAW_OPENROUTER_MODEL        "inclusionai/ling-3.2-flash:free"
#define CSAW_LLM_STREAM_BUF_SIZE     (32 * 1024)
#define CSAW_LLM_LOG_VERBOSE_PAYLOAD 0
#define CSAW_LLM_LOG_PREVIEW_BYTES   160

/* Message Bus */
#define CSAW_BUS_QUEUE_LEN           16
#define CSAW_OUTBOUND_STACK          (12 * 1024)
#define CSAW_OUTBOUND_PRIO           5
#define CSAW_OUTBOUND_CORE           0

/* Primary on-device UI (AGENTS.md 6). The on-screen keyboard/chat is the
 * default UX; network IM channels are optional legacy integrations. */
#define CSAW_UI_CHAT_ID              "local"
#define CSAW_LEGACY_TELEGRAM_ENABLED 0
#define CSAW_LEGACY_FEISHU_ENABLED   0

/* Storage policy (AGENTS.md 7).
 * Flash/NVS/SPIFFS: config, device settings, provider secrets and long-term memory only.
 * SDMMC TF card: every high-churn artifact (sessions, daily/working memory, cache,
 * logs, temp files, agent file tools and MicroPython scripts). */
#define CSAW_SPIFFS_BASE             "/spiffs"
#define CSAW_SPIFFS_CONFIG_DIR       CSAW_SPIFFS_BASE "/config"
#define CSAW_SPIFFS_MEMORY_DIR       CSAW_SPIFFS_BASE "/memory"
#define CSAW_MEMORY_FILE             CSAW_SPIFFS_MEMORY_DIR "/MEMORY.md"
#define CSAW_SOUL_FILE               CSAW_SPIFFS_CONFIG_DIR "/SOUL.md"
#define CSAW_USER_FILE               CSAW_SPIFFS_CONFIG_DIR "/USER.md"
#define CSAW_MEMORY_MAX_BYTES        (32 * 1024)
#define CSAW_CONTEXT_BUF_SIZE        (16 * 1024)
#define CSAW_SESSION_MAX_MSGS        20

/* SD card path schema (mounted by the HAL at /sd). */
#define CSAW_SD_BASE                 "/sd"
#define CSAW_SD_ROOT                 CSAW_SD_BASE "/csaw"
#define CSAW_SD_SESSION_DIR          CSAW_SD_ROOT "/sessions"
#define CSAW_SD_CONTEXT_DIR          CSAW_SD_ROOT "/context"
#define CSAW_SD_DAILY_DIR            CSAW_SD_CONTEXT_DIR "/daily"
#define CSAW_SD_CACHE_DIR            CSAW_SD_ROOT "/cache"
#define CSAW_SD_LOG_DIR              CSAW_SD_ROOT "/logs"
#define CSAW_SD_TEMP_DIR             CSAW_SD_ROOT "/temp"
#define CSAW_SD_PYTHON_DIR           CSAW_SD_ROOT "/python"
#define CSAW_SD_TOOLS_DIR            CSAW_SD_ROOT "/tools"

/* Rotation / size limits so the SD card cannot be exhausted by high-churn data. */
#define CSAW_SESSION_MAX_BYTES       (512 * 1024)
#define CSAW_SESSION_MAX_ROTATIONS   2
#define CSAW_SESSION_MAX_FILES       32
#define CSAW_DAILY_MAX_BYTES         (64 * 1024)
#define CSAW_DAILY_MAX_ROTATIONS     1
#define CSAW_DAILY_MAX_FILES         14
#define CSAW_LOG_MAX_BYTES           (256 * 1024)
#define CSAW_LOG_MAX_ROTATIONS       3
#define CSAW_CACHE_MAX_BYTES         (256 * 1024)
#define CSAW_CACHE_MAX_FILES         32
#define CSAW_TOOL_FILE_MAX_BYTES     (512 * 1024)

/* Cron / Heartbeat */
#define CSAW_CRON_FILE               CSAW_SPIFFS_BASE "/cron.json"
#define CSAW_CRON_MAX_JOBS           16
#define CSAW_CRON_CHECK_INTERVAL_MS  (60 * 1000)
#define CSAW_HEARTBEAT_FILE          CSAW_SD_CONTEXT_DIR "/HEARTBEAT.md"
#define CSAW_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)

/* GPIO */
#define CSAW_GPIO_CONFIG_SECTION     1   /* enable GPIO tools */

/* Power / RTC / low-power (AGENTS.md 8).
 * Touch INT is GPIO21 per vendor pin_config.h. The QMI8658 INT-to-ESP pin is
 * not established on Rev 1, so wake-on-shake is polled during short light
 * sleep timer wakes rather than assigned a guessed GPIO. */
#define CSAW_TOUCH_INT_GPIO          21
#define CSAW_BATTERY_CRITICAL_PCT    15
#define CSAW_BATTERY_SAVE_PCT        30
#define CSAW_POWER_IDLE_ACTIVE_MS    (120 * 1000)
#define CSAW_POWER_IDLE_SAVE_MS      (30 * 1000)
#define CSAW_POWER_IDLE_SLEEP_MS     (8 * 1000)
#define CSAW_POWER_IDLE_CRITICAL_MS  (20 * 1000)
#define CSAW_POWER_SLEEP_POLL_MS     500
#define CSAW_BACKUP_CELL_MV          3000

/* Skills */
#define CSAW_SKILLS_PREFIX           CSAW_SPIFFS_BASE "/skills/"

/* WebSocket Gateway */
#define CSAW_WS_PORT                 18789
#define CSAW_WS_MAX_CLIENTS          4

/* Serial CLI */
#define CSAW_CLI_STACK               (4 * 1024)
#define CSAW_CLI_PRIO                3
#define CSAW_CLI_CORE                0

/* NVS Namespaces */
#define CSAW_NVS_WIFI                "wifi_config"
#define CSAW_NVS_TG                  "tg_config"
#define CSAW_NVS_FEISHU              "feishu_config"
#define CSAW_NVS_LLM                 "llm_config"
#define CSAW_NVS_PROXY               "proxy_config"
#define CSAW_NVS_SEARCH              "search_config"
#define CSAW_NVS_LEGACY              "legacy_config"

/* NVS Keys */
#define CSAW_NVS_KEY_SSID            "ssid"
#define CSAW_NVS_KEY_PASS            "password"
#define CSAW_NVS_KEY_TG_TOKEN        "bot_token"
#define CSAW_NVS_KEY_FEISHU_APP_ID   "app_id"
#define CSAW_NVS_KEY_FEISHU_APP_SECRET "app_secret"
#define CSAW_NVS_KEY_API_KEY         "api_key"
#define CSAW_NVS_KEY_TAVILY_KEY      "tavily_key"
#define CSAW_NVS_KEY_MODEL           "model"
#define CSAW_NVS_KEY_PROVIDER        "provider"
#define CSAW_NVS_KEY_PROXY_HOST      "host"
#define CSAW_NVS_KEY_PROXY_PORT      "port"
#define CSAW_NVS_KEY_PROXY_TYPE      "proxy_type"

/* WiFi Onboarding (Captive Portal) */
#define CSAW_ONBOARD_AP_PREFIX    "CsAW-"
#define CSAW_ONBOARD_AP_PASS      ""          /* open network */
#define CSAW_ONBOARD_HTTP_PORT    80
#define CSAW_ONBOARD_DNS_STACK    (4 * 1024)
#define CSAW_ONBOARD_MAX_SCAN     20
