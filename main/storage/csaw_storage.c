#include "storage/csaw_storage.h"
#include "csaw_config.h"

#include <errno.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "esp_log.h"

static const char *TAG = "storage";
static bool s_sd_ready;

/* --- path helpers ------------------------------------------------------- */

static int mkdir_p(const char *path)
{
    char buf[320];
    if (!path || strlen(path) >= sizeof(buf)) return -1;
    strcpy(buf, path);

    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

esp_err_t csaw_storage_ensure_dir(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    if (mkdir_p(path) != 0) {
        ESP_LOGE(TAG, "Cannot create SD directory %s: errno=%d", path, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t csaw_storage_init(void)
{
    static const char *const dirs[] = {
        CSAW_SD_ROOT, CSAW_SD_SESSION_DIR, CSAW_SD_CONTEXT_DIR,
        CSAW_SD_DAILY_DIR, CSAW_SD_CACHE_DIR, CSAW_SD_LOG_DIR,
        CSAW_SD_TEMP_DIR, CSAW_SD_PYTHON_DIR, CSAW_SD_TOOLS_DIR,
    };

    s_sd_ready = false;
    if (access(CSAW_SD_BASE, F_OK) != 0) {
        ESP_LOGE(TAG, "SD card mount %s unavailable; high-churn storage disabled",
                 CSAW_SD_BASE);
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (csaw_storage_ensure_dir(dirs[i]) != ESP_OK) {
            ESP_LOGE(TAG, "SD path schema incomplete at %s", dirs[i]);
            return ESP_FAIL;
        }
    }

    s_sd_ready = true;
    ESP_LOGI(TAG, "SD storage ready at %s (sessions/context/cache/logs/temp/python/tools)",
             CSAW_SD_ROOT);
    ESP_LOGI(TAG, "Flash holds config, long-term memory and provider secrets only");
    csaw_storage_log(TAG,
        "storage initialized: SD=%s, session_max=%d bytes x%d rotations, log_max=%d bytes x%d rotations",
        CSAW_SD_ROOT, (int)CSAW_SESSION_MAX_BYTES, CSAW_SESSION_MAX_ROTATIONS,
        (int)CSAW_LOG_MAX_BYTES, CSAW_LOG_MAX_ROTATIONS);
    return ESP_OK;
}

bool csaw_storage_sd_available(void)
{
    return s_sd_ready;
}

/* --- bounded/rotated writes --------------------------------------------- */

esp_err_t csaw_storage_append_rotated(const char *path, const char *data,
                                      size_t len, size_t max_bytes, int rotations)
{
    if (!path || !data) return ESP_ERR_INVALID_ARG;
    if (!s_sd_ready) return ESP_ERR_INVALID_STATE;

    struct stat st;
    size_t current = 0;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        current = (size_t)st.st_size;
    }
    if (current + len > max_bytes && rotations > 0) {
        for (int i = rotations; i >= 1; i--) {
            char src[320], dst[320];
            if (i == 1) {
                snprintf(src, sizeof(src), "%s", path);
            } else {
                snprintf(src, sizeof(src), "%s.%d", path, i - 1);
            }
            snprintf(dst, sizeof(dst), "%s.%d", path, i);
            if (access(src, F_OK) != 0) continue;
            if (i == rotations) remove(dst); /* discard oldest generation */
            if (rename(src, dst) != 0) {
                ESP_LOGW(TAG, "Rotation %s -> %s failed: errno=%d", src, dst, errno);
            }
        }
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot append %s: errno=%d", path, errno);
        return ESP_FAIL;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t csaw_storage_write_bounded(const char *path, const char *data,
                                     size_t len, size_t max_bytes)
{
    if (!path || !data) return ESP_ERR_INVALID_ARG;
    if (len > max_bytes) return ESP_ERR_INVALID_SIZE;

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write %s: errno=%d", path, errno);
        return ESP_FAIL;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return (written == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t csaw_storage_prune(const char *dir, const char *prefix, int max_files)
{
    if (!dir || !prefix || max_files < 0) return ESP_ERR_INVALID_ARG;

    typedef struct {
        char name[160];
        time_t mtime;
    } file_entry_t;

    file_entry_t entries[256];
    int count = 0;
    DIR *d = opendir(dir);
    if (!d) return ESP_ERR_NOT_FOUND;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < (int)(sizeof(entries) / sizeof(entries[0]))) {
        char full[320];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (strncmp(ent->d_name, prefix, strlen(prefix)) != 0) continue;
        if (strlen(ent->d_name) >= sizeof(entries[count].name)) continue;
        strcpy(entries[count].name, ent->d_name);
        entries[count].mtime = st.st_mtime;
        count++;
    }
    closedir(d);

    /* Simple insertion sort, oldest first. */
    for (int i = 1; i < count; i++) {
        file_entry_t key = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].mtime > key.mtime) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }

    int remove_count = count - max_files;
    for (int i = 0; i < remove_count; i++) {
        char full[320];
        int n = snprintf(full, sizeof(full), "%s/%s", dir, entries[i].name);
        if (n < 0 || n >= (int)sizeof(full)) {
            ESP_LOGW(TAG, "SD prune path too long, skipping");
            continue;
        }
        ESP_LOGI(TAG, "Pruning old SD file %s", full);
        remove(full);
    }
    return ESP_OK;
}

void csaw_storage_log(const char *tag, const char *fmt, ...)
{
    if (!s_sd_ready || !fmt) return;

    char line[640];
    char ts[32];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    int off = snprintf(line, sizeof(line), "%s %s: ", ts, tag ? tag : "csaw");
    if (off < 0 || (size_t)off >= sizeof(line)) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line + off, sizeof(line) - (size_t)off, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t len = strlen(line);
    if (len + 1 >= sizeof(line)) line[sizeof(line) - 2] = '\n';
    else line[len] = '\n', len++;

    csaw_storage_append_rotated(CSAW_SD_LOG_DIR "/csaw.log", line, len,
                                CSAW_LOG_MAX_BYTES, CSAW_LOG_MAX_ROTATIONS);
}

esp_err_t csaw_storage_status(char *buf, size_t size)
{
    if (!buf || size == 0) return ESP_ERR_INVALID_ARG;

    static const char *const dirs[] = {
        CSAW_SD_SESSION_DIR, CSAW_SD_CONTEXT_DIR, CSAW_SD_CACHE_DIR,
        CSAW_SD_LOG_DIR, CSAW_SD_TEMP_DIR, CSAW_SD_PYTHON_DIR,
        CSAW_SD_TOOLS_DIR,
    };
    static const char *const names[] = {
        "sessions", "context", "cache", "logs", "temp", "python", "tools",
    };

    size_t off = 0;
    off += snprintf(buf + off, size - off,
                    "SD %s: %s\nFlash(NVS/SPIFFS): config, long-term memory, secrets\n",
                    CSAW_SD_ROOT, s_sd_ready ? "READY" : "UNAVAILABLE");

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && off < size - 1; i++) {
        DIR *d = opendir(dirs[i]);
        int files = 0;
        size_t bytes = 0;
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                char full[320];
                struct stat st;
                snprintf(full, sizeof(full), "%s/%s", dirs[i], ent->d_name);
                if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) {
                    files++;
                    bytes += (size_t)st.st_size;
                }
            }
            closedir(d);
        }
        off += snprintf(buf + off, size - off, "  %-9s %3d files, %7d bytes\n",
                        names[i], files, (int)bytes);
    }
    if (off < size - 1) {
        off += snprintf(buf + off, size - off,
                        "Limits: session %d KiB x%d rot (%d files), daily %d KiB x%d rot (%d days), "
                        "log %d KiB x%d rot\n",
                        (int)(CSAW_SESSION_MAX_BYTES / 1024), CSAW_SESSION_MAX_ROTATIONS,
                        CSAW_SESSION_MAX_FILES, (int)(CSAW_DAILY_MAX_BYTES / 1024),
                        CSAW_DAILY_MAX_ROTATIONS, CSAW_DAILY_MAX_FILES,
                        (int)(CSAW_LOG_MAX_BYTES / 1024), CSAW_LOG_MAX_ROTATIONS);
    }
    buf[size - 1] = '\0';
    return ESP_OK;
}
