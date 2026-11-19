/*
 * CsAW free-provider chain implementation.
 * All network endpoints are free-quota routers; the chain advances on
 * transport failure, HTTP 429, auth rejection, and model unavailability.
 */
#include "provider_chain.h"
#include "csaw_config.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "providers";

/* NVS keys are limited to 15 characters by ESP-IDF. */
#define NVS_KEY_CHAIN   "chain"
#define NVS_KEY_ACTIVE  "active"

typedef struct {
    const char *id;
    const char *name;
    const char *chat_url;
    const char *models_url;
    const char *model;
    const char *nvs_prefix;
} provider_default_t;

static const provider_default_t s_defaults[CSAW_PROVIDER_COUNT] = {
    [CSAW_PROVIDER_KILO] = {
        .id = "kilo",
        .name = "Kilo free router",
        .chat_url = CSAW_KILO_BASE_URL "/chat/completions",
        .models_url = CSAW_KILO_BASE_URL "/models",
        .model = CSAW_KILO_MODEL,
        .nvs_prefix = "kilo",
    },
    [CSAW_PROVIDER_OPENCODE] = {
        .id = "opencode",
        .name = "OpenCode",
        .chat_url = CSAW_OPENCODE_BASE_URL "/chat/completions",
        .models_url = CSAW_OPENCODE_MODELS_URL,
        .model = CSAW_OPENCODE_MODEL,
        .nvs_prefix = "ocode",
    },
    [CSAW_PROVIDER_OPENROUTER] = {
        .id = "openrouter",
        .name = "OpenRouter free",
        .chat_url = CSAW_OPENROUTER_BASE_URL "/chat/completions",
        .models_url = CSAW_OPENROUTER_BASE_URL "/models",
        .model = CSAW_OPENROUTER_MODEL,
        .nvs_prefix = "oroute",
    },
};

static csaw_provider_t s_providers[CSAW_PROVIDER_COUNT];
static csaw_provider_id_t s_order[CSAW_PROVIDER_CHAIN_MAX];
static size_t s_order_len = 0;
static csaw_provider_id_t s_active = CSAW_PROVIDER_KILO;
static bool s_initialized = false;

static void copy_str(char *dst, size_t len, const char *src)
{
    if (!dst || len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void nvs_key_for(const csaw_provider_t *p, const char *field, char *out, size_t len)
{
    snprintf(out, len, "%s_%s", p->nvs_prefix, field);
}

static esp_err_t nvs_read_str(const char *key, char *buf, size_t len)
{
    nvs_handle_t nvs;
    if (nvs_open(CSAW_NVS_LLM, NVS_READONLY, &nvs) != ESP_OK) return ESP_ERR_NOT_FOUND;
    size_t got = len;
    esp_err_t err = nvs_get_str(nvs, key, buf, &got);
    nvs_close(nvs);
    return err;
}

static esp_err_t nvs_write_str(const char *key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CSAW_NVS_LLM, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_str(nvs, key, value);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static csaw_provider_id_t id_to_enum(const char *id)
{
    if (!id) return CSAW_PROVIDER_COUNT;
    if (strcasecmp(id, "kilo") == 0) return CSAW_PROVIDER_KILO;
    if (strcasecmp(id, "opencode") == 0) return CSAW_PROVIDER_OPENCODE;
    if (strcasecmp(id, "openrouter") == 0) return CSAW_PROVIDER_OPENROUTER;
    return CSAW_PROVIDER_COUNT;
}

static void set_default_order(void)
{
    s_order[0] = CSAW_PROVIDER_KILO;
    s_order[1] = CSAW_PROVIDER_OPENCODE;
    s_order[2] = CSAW_PROVIDER_OPENROUTER;
    s_order_len = CSAW_PROVIDER_COUNT;
}

static esp_err_t parse_order(const char *csv)
{
    if (!csv || !csv[0]) return ESP_ERR_INVALID_ARG;
    csaw_provider_id_t parsed[CSAW_PROVIDER_CHAIN_MAX];
    size_t n = 0;
    const char *start = csv;
    while (*start && n < CSAW_PROVIDER_CHAIN_MAX) {
        const char *comma = strchr(start, ',');
        size_t len = comma ? (size_t)(comma - start) : strlen(start);
        if (len == 0 || len >= CSAW_PROVIDER_ID_MAX) return ESP_ERR_INVALID_ARG;
        char id[CSAW_PROVIDER_ID_MAX];
        memcpy(id, start, len);
        id[len] = '\0';
        csaw_provider_id_t pid = id_to_enum(id);
        if (pid == CSAW_PROVIDER_COUNT) return ESP_ERR_INVALID_ARG;
        /* No duplicates: each provider appears at most once. */
        for (size_t i = 0; i < n; i++) {
            if (parsed[i] == pid) return ESP_ERR_INVALID_ARG;
        }
        parsed[n++] = pid;
        if (!comma) break;
        start = comma + 1;
    }
    if (n == 0) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < n; i++) s_order[i] = parsed[i];
    s_order_len = n;
    return ESP_OK;
}

esp_err_t provider_chain_init(void)
{
    if (s_initialized) return ESP_OK;

    for (size_t i = 0; i < CSAW_PROVIDER_COUNT; i++) {
        const provider_default_t *d = &s_defaults[i];
        csaw_provider_t *p = &s_providers[i];
        memset(p, 0, sizeof(*p));
        p->id_enum = (csaw_provider_id_t)i;
        copy_str(p->id, sizeof(p->id), d->id);
        copy_str(p->name, sizeof(p->name), d->name);
        copy_str(p->chat_url, sizeof(p->chat_url), d->chat_url);
        copy_str(p->models_url, sizeof(p->models_url), d->models_url);
        copy_str(p->model, sizeof(p->model), d->model);
        copy_str(p->nvs_prefix, sizeof(p->nvs_prefix), d->nvs_prefix);
        p->enabled = true;

        char key[24];
        nvs_key_for(p, "url", key, sizeof(key));
        char tmp[CSAW_PROVIDER_URL_MAX];
        if (nvs_read_str(key, tmp, sizeof(tmp)) == ESP_OK && tmp[0]) {
            copy_str(p->chat_url, sizeof(p->chat_url), tmp);
        }
        nvs_key_for(p, "model", key, sizeof(key));
        if (nvs_read_str(key, tmp, sizeof(tmp)) == ESP_OK && tmp[0]) {
            copy_str(p->model, sizeof(p->model), tmp);
        }
        nvs_key_for(p, "key", key, sizeof(key));
        char keybuf[CSAW_PROVIDER_KEY_MAX];
        if (nvs_read_str(key, keybuf, sizeof(keybuf)) == ESP_OK && keybuf[0]) {
            copy_str(p->api_key, sizeof(p->api_key), keybuf);
        }
    }

    /* Build-time secrets apply to the default provider (Kilo) only. */
    if (CSAW_SECRET_API_KEY[0]) {
        copy_str(s_providers[CSAW_PROVIDER_KILO].api_key,
                 sizeof(s_providers[CSAW_PROVIDER_KILO].api_key), CSAW_SECRET_API_KEY);
    }
    if (CSAW_SECRET_MODEL[0]) {
        copy_str(s_providers[CSAW_PROVIDER_KILO].model,
                 sizeof(s_providers[CSAW_PROVIDER_KILO].model), CSAW_SECRET_MODEL);
    }

    set_default_order();
    char chain[128];
    if (nvs_read_str(NVS_KEY_CHAIN, chain, sizeof(chain)) == ESP_OK && chain[0]) {
        if (parse_order(chain) != ESP_OK) {
            ESP_LOGW(TAG, "Invalid stored provider chain '%s'; using defaults", chain);
            set_default_order();
        }
    }

    s_active = CSAW_PROVIDER_KILO;
    char active[CSAW_PROVIDER_ID_MAX];
    if (nvs_read_str(NVS_KEY_ACTIVE, active, sizeof(active)) == ESP_OK && active[0]) {
        csaw_provider_id_t pid = id_to_enum(active);
        if (pid != CSAW_PROVIDER_COUNT) s_active = pid;
    } else if (CSAW_SECRET_MODEL_PROVIDER[0]) {
        csaw_provider_id_t pid = id_to_enum(CSAW_SECRET_MODEL_PROVIDER);
        if (pid != CSAW_PROVIDER_COUNT) s_active = pid;
    }

    s_initialized = true;
    provider_chain_describe(chain, sizeof(chain));
    ESP_LOGI(TAG, "Provider chain: %s (active: %s)", chain,
             s_providers[s_active].id);
    for (size_t i = 0; i < CSAW_PROVIDER_COUNT; i++) {
        ESP_LOGI(TAG, "  %-11s %-20s model=%s key=%s", s_providers[i].id,
                 s_providers[i].name, s_providers[i].model,
                 s_providers[i].api_key[0] ? "configured" : "anonymous-free");
    }
    return ESP_OK;
}

size_t provider_chain_count(void)
{
    size_t n = 0;
    for (size_t i = 0; i < s_order_len; i++) {
        if (s_providers[s_order[i]].enabled) n++;
    }
    return n;
}

const csaw_provider_t *provider_chain_at(size_t index)
{
    size_t seen = 0;
    for (size_t i = 0; i < s_order_len; i++) {
        const csaw_provider_t *p = &s_providers[s_order[i]];
        if (!p->enabled) continue;
        if (seen == index) return p;
        seen++;
    }
    return NULL;
}

const csaw_provider_t *provider_chain_next(const csaw_provider_t *current)
{
    if (!current || !s_order_len) return NULL;
    size_t pos = 0;
    while (pos < s_order_len && s_order[pos] != current->id_enum) pos++;
    if (pos == s_order_len) return provider_chain_primary();
    for (size_t step = 1; step <= s_order_len; step++) {
        const csaw_provider_t *p = &s_providers[s_order[(pos + step) % s_order_len]];
        if (p->enabled) return p;
    }
    return NULL;
}

const csaw_provider_t *provider_chain_find(const char *id)
{
    csaw_provider_id_t pid = id_to_enum(id);
    return pid == CSAW_PROVIDER_COUNT ? NULL : &s_providers[pid];
}

const csaw_provider_t *provider_chain_primary(void)
{
    return s_order_len ? &s_providers[s_order[0]] : NULL;
}

const csaw_provider_t *provider_chain_active(void)
{
    return &s_providers[s_active];
}

const char *provider_chain_active_id(void)
{
    return s_providers[s_active].id;
}

esp_err_t provider_chain_select(const char *id)
{
    csaw_provider_id_t pid = id_to_enum(id);
    if (pid == CSAW_PROVIDER_COUNT) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_write_str(NVS_KEY_ACTIVE, s_providers[pid].id);
    if (err != ESP_OK) return err;
    s_active = pid;
    ESP_LOGI(TAG, "Active provider set to %s", s_providers[pid].id);
    return ESP_OK;
}

esp_err_t provider_chain_set_key(const char *id, const char *key)
{
    csaw_provider_id_t pid = id_to_enum(id);
    if (pid == CSAW_PROVIDER_COUNT || !key) return ESP_ERR_INVALID_ARG;
    csaw_provider_t *p = &s_providers[pid];
    char nvs_key[24];
    nvs_key_for(p, "key", nvs_key, sizeof(nvs_key));
    esp_err_t err = nvs_write_str(nvs_key, key);
    if (err != ESP_OK) return err;
    copy_str(p->api_key, sizeof(p->api_key), key);
    if (pid == s_active) {
        err = nvs_write_str(CSAW_NVS_KEY_API_KEY, key);
        if (err != ESP_OK) return err;
    }
    ESP_LOGI(TAG, "API key stored for %s (flash/NVS)", p->id);
    return ESP_OK;
}

esp_err_t provider_chain_set_url(const char *id, const char *url)
{
    csaw_provider_id_t pid = id_to_enum(id);
    if (pid == CSAW_PROVIDER_COUNT || !url || !url[0]) return ESP_ERR_INVALID_ARG;
    /* Only HTTPS endpoints are accepted; never allow plaintext keys. */
    if (strncasecmp(url, "https://", 8) != 0) return ESP_ERR_INVALID_ARG;
    csaw_provider_t *p = &s_providers[pid];
    char nvs_key[24];
    nvs_key_for(p, "url", nvs_key, sizeof(nvs_key));
    esp_err_t err = nvs_write_str(nvs_key, url);
    if (err != ESP_OK) return err;
    copy_str(p->chat_url, sizeof(p->chat_url), url);
    ESP_LOGI(TAG, "Chat endpoint stored for %s: %s", p->id, p->chat_url);
    return ESP_OK;
}

esp_err_t provider_chain_set_model(const char *id, const char *model)
{
    csaw_provider_id_t pid = id_to_enum(id);
    if (pid == CSAW_PROVIDER_COUNT || !model || !model[0]) return ESP_ERR_INVALID_ARG;
    csaw_provider_t *p = &s_providers[pid];
    char nvs_key[24];
    nvs_key_for(p, "model", nvs_key, sizeof(nvs_key));
    esp_err_t err = nvs_write_str(nvs_key, model);
    if (err != ESP_OK) return err;
    copy_str(p->model, sizeof(p->model), model);
    if (pid == s_active) err = nvs_write_str(CSAW_NVS_KEY_MODEL, model);
    ESP_LOGI(TAG, "Model stored for %s: %s", p->id, p->model);
    return err;
}

esp_err_t provider_chain_set_order(const char *csv)
{
    csaw_provider_id_t saved[CSAW_PROVIDER_CHAIN_MAX];
    size_t saved_len = s_order_len;
    memcpy(saved, s_order, sizeof(saved));
    esp_err_t err = parse_order(csv);
    if (err != ESP_OK) return err;
    err = nvs_write_str(NVS_KEY_CHAIN, csv);
    if (err != ESP_OK) {
        memcpy(s_order, saved, sizeof(saved));
        s_order_len = saved_len;
        return err;
    }
    char buf[128];
    provider_chain_describe(buf, sizeof(buf));
    ESP_LOGI(TAG, "Provider order: %s", buf);
    return ESP_OK;
}

void provider_chain_describe(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    size_t off = 0;
    buf[0] = '\0';
    bool first = true;
    for (size_t i = 0; i < s_order_len; i++) {
        const csaw_provider_t *p = &s_providers[s_order[i]];
        if (!p->enabled) continue;
        int n = snprintf(buf + off, len - off, "%s%s", first ? "" : " -> ", p->id);
        if (n <= 0 || (size_t)n >= len - off) break;
        off += (size_t)n;
        first = false;
    }
}

bool provider_status_is_fallback(int status)
{
    return status == 429 || status == 401 || status == 403 ||
           status == 404 || status == 408 || status == 409 ||
           (status >= 500 && status < 600);
}

void provider_chain_report_failure(const csaw_provider_t *provider, int http_status)
{
    if (!provider || !provider_status_is_fallback(http_status)) return;
    const csaw_provider_t *next = provider_chain_next(provider);
    if (!next || strcmp(next->id, provider->id) == 0) return;
    s_active = next->id_enum;
    /* In-memory only: fallback is high-churn and must not wear flash. */
    ESP_LOGW(TAG, "%s failed (HTTP %d); falling back to %s for subsequent requests",
             provider->id, http_status, next->id);
}

void provider_chain_report_success(const csaw_provider_t *provider)
{
    if (!provider) return;
    s_active = provider->id_enum;
}
