/*
 * CsAW on-screen chat UI (LVGL v9) for SH8601 + FT3168 Rev 1 boards.
 * This is the primary interaction channel; Telegram/Feishu are legacy-only.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Callback used by the chat screen to submit a user message to the agent bus. */
typedef void (*csaw_ui_send_cb)(const char *text);

/** Create display, touch input device, chat screen and on-screen keyboard. */
esp_err_t csaw_ui_init(void);

/** Register the bridge from the UI to message_bus (installed by main). */
void csaw_ui_set_send_cb(csaw_ui_send_cb cb);

/** Render an assistant reply from the outbound message bus. */
esp_err_t csaw_ui_show_response(const char *text);

/** Render a local status/system line. */
esp_err_t csaw_ui_show_status(const char *text);

/** Return true once the LVGL UI is ready. */
bool csaw_ui_ready(void);

#ifdef __cplusplus
}
#endif
