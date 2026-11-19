#include "legacy_channels.h"
#include "csaw_config.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "legacy_ch";

bool csaw_legacy_channel_enabled(const char *id)
{
    bool enabled;
    if (id && strcmp(id, "telegram") == 0) {
        enabled = CSAW_LEGACY_TELEGRAM_ENABLED;
    } else if (id && strcmp(id, "feishu") == 0) {
        enabled = CSAW_LEGACY_FEISHU_ENABLED;
    } else {
        return false;
    }

    /* Explicit NVS opt-in/opt-out overrides the build-time default. */
    nvs_handle_t nvs;
    if (nvs_open(CSAW_NVS_LEGACY, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t stored = 0;
        if (nvs_get_u8(nvs, id, &stored) == ESP_OK) enabled = stored != 0;
        nvs_close(nvs);
    }
    return enabled;
}

esp_err_t csaw_legacy_channel_set(const char *id, bool enabled)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    if (strcmp(id, "telegram") != 0 && strcmp(id, "feishu") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CSAW_NVS_LEGACY, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(nvs, id, enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Legacy channel '%s' %s (flash/NVS)", id,
                 enabled ? "enabled" : "disabled");
    }
    return err;
}
