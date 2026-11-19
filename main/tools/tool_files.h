#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Read a file from the SD card.
 * Input JSON: {"path": "<CSAW_SD_BASE>/..."}
 */
esp_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * Write/overwrite a file on the SD card.
 * Input JSON: {"path": "<CSAW_SD_BASE>/...", "content": "..."}
 */
esp_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * Find-and-replace edit a file on the SD card.
 * Input JSON: {"path": "<CSAW_SD_BASE>/...", "old_string": "...", "new_string": "..."}
 */
esp_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * List files on the SD card, optionally filtered by path prefix.
 * Input JSON: {"prefix": "<CSAW_SD_BASE>/..."} (prefix is optional)
 */
esp_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size);

/*
 * Unrestricted, no-confirmation filesystem tools (AGENTS.md §9).
 *
 * Unlike the four tools above, these are confined to CSAW_SD_ROOT
 * ("/sd/csaw"): flash/NVS/SPIFFS paths, bare "/sd" locations, ".."
 * traversal and destinations outside the SD workspace are rejected.
 * Every mutation is written to the csaw_storage_log() audit trail.
 */

/**
 * Create a directory and all missing parents (mkdir -p semantics).
 * Input JSON: {"path": "<CSAW_SD_ROOT>/..."}
 */
esp_err_t tool_create_dir_execute(const char *input_json, char *output, size_t output_size);

/**
 * Copy a file (byte-wise, capped at CSAW_TOOL_FILE_MAX_BYTES) or a whole
 * directory tree recursively from src to dst.
 * Input JSON: {"src": "<CSAW_SD_ROOT>/...", "dst": "<CSAW_SD_ROOT>/..."}
 */
esp_err_t tool_copy_execute(const char *input_json, char *output, size_t output_size);

/**
 * Move or rename a file or directory; uses rename() with a copy+delete
 * fallback on cross-device (EXDEV) moves. Parent dirs of dst are created.
 * Input JSON: {"src": "<CSAW_SD_ROOT>/...", "dst": "<CSAW_SD_ROOT>/..."}
 */
esp_err_t tool_move_execute(const char *input_json, char *output, size_t output_size);

/**
 * Recursively delete a file or directory. The workspace root itself is
 * protected and cannot be removed.
 * Input JSON: {"path": "<CSAW_SD_ROOT>/..."}
 */
esp_err_t tool_delete_execute(const char *input_json, char *output, size_t output_size);
