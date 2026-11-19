# CsAW On-Device Chat UI (WP3)

**Status:** implemented in this revision; IDF 6.1 build verification recorded in [CHANGELOG.md](../CHANGELOG.md).

The primary user interface is the **on-screen keyboard and chat screen** rendered on the SH8601 AMOLED (368×448 RGB565) of the Rev 1 board. Serial/CLI is diagnostics-only; Telegram and Feishu are optional legacy channels and are inert by default (AGENTS.md §6).

## Components and sources

| Item | Value / source |
| --- | --- |
| Graphics library | LVGL **v9.6.0**, vendored as `components/lvgl/` from preflight stage `third_party/lvgl` (local build, no download at build time) |
| Configuration | `components/lvgl/lv_conf.h` (template enabled; widgets/keys/text area/labels/flex/SW renderer on) |
| Display | `csaw_hal_display_panel_create()` in `components/csaw_hal/csaw_hal.c` — native `esp_lcd_panel_t` over the QSPI panel IO |
| Touch | FT3168 at I²C `0x38`; vendor FT3x68 register decode from Waveshare `Arduino_FT3x68.cpp` (see [HARDWARE_SOURCES.md](HARDWARE_SOURCES.md)) |
| UI component | `components/csaw_ui/` (`csaw_ui.c`, `include/csaw_ui.h`) |
| Bus integration | `main/csaw.c`, `main/bus/message_bus.h` (`CSAW_CHAN_UI "ui"`) |

## Boot order and brightness safety

The exact hardware sequence is unchanged and completes before any UI object is created:

```
I2C0 -> IO Expander (rails) -> PMU (AXP2101) -> SH8601 (QSPI register table) -> brightness >= 30% -> LVGL panel handle
```

`csaw_hal_init()` asserts the brightness gate (`BRIGHTNESS_GATE OK: 30% asserted before UI draw`) before `csaw_ui_init()` is called in `app_main()`. The UI is also initialized **before** WiFi onboarding so the panel is never black while waiting for a network.

## Rendering and input details

- Two partial-render draw buffers, each `368 × 24` RGB565 pixels, allocated from **PSRAM** (`MALLOC_CAP_SPIRAM`).
- `esp_timer` 1 ms periodic tick drives `lv_tick_inc(1)`; a UI task pinned to core 1 calls `lv_timer_handler()` every 5 ms.
- Flush callback writes the dirty rectangle with SH8601 commands `0x2A` (column), `0x2B` (row), `0x2C` (RGB565 payload) through the native `esp_lcd_panel_draw_bitmap()` path.
- FT3168 single-touch read: register `0x02` = finger count; `0x03/0x04` = X high/low; `0x05/0x06` = Y high/low; high byte masked with `0x0F` per vendor driver. Reports `LV_INDEV_STATE_PRESSED/RELEASED` to the LVGL pointer device.

## Chat screen

- Header: `CsAW - On-device Agent`, plus a status line used for readiness and "working" indications.
- Scrollable message label with a bounded 6 KB history buffer (appending stops when the buffer is full; WP4 moves this history to the SD card).
- One-line text area; tapping it (or focusing it with the keyboard group) reveals the LVGL keyboard on `lv_layer_top()`.
- `LV_EVENT_READY` (Enter / keyboard OK) submits the text, clears the field, hides the keyboard and returns focus to the message view. `LV_EVENT_CANCEL` hides the keyboard without sending.

## Agent-loop wiring (same bus, no parallel path)

```
keyboard READY
  -> submit_text()
  -> csaw_ui_set_send_cb(ui_send_bridge)
  -> message_bus_push_inbound(channel="ui", chat_id="local")
  -> agent_loop (LLM provider chain + tools + session memory)
  -> message_bus_push_outbound(channel="ui", chat_id="local")
  -> outbound_dispatch_task -> csaw_ui_show_response() -> chat label
```

Working-status messages emitted by the agent loop also carry the `"ui"` channel and are rendered as local status text. Conversation history for `chat_id="local"` is written by the existing session manager; the WP4 storage split moves high-churn session files to the SD card without changing this UI path. Voice routes under development feed the same inbound bus and therefore share this chat view.

## Optional legacy channels

Telegram and Feishu code remains in the tree for compatibility but is **disabled by default** (`CSAW_LEGACY_TELEGRAM_ENABLED 0`, `CSAW_LEGACY_FEISHU_ENABLED 0` in `main/csaw_config.h`):

- `telegram_bot_init/start` and `feishu_bot_init/start` are skipped unless opted in.
- Outbound replies addressed to those channels are dropped with a warning while disabled.
- The WiFi admin/onboarding page labels both sections "legacy / optional - disabled by default".
- Runtime opt-in is persisted in flash/NVS (namespace `legacy_config`):

```text
csaw> legacy_channel_set telegram 1     # enable (requires reboot)
csaw> legacy_channel_set feishu 1       # enable (requires reboot)
csaw> legacy_channel_set telegram 0     # back to inert default
```
