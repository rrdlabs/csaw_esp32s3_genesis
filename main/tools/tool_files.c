#include "tools/tool_files.h"
#include "csaw_config.h"
#include "storage/csaw_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tool_files";

#define MAX_FILE_SIZE CSAW_TOOL_FILE_MAX_BYTES

/**
 * Validate that a path is inside the SD card mount (AGENTS.md 7/9) and has no
 * ".." traversal. Flash paths (/spiffs, NVS) are never accepted by file tools.
 */
static bool validate_path(const char *path)
{
    if (!path) return false;
    if (strncmp(path, "/spiffs", 7) == 0) return false;
    size_t base_len = strlen(CSAW_SD_BASE);
    if (strncmp(path, CSAW_SD_BASE, base_len) != 0) return false;
    /* Require a path separator after the base (unless base ends with '/') */
    if (base_len > 0 && CSAW_SD_BASE[base_len - 1] != '/') {
        if (path[base_len] != '/') return false;
    }
    if (strstr(path, "..") != NULL) return false;
    return true;
}

/* ── read_file ─────────────────────────────────────────────── */

esp_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", CSAW_SD_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    size_t max_read = output_size - 1;
    if (max_read > MAX_FILE_SIZE) max_read = MAX_FILE_SIZE;

    size_t n = fread(output, 1, max_read, f);
    output[n] = '\0';
    fclose(f);

    ESP_LOGI(TAG, "read_file: %s (%d bytes)", path, (int)n);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── write_file ────────────────────────────────────────────── */

esp_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));

    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", CSAW_SD_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!content) {
        snprintf(output, output_size, "Error: missing 'content' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        snprintf(output, output_size, "Error: wrote %d of %d bytes to %s", (int)written, (int)len, path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    snprintf(output, output_size, "OK: wrote %d bytes to %s", (int)written, path);
    ESP_LOGI(TAG, "write_file: %s (%d bytes)", path, (int)written);
    csaw_storage_log(TAG, "write_file %s (%d bytes)", path, (int)written);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── edit_file ─────────────────────────────────────────────── */

esp_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *old_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "old_string"));
    const char *new_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "new_string"));

    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", CSAW_SD_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!old_str || !new_str) {
        snprintf(output, output_size, "Error: missing 'old_string' or 'new_string' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    /* Read existing file */
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > MAX_FILE_SIZE) {
        snprintf(output, output_size, "Error: file too large or empty (%ld bytes)", file_size);
        fclose(f);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Allocate buffer for the result (old content + possible expansion) */
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t max_result = file_size + (new_len > old_len ? new_len - old_len : 0) + 1;
    char *buf = malloc(file_size + 1);
    char *result = malloc(max_result);
    if (!buf || !result) {
        free(buf);
        free(result);
        fclose(f);
        snprintf(output, output_size, "Error: out of memory");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(buf, 1, file_size, f);
    buf[n] = '\0';
    fclose(f);

    /* Find and replace first occurrence */
    char *pos = strstr(buf, old_str);
    if (!pos) {
        snprintf(output, output_size, "Error: old_string not found in %s", path);
        free(buf);
        free(result);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    size_t prefix_len = pos - buf;
    memcpy(result, buf, prefix_len);
    memcpy(result + prefix_len, new_str, new_len);
    size_t suffix_start = prefix_len + old_len;
    size_t suffix_len = n - suffix_start;
    memcpy(result + prefix_len + new_len, buf + suffix_start, suffix_len);
    size_t total = prefix_len + new_len + suffix_len;
    result[total] = '\0';

    free(buf);

    /* Write back */
    f = fopen(path, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
        free(result);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    fwrite(result, 1, total, f);
    fclose(f);
    free(result);

    snprintf(output, output_size, "OK: edited %s (replaced %d bytes with %d bytes)", path, (int)old_len, (int)new_len);
    ESP_LOGI(TAG, "edit_file: %s", path);
    csaw_storage_log(TAG, "edit_file %s", path);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── list_dir ──────────────────────────────────────────────── */

esp_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    const char *prefix = NULL;
    if (root) {
        cJSON *pfx = cJSON_GetObjectItem(root, "prefix");
        if (pfx && cJSON_IsString(pfx)) {
            prefix = pfx->valuestring;
        }
    }

    DIR *dir = opendir(CSAW_SD_BASE);
    if (!dir) {
        snprintf(output, output_size, "Error: cannot open %s directory", CSAW_SD_BASE);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t off = 0;
    struct dirent *ent;
    int count = 0;

    while ((ent = readdir(dir)) != NULL && off < output_size - 1) {
        /* Build full path: SPIFFS entries are just filenames with embedded slashes */
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", CSAW_SD_BASE, ent->d_name);

        if (prefix && strncmp(full_path, prefix, strlen(prefix)) != 0) {
            continue;
        }

        off += snprintf(output + off, output_size - off, "%s\n", full_path);
        count++;
    }

    closedir(dir);

    if (count == 0) {
        snprintf(output, output_size, "(no files found)");
    }

    ESP_LOGI(TAG, "list_dir: %d files (prefix=%s)", count, prefix ? prefix : "(none)");
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── SD-only, no-confirmation filesystem tools (AGENTS.md §9) ─────────────
 *
 * These helpers back create_dir / move / copy / delete. They are intentionally
 * stricter than validate_path() above: every path must be CSAW_SD_ROOT
 * ("/sd/csaw") itself or live beneath it. Flash (SPIFFS), NVS, bare "/sd"
 * locations and any ".." traversal are rejected before any filesystem call.
 * All operations are logged with csaw_storage_log() to the SD audit log.
 */

#define SD_TOOL_PATH_MAX   512
#define SD_TOOL_COPY_BUF   4096
#define SD_TOOL_MAX_DEPTH  32

/**
 * Safety base for the unrestricted tools. CSAW_SD_BASE pins the mount point
 * ("/sd"); CSAW_SD_ROOT narrows the writable workspace to "/sd/csaw".
 */
static bool validate_sd_root_path(const char *path)
{
    if (!path || path[0] != '/') return false;

    /* Explicitly reject flash / NVS namespaces (defence in depth). */
    if (strncmp(path, "/spiffs", 7) == 0) return false;
    if (strncmp(path, "/flash",  6) == 0) return false;
    if (strncmp(path, "/nvs",    4) == 0) return false;
    if (strstr(path, "..") != NULL) return false;

    size_t base_len = strlen(CSAW_SD_BASE);   /* "/sd" */
    if (strncmp(path, CSAW_SD_BASE, base_len) != 0) return false;
    if (path[base_len] != '/' && path[base_len] != '\0') return false;

    size_t root_len = strlen(CSAW_SD_ROOT);   /* "/sd/csaw" */
    if (strncmp(path, CSAW_SD_ROOT, root_len) != 0) return false;
    if (path[root_len] != '/' && path[root_len] != '\0') return false;

    return true;
}

/** True when "candidate" is "base" itself or lives below it. */
static bool is_same_or_descendant(const char *base, const char *candidate)
{
    size_t n = strlen(base);
    if (strncmp(base, candidate, n) != 0) return false;
    return candidate[n] == '/' || candidate[n] == '\0';
}

/** Bounded path join; avoids format-truncation warnings on long FAT LFN names. */
static bool join_path(char *dst, size_t dst_size, const char *a, const char *b)
{
    if (!dst || !a || !b || dst_size == 0) return false;
    size_t la = strlen(a);
    size_t lb = strlen(b);
    if (la >= dst_size || lb >= dst_size) return false;

    size_t sep = (la > 0 && a[la - 1] == '/') ? 0 : 1;
    if (la + sep + lb + 1 > dst_size) return false;

    memcpy(dst, a, la);
    if (sep) dst[la] = '/';
    memcpy(dst + la + sep, b, lb + 1);
    return true;
}

/** mkdir -p semantics, including verification that existing nodes are dirs. */
static esp_err_t mkdir_p(const char *path)
{
    char tmp[SD_TOOL_PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return ESP_ERR_INVALID_ARG;
    memcpy(tmp, path, len + 1);

    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }

    struct stat st;
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0775) != 0 && errno != EEXIST) return ESP_FAIL;
        if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) return ESP_FAIL;
        *p = '/';
    }

    if (mkdir(tmp, 0775) != 0 && errno != EEXIST) return ESP_FAIL;
    if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) return ESP_FAIL;
    return ESP_OK;
}

/** Create every missing parent of "path" (the final component is not created). */
static esp_err_t ensure_parent_dir(const char *path)
{
    size_t end = strlen(path);
    while (end > 0 && path[end - 1] == '/') end--;

    size_t slash = end;
    while (slash > 0 && path[slash - 1] != '/') slash--;

    size_t plen = slash ? slash : 1;   /* include the leading '/' */
    char parent[SD_TOOL_PATH_MAX];
    if (plen == 0 || plen >= sizeof(parent)) return ESP_ERR_INVALID_ARG;
    memcpy(parent, path, plen);
    parent[plen] = '\0';

    return mkdir_p(parent);
}

static const char *csaw_err_text(esp_err_t rc)
{
    switch (rc) {
        case ESP_OK:                   return "ok";
        case ESP_ERR_INVALID_ARG:      return "invalid argument";
        case ESP_ERR_NOT_FOUND:        return "not found";
        case ESP_ERR_INVALID_SIZE:     return "file exceeds size limit";
        case ESP_ERR_NOT_SUPPORTED:    return "unsupported file type";
        case ESP_ERR_NO_MEM:           return "out of memory";
        default:                       return "I/O error";
    }
}

typedef struct {
    int files;
    int dirs;
    size_t bytes;
} csaw_copy_stats_t;

typedef struct {
    int files;
    int dirs;
} csaw_delete_stats_t;

/** Byte-wise regular-file copy, capped at CSAW_TOOL_FILE_MAX_BYTES. */
static esp_err_t copy_file(const char *src, const char *dst, csaw_copy_stats_t *stats)
{
    struct stat st;
    if (stat(src, &st) != 0) return ESP_ERR_NOT_FOUND;
    if (!S_ISREG(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;
    if (st.st_size < 0 || (size_t)st.st_size > CSAW_TOOL_FILE_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *in = fopen(src, "rb");
    if (!in) return ESP_FAIL;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return ESP_FAIL;
    }

    char buf[SD_TOOL_COPY_BUF];
    size_t total = 0;
    esp_err_t rc = ESP_OK;

    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                rc = ESP_FAIL;
                break;
            }
            total += n;
            if (total > CSAW_TOOL_FILE_MAX_BYTES) {
                rc = ESP_ERR_INVALID_SIZE;
                break;
            }
        }
        if (n < sizeof(buf)) {
            if (ferror(in)) rc = ESP_FAIL;
            break;
        }
    }

    if (fclose(out) != 0 && rc == ESP_OK) rc = ESP_FAIL;
    fclose(in);

    if (rc != ESP_OK) {
        remove(dst);   /* never leave a truncated copy behind */
        return rc;
    }

    if (stats) {
        stats->files++;
        stats->bytes += total;
    }
    return ESP_OK;
}

/** Recursive copy: directories are recreated, files copied byte-wise. */
static esp_err_t copy_recursive(const char *src, const char *dst,
                                csaw_copy_stats_t *stats, int depth)
{
    if (depth > SD_TOOL_MAX_DEPTH) return ESP_ERR_INVALID_ARG;

    struct stat st;
    if (stat(src, &st) != 0) return ESP_ERR_NOT_FOUND;
    if (S_ISREG(st.st_mode)) return copy_file(src, dst, stats);
    if (!S_ISDIR(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;

    esp_err_t rc = mkdir_p(dst);
    if (rc != ESP_OK) return rc;
    if (stats) stats->dirs++;

    DIR *dir = opendir(src);
    if (!dir) return ESP_FAIL;

    struct dirent *ent;
    rc = ESP_OK;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        size_t s_need = strlen(src) + 1 + strlen(ent->d_name) + 1;
        size_t d_need = strlen(dst) + 1 + strlen(ent->d_name) + 1;
        char *child_src = malloc(s_need);
        char *child_dst = malloc(d_need);
        if (!child_src || !child_dst) {
            free(child_src);
            free(child_dst);
            rc = ESP_ERR_NO_MEM;
            break;
        }
        if (!join_path(child_src, s_need, src, ent->d_name) ||
            !join_path(child_dst, d_need, dst, ent->d_name)) {
            free(child_src);
            free(child_dst);
            rc = ESP_ERR_INVALID_ARG;
            break;
        }

        rc = copy_recursive(child_src, child_dst, stats, depth + 1);
        free(child_src);
        free(child_dst);
        if (rc != ESP_OK) break;
    }

    closedir(dir);
    return rc;
}

/** Recursive delete: children first, then rmdir on each directory. */
static esp_err_t delete_recursive(const char *path, csaw_delete_stats_t *stats, int depth)
{
    if (depth > SD_TOOL_MAX_DEPTH) return ESP_ERR_INVALID_ARG;

    struct stat st;
    if (stat(path, &st) != 0) return ESP_ERR_NOT_FOUND;

    if (S_ISREG(st.st_mode)) {
        if (remove(path) != 0) return ESP_FAIL;
        if (stats) stats->files++;
        return ESP_OK;
    }
    if (!S_ISDIR(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;

    DIR *dir = opendir(path);
    if (!dir) return ESP_FAIL;

    struct dirent *ent;
    esp_err_t rc = ESP_OK;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        size_t need = strlen(path) + 1 + strlen(ent->d_name) + 1;
        char *child = malloc(need);
        if (!child) {
            rc = ESP_ERR_NO_MEM;
            break;
        }
        if (!join_path(child, need, path, ent->d_name)) {
            free(child);
            rc = ESP_ERR_INVALID_ARG;
            break;
        }

        rc = delete_recursive(child, stats, depth + 1);
        free(child);
        if (rc != ESP_OK) break;
    }

    closedir(dir);
    if (rc != ESP_OK) return rc;

    if (rmdir(path) != 0) return ESP_FAIL;
    if (stats) stats->dirs++;
    return ESP_OK;
}

/* ── create_dir ─────────────────────────────────────────────── */

esp_err_t tool_create_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if (!validate_sd_root_path(path)) {
        snprintf(output, output_size,
                 "Error: path must be %s or below it and must not contain '..' (flash/NVS paths are forbidden)",
                 CSAW_SD_ROOT);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t rc = mkdir_p(path);
    if (rc != ESP_OK) {
        snprintf(output, output_size, "Error: cannot create directory %s", path);
        cJSON_Delete(root);
        return rc;
    }

    snprintf(output, output_size, "OK: created directory %s", path);
    ESP_LOGI(TAG, "create_dir: %s", path);
    csaw_storage_log(TAG, "create_dir %s", path);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── copy ───────────────────────────────────────────────────── */

esp_err_t tool_copy_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *src = cJSON_GetStringValue(cJSON_GetObjectItem(root, "src"));
    const char *dst = cJSON_GetStringValue(cJSON_GetObjectItem(root, "dst"));
    if (!validate_sd_root_path(src) || !validate_sd_root_path(dst)) {
        snprintf(output, output_size,
                 "Error: src and dst must be %s or below it and must not contain '..'", CSAW_SD_ROOT);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (is_same_or_descendant(src, dst)) {
        snprintf(output, output_size, "Error: destination %s is the same as or inside source %s", dst, src);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    csaw_copy_stats_t stats = {0};
    esp_err_t rc = copy_recursive(src, dst, &stats, 0);
    if (rc != ESP_OK) {
        snprintf(output, output_size, "Error: copy %s -> %s failed: %s", src, dst, csaw_err_text(rc));
        ESP_LOGE(TAG, "copy failed: %s -> %s (%s)", src, dst, csaw_err_text(rc));
        cJSON_Delete(root);
        return rc;
    }

    snprintf(output, output_size, "OK: copied %s -> %s (%d files, %d dirs, %d bytes)",
             src, dst, stats.files, stats.dirs, (int)stats.bytes);
    ESP_LOGI(TAG, "copy: %s -> %s", src, dst);
    csaw_storage_log(TAG, "copy %s -> %s (files=%d dirs=%d bytes=%d)",
                     src, dst, stats.files, stats.dirs, (int)stats.bytes);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── move / rename ──────────────────────────────────────────── */

esp_err_t tool_move_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *src = cJSON_GetStringValue(cJSON_GetObjectItem(root, "src"));
    const char *dst = cJSON_GetStringValue(cJSON_GetObjectItem(root, "dst"));
    if (!validate_sd_root_path(src) || !validate_sd_root_path(dst)) {
        snprintf(output, output_size,
                 "Error: src and dst must be %s or below it and must not contain '..'", CSAW_SD_ROOT);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (is_same_or_descendant(src, dst)) {
        snprintf(output, output_size, "Error: destination %s is the same as or inside source %s", dst, src);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t rc = ensure_parent_dir(dst);
    if (rc != ESP_OK) {
        snprintf(output, output_size, "Error: cannot create parent directory for %s", dst);
        cJSON_Delete(root);
        return rc;
    }

    if (rename(src, dst) == 0) {
        snprintf(output, output_size, "OK: moved %s -> %s", src, dst);
        ESP_LOGI(TAG, "move: %s -> %s (rename)", src, dst);
        csaw_storage_log(TAG, "move %s -> %s (rename)", src, dst);
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (errno != EXDEV) {
        snprintf(output, output_size, "Error: rename %s -> %s failed: %s", src, dst, strerror(errno));
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    /* Cross-device fallback: copy the whole tree, then remove the source. */
    csaw_copy_stats_t copy_stats = {0};
    rc = copy_recursive(src, dst, &copy_stats, 0);
    if (rc != ESP_OK) {
        snprintf(output, output_size, "Error: cross-device move %s -> %s failed during copy: %s",
                 src, dst, csaw_err_text(rc));
        cJSON_Delete(root);
        return rc;
    }

    csaw_delete_stats_t del_stats = {0};
    rc = delete_recursive(src, &del_stats, 0);
    if (rc != ESP_OK) {
        snprintf(output, output_size,
                 "Error: copied %s -> %s but failed to remove source: %s", src, dst, csaw_err_text(rc));
        cJSON_Delete(root);
        return rc;
    }

    snprintf(output, output_size, "OK: moved %s -> %s (copy+delete, %d files, %d bytes)",
             src, dst, copy_stats.files, (int)copy_stats.bytes);
    ESP_LOGI(TAG, "move: %s -> %s (copy+delete)", src, dst);
    csaw_storage_log(TAG, "move %s -> %s (copy+delete files=%d bytes=%d)",
                     src, dst, copy_stats.files, (int)copy_stats.bytes);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── delete ─────────────────────────────────────────────────── */

esp_err_t tool_delete_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if (!validate_sd_root_path(path)) {
        snprintf(output, output_size,
                 "Error: path must be %s or below it and must not contain '..' (flash/NVS paths are forbidden)",
                 CSAW_SD_ROOT);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(path, CSAW_SD_ROOT) == 0 || strcmp(path, CSAW_SD_BASE) == 0) {
        snprintf(output, output_size, "Error: refusing to delete the SD workspace root %s", CSAW_SD_ROOT);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    csaw_delete_stats_t stats = {0};
    esp_err_t rc = delete_recursive(path, &stats, 0);
    if (rc != ESP_OK) {
        snprintf(output, output_size, "Error: delete %s failed: %s", path, csaw_err_text(rc));
        ESP_LOGE(TAG, "delete failed: %s (%s)", path, csaw_err_text(rc));
        cJSON_Delete(root);
        return rc;
    }

    snprintf(output, output_size, "OK: deleted %s (%d files, %d dirs)", path, stats.files, stats.dirs);
    ESP_LOGI(TAG, "delete: %s", path);
    csaw_storage_log(TAG, "delete %s (files=%d dirs=%d)", path, stats.files, stats.dirs);
    cJSON_Delete(root);
    return ESP_OK;
}
