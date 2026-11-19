# Free Provider Configuration Inputs

Authoritative sources retrieved during preflight:

- Kilo free usage: https://kilo.ai/docs/getting-started/using-kilo-for-free
- Kilo Gateway auth: https://kilo.ai/docs/gateway/authentication
- Kilo Gateway models/routing: https://kilo.ai/docs/gateway/models-and-providers
- OpenCode console models / free models: https://opencode.ai/docs/console/models

## Kilo free router — DEFAULT

| Field | Value |
| --- | --- |
| Gateway base URL | `https://ai-gateway.kilo.ai/api/v1` |
| Chat endpoint | `https://ai-gateway.kilo.ai/api/v1/chat/completions` |
| Model catalog | `GET https://ai-gateway.kilo.ai/api/v1/models` (no auth required) |
| Default free model ID | `kilo-auto/free` (Auto Free — routes to best available free models) |
| Individual free models | IDs tagged `:free` (e.g. `minimax/minimax-m2.1:free`, `z-ai/glm-5:free`) |
| Auth | `Authorization: Bearer <KILO_API_KEY>`; **anonymous access allowed for free models only** |
| Anonymous limit | 200 requests/hour per IP |
| Optional headers | `x-kilocode-mode`, `X-KiloCode-TaskId`, `X-KiloCode-Version`, `X-KiloCode-OrganizationId` |
| Data policy caveat | Auto Free may route to providers that log prompts/outputs (including NVIDIA free endpoints). Do not send confidential data. |

## OpenCode — first fallback

| Field | Value |
| --- | --- |
| Models endpoint | `https://opencode.ai/inference/v1/models` |
| Console/models docs | https://opencode.ai/docs/console/models |
| Free models | Free-tier chat models require OpenCode; examples listed in docs: DeepSeek V4.1 Flash Free, MiMo-V2.6-Flash Free, Nemotron 3.5 Lightning Free, Hy4 flash Free, Jev 1.13 Free |
| Opt-out flag | `OPENCODE_DISABLE_FREE_MODELS=1` hides free models (do not set for CsAW) |
| Auth | Console API key (optional for free-tier use subject to current terms) |

## Additional fallbacks (recorded, not yet configured)

- OpenRouter free models: https://openrouter.ai/blog/tutorials/kilo-code-openrouter — free tier 50 requests/day, 20/min without credits; `data_collection: "deny"` recommended for private code.

## Implementation constraints

- Fallback order: Kilo (`kilo-auto/free`) → OpenCode (`mimo-v2.6-flash-free`) → OpenRouter (`inclusionai/ling-3.2-flash:free`).
- Advance chain on transport failure, rate-limit (HTTP 429), auth/model rejection (401/403/404), or server error (5xx).
- No paid-key requirement at runtime; do not route free requests to paid model IDs.
- Secrets never stored in source or in this document: use flash/NVS per AGENTS.md Section 7.
- Free-model catalogs change over time; re-check `GET /models` at runtime/config time.

## Implemented abstraction (WP2)

`main/llm/provider_chain.{c,h}` holds three free-only provider descriptors with runtime-overridable chat URL, models URL, model and token. `main/llm/llm_proxy.c` speaks one chat-completions wire format to every entry and loops over the ordered chain, advancing on failure. The active entry changes **in memory only** on automatic fallback (no flash wear); manual `set_model_provider` persists it. No legacy paid-provider request builder or header remains.

OpenCode's verified chat-completions base URL is `https://opencode.ai/inference/openai/v1` (the `/openai/v1` path segment is the router's wire-compatibility namespace, not a provider choice), while its model catalog is `https://opencode.ai/inference/v1/models`. These values were confirmed against the live catalog on 2026-11-18.

`docs/ARCHITECTURE.md` and the CLI/onboarding UI name only these providers. `provider_status`, `set_provider_chain`, `set_provider_model`, `set_provider_url` and `set_provider_key` are the runtime controls; see `config/providers.md`.
