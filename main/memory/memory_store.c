#include "memory_store.h"
#include "csaw_config.h"
#include "storage/csaw_storage.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "memory";

static void get_date_str(char *buf, size_t size, int days_ago)
{
    time_t now;
    time(&now);
    now -= days_ago * 86400;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d", &tm);
}

esp_err_t memory_store_init(void)
{
    esp_err_t err = csaw_storage_ensure_dir(CSAW_SD_DAILY_DIR);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Long-term memory: flash %s (low churn)", CSAW_MEMORY_FILE);
    ESP_LOGI(TAG, "Daily/working memory: SD %s (max %d KiB, %d days)",
             CSAW_SD_DAILY_DIR, (int)(CSAW_DAILY_MAX_BYTES / 1024),
             CSAW_DAILY_MAX_FILES);
    return ESP_OK;
}

esp_err_t memory_read_long_term(char *buf, size_t size)
{
    FILE *f = fopen(CSAW_MEMORY_FILE, "r");
    if (!f) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t memory_write_long_term(const char *content)
{
    /* Long-term memory is explicitly allowed in flash, but is still bounded. */
    esp_err_t err = csaw_storage_write_bounded(CSAW_MEMORY_FILE, content,
                                               strlen(content), CSAW_MEMORY_MAX_BYTES);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot write %s: %s", CSAW_MEMORY_FILE, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Long-term memory updated (%d bytes, flash)", (int)strlen(content));
    return ESP_OK;
}

static void daily_path(const char *date_str, char *buf, size_t size)
{
    snprintf(buf, size, "%s/%s.md", CSAW_SD_DAILY_DIR, date_str);
}

esp_err_t memory_append_today(const char *note)
{
    char date_str[16];
    get_date_str(date_str, sizeof(date_str), 0);

    char path[192];
    daily_path(date_str, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (!f) {
            ESP_LOGE(TAG, "Cannot create SD daily note %s", path);
            return ESP_FAIL;
        }
        fprintf(f, "# %s\n\n", date_str);
        fclose(f);
    }

    char line[1024];
    int len = snprintf(line, sizeof(line), "%s\n", note);
    if (len <= 0) return ESP_FAIL;
    if ((size_t)len >= sizeof(line)) len = sizeof(line) - 1;

    esp_err_t err = csaw_storage_append_rotated(path, line, (size_t)len,
                                                CSAW_DAILY_MAX_BYTES,
                                                CSAW_DAILY_MAX_ROTATIONS);
    if (err != ESP_OK) return err;
    /* Keep at most two weeks of daily notes on the card. */
    csaw_storage_prune(CSAW_SD_DAILY_DIR, "", CSAW_DAILY_MAX_FILES);
    return ESP_OK;
}

esp_err_t memory_read_recent(char *buf, size_t size, int days)
{
    size_t offset = 0;
    buf[0] = '\0';

    for (int i = 0; i < days && offset < size - 1; i++) {
        char date_str[16];
        get_date_str(date_str, sizeof(date_str), i);

        char path[192];
        daily_path(date_str, path, sizeof(path));

        FILE *f = fopen(path, "r");
        if (!f) continue;

        if (offset > 0 && offset < size - 4) {
            offset += snprintf(buf + offset, size - offset, "\n---\n");
        }

        size_t n = fread(buf + offset, 1, size - offset - 1, f);
        offset += n;
        buf[offset] = '\0';
        fclose(f);
    }

    return ESP_OK;
}
