# CsAW SD-Card Filesystem Tools (AGENTS.md §9)

**Status:** implemented in revision WP6-yolo-tools (2026-11-18).
**Owner files:** `main/tools/tool_files.{c,h}`, `main/tools/tool_registry.c`.

These are "yolo" tools: they perform filesystem mutations **without any
confirmation prompt**. The safety guarantee is not a prompt — it is strict
path validation plus a narrow writable workspace on the SD card.

## 1. Writable boundary

| Item | Value |
| --- | --- |
| SD mount (from `csaw_config.h`) | `CSAW_SD_BASE` = `/sd` |
| Agent workspace / safety base | `CSAW_SD_ROOT` = `/sd/csaw` |
| Per-file copy cap | `CSAW_TOOL_FILE_MAX_BYTES` = 512 KiB |
| Max recursion depth | 32 levels |
| Audit sink | `csaw_storage_log()` → `/sd/csaw/logs/csaw.log` |

Every argument accepted by the four tools below passes through
`validate_sd_root_path()` **before any filesystem call**. A path is accepted
only when it is `/sd/csaw` itself or starts with `/sd/csaw/`. Explicitly
rejected:

- `/spiffs…`, `/flash…`, `/nvs…` (and anything else not under `/sd/csaw`);
- bare `/sd` locations such as `/sd/notes.txt` — the existing `read_file`,
  `write_file`, `edit_file` and `list_dir` tools still accept `/sd/…`, but the
  unrestricted tools are deliberately narrowed to `/sd/csaw`;
- any path containing `..` (traversal), including `/sd/csaw/../spiffs/x`;
- a destination equal to, or inside, the copy/move source (prevents infinite
  recursive copies and self-truncation);
- deleting the workspace root `/sd/csaw` itself (protects the storage schema).

`CSAW_SD_BASE` is used to pin the physical mount before the `CSAW_SD_ROOT`
check, so a path like `/sd/csawx/foo` cannot pass by prefix accident.

No code path in these tools can write to flash/NVS/SPIFFS. The only flash
write surface in the project remains configuration, long-term memory, cron
config and shipped skills (see `STORAGE_POLICY.md`).

## 2. Tool signatures

All tools follow the existing convention: `esp_err_t execute(const char
*input_json, char *output, size_t output_size)`. Errors are returned as plain
text strings beginning with `Error:` (the same convention as `read_file` and
`write_file`), with a non-`ESP_OK` return code.

| Tool | Input JSON | Behaviour |
| --- | --- | --- |
| `create_dir` | `{"path": "<CSAW_SD_ROOT>/..."}` | `mkdir -p`: creates every missing parent, verifies existing nodes are directories. |
| `copy` | `{"src": "<CSAW_SD_ROOT>/...", "dst": "<CSAW_SD_ROOT>/..."}` | Byte-wise file copy, or recursive directory-tree copy (files + subdirs). Parent dirs of `dst` are created. Files over 512 KiB are refused; partial copies are removed. |
| `move` | `{"src": "<CSAW_SD_ROOT>/...", "dst": "<CSAW_SD_ROOT>/..."}` | Rename in place; on `EXDEV` falls back to recursive copy followed by recursive delete of `src`. Parent dirs of `dst` are created. |
| `delete` | `{"path": "<CSAW_SD_ROOT>/..."}` | Recursive delete (children first, then `rmdir`). Refuses `/sd/csaw` itself. |

Return codes used: `ESP_OK`, `ESP_ERR_INVALID_ARG` (validation / same-or-child
paths / protected root), `ESP_ERR_NOT_FOUND`, `ESP_ERR_INVALID_SIZE` (over the
copy cap), `ESP_ERR_NOT_SUPPORTED` (non-file/non-dir nodes), `ESP_ERR_NO_MEM`,
and `ESP_FAIL` for generic I/O failures.

Every successful mutation emits a structured audit line through
`csaw_storage_log()`, e.g. `copy /sd/csaw/tools/a -> /sd/csaw/tools/b
(files=3 dirs=1 bytes=2048)`. Serial/console logging uses `ESP_LOGI`/`ESP_LOGE`
with the `tool_files` tag.

## 3. Examples

Agent tool calls (input JSON → typical output text):

```text
create_dir  {"path":"/sd/csaw/tools/notes/2026"}
            -> OK: created directory /sd/csaw/tools/notes/2026

copy        {"src":"/sd/csaw/tools/notes","dst":"/sd/csaw/tools/notes_backup"}
            -> OK: copied /sd/csaw/tools/notes -> /sd/csaw/tools/notes_backup (5 files, 2 dirs, 12480 bytes)

move        {"src":"/sd/csaw/temp/draft.md","dst":"/sd/csaw/tools/notes/2026/draft.md"}
            -> OK: moved /sd/csaw/temp/draft.md -> /sd/csaw/tools/notes/2026/draft.md

delete      {"path":"/sd/csaw/temp/draft.md"}
            -> OK: deleted /sd/csaw/temp/draft.md (1 files, 0 dirs)
```

Rejected examples (no filesystem access occurs):

```text
delete      {"path":"/spiffs/memory/MEMORY.md"}
            -> Error: path must be /sd/csaw or below it and must not contain '..' (flash/NVS paths are forbidden)

copy        {"src":"/sd/csaw/tools","dst":"/sd/csaw/tools/inner"}
            -> Error: destination /sd/csaw/tools/inner is the same as or inside source /sd/csaw/tools

delete      {"path":"/sd/csaw"}
            -> Error: refusing to delete the SD workspace root /sd/csaw

move        {"src":"/sd/csaw/a.txt","dst":"/sd/other/a.txt"}
            -> Error: src and dst must be /sd/csaw or below it and must not contain '..'
```

## 4. Verification notes

Performed by the WP6-yolo-tools change owner (parent agent owns firmware
builds and `idf.py build`; this change did not invoke a build):

- `MAX_TOOLS` raised from 16 to 24; 12 pre-existing tools plus 4 new tools are
  registered in `tool_registry_init()` with matching names, descriptions and
  `input_schema_json` objects.
- Source-level boundary check (no literal flash paths in new code and no
  traversal-capable branching):
  `grep -nE 'validate_sd_root_path|CSAW_SD_ROOT|csaw_storage_log|strstr\(path, "\.\."\)' main/tools/tool_files.c`
- Branding check (zero matches expected; character classes keep the
  pattern itself from matching the documentation line):
  `grep -rniE 'm[i]mi|don[o]r' main/tools/tool_files.c main/tools/tool_files.h main/tools/tool_registry.c docs/SD_CARD_TOOLS.md`
- C compatibility: ESP-IDF 6.1 newlib/POSIX VFS APIs only (`mkdir`, `rmdir`,
  `rename`, `stat`, `opendir`/`readdir`, `fopen`/`fread`/`fwrite`, `cJSON`).
  Path composition uses length-checked `memcpy` via `join_path()` rather than
  `snprintf("%s", long_name)` into fixed buffers, avoiding
  `-Werror=format-truncation` with FAT long filenames.
- Runtime verification (to be performed by the parent agent on hardware):
  mount an SD card, exercise all four tools from the on-device chat/CLI, then
  confirm `type /sd/csaw/logs/csaw.log` contains the corresponding audit lines
  and that `/spiffs` is unchanged.
