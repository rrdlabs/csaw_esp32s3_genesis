/*
 * Optional legacy IM channels (Telegram / Feishu).
 *
 * AGENTS.md 6: these are NOT the default UX. Both are inert unless explicitly
 * enabled at build time or via the serial CLI, which stores the opt-in in
 * flash/NVS. The on-screen keyboard/chat UI is always the primary channel.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** True only when the named legacy channel is explicitly opted in. */
bool csaw_legacy_channel_enabled(const char *id);

/** Persist an opt-in/opt-out for "telegram" or "feishu" in flash/NVS. */
esp_err_t csaw_legacy_channel_set(const char *id, bool enabled);

#ifdef __cplusplus
}
#endif
