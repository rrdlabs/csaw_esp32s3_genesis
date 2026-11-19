/*
 * CsAW free-provider abstraction.
 *
 * Ordered chain: Kilo free router (default) -> OpenCode -> OpenRouter.
 * Every entry is a free-quota / free-router endpoint using the chat-completions
 * wire format. No paid-key path exists. Selection, keys and endpoints are
 * runtime-configurable and persisted in flash/NVS only (AGENTS.md 7).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define CSAW_PROVIDER_ID_MAX    24
#define CSAW_PROVIDER_NAME_MAX  36
#define CSAW_PROVIDER_URL_MAX   176
#define CSAW_PROVIDER_MODEL_MAX 64
#define CSAW_PROVIDER_KEY_MAX   320
#define CSAW_PROVIDER_CHAIN_MAX 4

typedef enum {
    CSAW_PROVIDER_KILO = 0,
    CSAW_PROVIDER_OPENCODE = 1,
    CSAW_PROVIDER_OPENROUTER = 2,
    CSAW_PROVIDER_COUNT = 3
} csaw_provider_id_t;

typedef struct {
    csaw_provider_id_t id_enum;
    char id[CSAW_PROVIDER_ID_MAX];
    char name[CSAW_PROVIDER_NAME_MAX];
    char chat_url[CSAW_PROVIDER_URL_MAX];
    char models_url[CSAW_PROVIDER_URL_MAX];
    char model[CSAW_PROVIDER_MODEL_MAX];
    char api_key[CSAW_PROVIDER_KEY_MAX];
    char nvs_prefix[16];
    bool enabled;
} csaw_provider_t;

/** Load defaults, then flash/NVS overrides. Call once at startup. */
esp_err_t provider_chain_init(void);

/** Number of enabled providers in the configured order. */
size_t provider_chain_count(void);

/** Provider at logical position in the configured order (or NULL). */
const csaw_provider_t *provider_chain_at(size_t index);

/** Next enabled provider after `current`, wrapping to the first (or NULL). */
const csaw_provider_t *provider_chain_next(const csaw_provider_t *current);

/** Lookup by id ("kilo", "opencode", "openrouter"). */
const csaw_provider_t *provider_chain_find(const char *id);

/** First enabled provider (the default entry point). */
const csaw_provider_t *provider_chain_primary(void);

/** Active provider (initial selection from NVS, advanced in memory on failure). */
const csaw_provider_t *provider_chain_active(void);
const char *provider_chain_active_id(void);

/** Manual selection: validates id, updates memory and persists to NVS. */
esp_err_t provider_chain_select(const char *id);

/** Runtime configuration (all persisted to NVS; never to SD card). */
esp_err_t provider_chain_set_key(const char *id, const char *key);
esp_err_t provider_chain_set_url(const char *id, const char *url);
esp_err_t provider_chain_set_model(const char *id, const char *model);
esp_err_t provider_chain_set_order(const char *csv);

/** Descriptive helpers for CLI / UI / logs. */
void provider_chain_describe(char *buf, size_t len);
bool provider_status_is_fallback(int http_status);

/**
 * Advance the in-memory active entry when a provider fails or rate-limits.
 * Deliberately NOT persisted: high-churn fallback state must not wear flash.
 */
void provider_chain_report_failure(const csaw_provider_t *provider, int http_status);
void provider_chain_report_success(const csaw_provider_t *provider);
