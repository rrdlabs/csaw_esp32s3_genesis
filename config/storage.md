# Storage Policy Records (preflight + WP4 implementation)

| Store | Contents | Notes |
| --- | --- | --- |
| Flash / NVS / SPIFFS | long-term context/memory, config/device settings, provider secrets/API keys | low-churn only; inherited SPIFFS base `/spiffs` |
| 1-bit SDMMC TF card (8 GB) | conversation history, session/working memory, cached learning/knowledge, logs, temp artifacts, agent file tools, MicroPython scripts | mounted by the HAL at `/sd`; implemented schema and limits in [docs/STORAGE_POLICY.md](../docs/STORAGE_POLICY.md) |

Implemented SD paths:

```text
/sd/csaw/{sessions,context,daily,cache,logs,temp,python,tools}
```

Constraints (all enforced in code):

- Never write high-churn session data to flash; `session_append()` writes `/sd/csaw/sessions/*.jsonl` only.
- Daily notes, logs and temp artifacts are SD-only and rotated (512 KiB sessions ×2, 64 KiB daily ×1, 256 KiB log ×3).
- SD filesystem tools accept only `/sd/...` paths and explicitly reject `/spiffs`; flash writes through tools are forbidden.
- MicroPython: on-demand only, never on boot; write access SD-only, flash read-only/off-limits (WP7).

Preflight source basis: Waveshare/Espressif SDMMC reference recorded in `docs/HARDWARE_SOURCES.md`; policy requirements from workspace `AGENTS.md` §7.
