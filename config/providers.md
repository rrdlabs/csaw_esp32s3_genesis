# Provider Configuration Records (preflight)

Full evidence: `docs/PROVIDER_CONFIG.md`.

| Variable | Default | Storage |
| --- | --- | --- |
| `CSAW_PROVIDER_CHAIN` | `kilo,opencode,openrouter` | flash/NVS (config) |
| `CSAW_PROVIDER_DEFAULT` | `kilo` | flash/NVS (config) |
| `CSAW_PROVIDER_FALLBACK_1` | `opencode` | flash/NVS (config) |
| `CSAW_PROVIDER_FALLBACK_2` | `openrouter` | flash/NVS (config) |
| `CSAW_KILO_BASE_URL` | `https://ai-gateway.kilo.ai/api/v1` | flash/NVS (config) |
| `CSAW_KILO_MODEL` | `kilo-auto/free` | flash/NVS (config) |
| `CSAW_KILO_API_KEY` | empty (anonymous free models allowed) | flash/NVS (secret) |
| `CSAW_OPENCODE_MODELS_URL` | `https://opencode.ai/inference/v1/models` | flash/NVS (config) |
| `CSAW_OPENCODE_MODEL` | select from live free-model list | flash/NVS (config) |
| `CSAW_OPENCODE_API_KEY` | empty | flash/NVS (secret) |
| `CSAW_FALLBACK_ON` | `error,429,rate_limit,model_unavailable` | flash/NVS (config) |

Rules: never store API keys in source, sdcard context, or markdown; rotate secrets via CLI/NVS; re-fetch free-model catalogs because availability changes.

## Implemented runtime configuration (WP2)

| CLI command | Effect | Storage |
| --- | --- | --- |
| `provider_status` | Show order, active entry, models, endpoints, key status | read-only |
| `set_model_provider kilo\|opencode\|openrouter` | Select active provider | flash/NVS `active` |
| `set_provider_chain kilo,opencode,openrouter` | Reorder/enable chain | flash/NVS `chain` |
| `set_provider_model <provider> <model>` | Set that provider's free model | flash/NVS `<prefix>_model` |
| `set_provider_url <provider> <https-url>` | Override chat endpoint | flash/NVS `<prefix>_url` |
| `set_provider_key <provider> <token>` | Set optional token | flash/NVS `<prefix>_key` |

NVS prefixes: `kilo`, `ocode`, `oroute` (ESP-IDF limits keys to 15 chars). Fallback state is **in-memory only** — high-churn failover never writes flash. Free models verified 2026-11-18: Kilo `kilo-auto/free`, OpenCode `mimo-v2.6-flash-free`, OpenRouter `inclusionai/ling-3.2-flash:free`. OpenCode's official chat path is `https://opencode.ai/inference/openai/v1/chat/completions` (`/openai/v1` is that router's wire-compatibility segment, not a provider selection).
