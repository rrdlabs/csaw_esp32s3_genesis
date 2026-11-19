# CsAW Storage Policy (WP4, AGENTS.md §7)

**Goal:** minimize flash wear. Flash/NVS/SPIFFS holds only low-churn state; the installed 8 GB 1-bit SDMMC TF card holds every high-churn artifact. The policy is enforced in `main/storage/csaw_storage.{c,h}` and applied by the memory, session and file-tool modules.

## What lives where

| Store | Contents | Examples |
| --- | --- | --- |
| Flash / NVS | device settings and provider secrets | WiFi credentials (`wifi_config`), provider tokens/URLs/models (`llm_config`), proxy/search keys, legacy-channel opt-ins (`legacy_config`) |
| Flash / SPIFFS | long-term memory and read-only config | `/spiffs/memory/MEMORY.md`, `/spiffs/config/SOUL.md`, `/spiffs/config/USER.md`, `/spiffs/skills/*.md`, `/spiffs/cron.json` |
| SD card (`/sd`) | all other context and memory | conversation history, sessions, daily/working memory, cache, logs, temp artifacts, agent file-tool data, MicroPython scripts |

`csaw_storage_init()` runs in `app_main()` immediately after the SPIFFS mount and before any memory/session/tool write. It creates the SD path schema and logs the split. A missing card is logged as an error; high-churn writes are then refused rather than silently redirected to flash.

## SD path schema

```text
/sd/
└── csaw/
    ├── sessions/    conversation history (*.jsonl, one file per chat_id)
    ├── context/     working memory (HEARTBEAT.md)
    │   └── daily/   daily notes (YYYY-MM-DD.md)
    ├── cache/       cached learning/knowledge
    ├── logs/        csaw.log + rotated generations (.1, .2, .3)
    ├── temp/        temporary artifacts
    ├── python/      on-demand MicroPython scripts (WP7)
    └── tools/       agent-managed files (WP6)
```

The chat id used in a session filename is sanitized to `[A-Za-z0-9_-]`, so external channel identifiers cannot escape the `sessions/` directory. The on-screen UI uses `chat_id = "local"`, giving `/sd/csaw/sessions/local.jsonl`.

## Size and rotation limits

Defined in `main/csaw_config.h`:

| Data | Limit | Rotation / retention |
| --- | --- | --- |
| Per-session JSONL | 512 KiB | 2 rotated generations (`.1`, `.2`); at most 32 session files |
| Daily note | 64 KiB | 1 rotated generation; at most 14 daily files |
| `csaw.log` | 256 KiB | 3 rotated generations (`.1` … `.3`) |
| Long-term `MEMORY.md` (flash) | 32 KiB | write refused above the cap |
| Agent `write_file` / `edit_file` | 512 KiB per file | caller sees an explicit error above the cap |
| Cache / temp / tools | bounded by per-file tool cap; `cache` reserved 32 files in `status` output | files pruned by the owning module |

Rotation is triggered when `current_size + new_bytes > limit`: `path.N-1 → path.N` (oldest generation removed at `N = max`), then `path → path.1`, and the new data is appended to a fresh `path`. `csaw_storage_prune()` deletes the oldest files (by mtime) once a directory exceeds its file-count cap.

## Enforcement points

- `main/memory/session_mgr.c` — `session_append()` writes only to `/sd/csaw/sessions/` using `csaw_storage_append_rotated()`; `session_clear()`/`session_list()` operate on the SD directory.
- `main/memory/memory_store.c` — long-term memory stays in flash with a 32 KiB cap; `memory_append_today()` and `memory_read_recent()` use `/sd/csaw/context/daily/`.
- `main/tools/tool_files.c` — `read_file`, `write_file`, `edit_file`, `list_dir` accept only paths under `/sd/` and explicitly reject `/spiffs` paths. File writes are logged to the SD log.
- `main/heartbeat/heartbeat.c` — uses `CSAW_HEARTBEAT_FILE` at `/sd/csaw/context/HEARTBEAT.md`.
- `main/agent/context_builder.c` — system prompt tells the agent which memory is flash vs SD and which paths tools may use.
- `main/csaw.c` — initializes storage before high-churn consumers.

Cron job configuration (`/spiffs/cron.json`), skills and device settings are intentionally low-churn and remain in flash.

## Diagnostics

```text
csaw> storage_status
SD /sd/csaw: READY
Flash(NVS/SPIFFS): config, long-term memory, secrets
  sessions     2 files,    1234 bytes
  context      1 files,     256 bytes
  ...
Limits: session 512 KiB x2 rot (32 files), daily 64 KiB x1 rot (14 days), log 256 KiB x3 rot
csaw> session_list       # lists SD session files
csaw> session_clear local
```

Flash wear verification: no code path in `main/memory/`, `main/tools/` or the agent loop writes session JSONL, daily notes, cache, logs or temp artifacts under `/spiffs`. The only flash writes remaining are configuration (NVS), long-term memory, cron config and shipped skills.

## Migration note

The donor stored sessions at `/spiffs/sessions/` and daily notes at `/spiffs/memory/`. Those old files are deliberately not migrated: they are low-value reference data, and copying them would write high-churn data into flash again. A fresh install starts with clean SD directories.
