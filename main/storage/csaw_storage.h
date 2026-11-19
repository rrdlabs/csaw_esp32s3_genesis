/*
 * CsAW storage policy (AGENTS.md 7).
 *
 * Flash/NVS/SPIFFS: long-term memory, config/device settings, provider secrets.
 * SDMMC card (/sd): sessions, working/daily memory, cache, logs, temp files,
 *                   agent file-tool data and MicroPython scripts.
 *
 * All high-churn writes go through this module so rotation and size limits are
 * enforced in one place and no session data can leak into flash.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create the /sd/csaw path schema and verify the SD mount. */
esp_err_t csaw_storage_init(void);

/** True after csaw_storage_init() has verified and prepared the SD card. */
bool csaw_storage_sd_available(void);

/** Recursively create a directory on the SD card. */
esp_err_t csaw_storage_ensure_dir(const char *path);

/**
 * Append to a file with bounded size and rotation:
 * path -> path.1 -> path.2 ... path.N (oldest copy discarded).
 */
esp_err_t csaw_storage_append_rotated(const char *path, const char *data,
                                      size_t len, size_t max_bytes, int rotations);

/** Write a complete file, refusing content larger than max_bytes. */
esp_err_t csaw_storage_write_bounded(const char *path, const char *data,
                                     size_t len, size_t max_bytes);

/** Keep at most max_files entries with the given prefix, deleting oldest first. */
esp_err_t csaw_storage_prune(const char *dir, const char *prefix, int max_files);

/** Append a timestamped line to /sd/csaw/logs/csaw.log (rotated). */
void csaw_storage_log(const char *tag, const char *fmt, ...);

/** Human-readable SD usage/limits summary for CLI diagnostics. */
esp_err_t csaw_storage_status(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
