#include "session_mgr.h"
#include "csaw_config.h"
#include "storage/csaw_storage.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "session";

/* Chat IDs arrive from external channels; reduce them to a safe FAT filename. */
static void safe_chat_id(const char *chat_id, char *out, size_t size)
{
    size_t j = 0;
    if (chat_id) {
        for (size_t i = 0; chat_id[i] && j + 1 < size; i++) {
            unsigned char c = (unsigned char)chat_id[i];
            if (isalnum(c) || c == '-' || c == '_') out[j++] = (char)c;
        }
    }
    if (j == 0) {
        snprintf(out, size, "unknown");
    } else {
        out[j] = '\0';
    }
}

static void session_path(const char *chat_id, char *buf, size_t size)
{
    char safe[64];
    safe_chat_id(chat_id, safe, sizeof(safe));
    snprintf(buf, size, "%s/%s.jsonl", CSAW_SD_SESSION_DIR, safe);
}

esp_err_t session_mgr_init(void)
{
    esp_err_t err = csaw_storage_ensure_dir(CSAW_SD_SESSION_DIR);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Session history on SD at %s (max %d KiB x%d rotations, %d sessions)",
             CSAW_SD_SESSION_DIR, (int)(CSAW_SESSION_MAX_BYTES / 1024),
             CSAW_SESSION_MAX_ROTATIONS, CSAW_SESSION_MAX_FILES);
    return ESP_OK;
}

esp_err_t session_append(const char *chat_id, const char *role, const char *content)
{
    char path[192];
    session_path(chat_id, path, sizeof(path));

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!line) return ESP_ERR_NO_MEM;

    size_t len = strlen(line);
    esp_err_t err = csaw_storage_append_rotated(path, line, len,
                                                CSAW_SESSION_MAX_BYTES,
                                                CSAW_SESSION_MAX_ROTATIONS);
    free(line);
    if (err != ESP_OK) return err;

    /* Bound total number of session files; oldest activity is discarded. */
    csaw_storage_prune(CSAW_SD_SESSION_DIR, "", CSAW_SESSION_MAX_FILES);
    csaw_storage_log(TAG, "session append chat=%s role=%s bytes=%d", chat_id, role, (int)len);
    return ESP_OK;
}

esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[192];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No history yet */
        snprintf(buf, size, "[]");
        return ESP_OK;
    }

    /* Read all lines into a ring buffer of cJSON objects */
    cJSON *messages[CSAW_SESSION_MAX_MSGS];
    int count = 0;
    int write_idx = 0;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        /* Ring buffer: overwrite oldest if full */
        if (count >= max_msgs) {
            cJSON_Delete(messages[write_idx]);
        }
        messages[write_idx] = obj;
        write_idx = (write_idx + 1) % max_msgs;
        if (count < max_msgs) count++;
    }
    fclose(f);

    /* Build JSON array with only role + content */
    cJSON *arr = cJSON_CreateArray();
    int start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % max_msgs;
        cJSON *src = messages[idx];

        cJSON *entry = cJSON_CreateObject();
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");
        if (role && content) {
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
        }
        cJSON_AddItemToArray(arr, entry);
    }

    /* Cleanup ring buffer */
    int cleanup_start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (cleanup_start + i) % max_msgs;
        cJSON_Delete(messages[idx]);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_clear(const char *chat_id)
{
    char path[192];
    session_path(chat_id, path, sizeof(path));

    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Session %s cleared from SD", chat_id);
        csaw_storage_log(TAG, "session cleared chat=%s", chat_id);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void session_list(void)
{
    DIR *dir = opendir(CSAW_SD_SESSION_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open SD session directory %s", CSAW_SD_SESSION_DIR);
        return;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 6 && strcmp(entry->d_name + len - 6, ".jsonl") == 0) {
            char full[320];
            snprintf(full, sizeof(full), "%s/%s", CSAW_SD_SESSION_DIR, entry->d_name);
            ESP_LOGI(TAG, "  SD session: %s", full);
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        ESP_LOGI(TAG, "  No sessions found on SD");
    }
}
