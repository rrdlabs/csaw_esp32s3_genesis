/*
 * CsAW on-screen chat UI — LVGL v9 on SH8601 (368x448) with FT3168 touch.
 * Primary UX per AGENTS.md 6: keyboard + chat feed the same message bus and
 * agent loop as every other route. Serial/CLI is diagnostics only.
 */
#include "csaw_ui.h"
#include "csaw_hal.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"

static const char *TAG = "csaw_ui";

#define UI_WIDTH                CSAW_LCD_H_RES
#define UI_HEIGHT               CSAW_LCD_V_RES
#define UI_BUF_LINES            24
#define UI_HISTORY_MAX          6000
#define UI_TASK_STACK           (12 * 1024)
#define UI_TASK_PRIO            4

static lv_display_t *s_disp;
static lv_indev_t *s_indev;
static lv_group_t *s_group;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_textarea;
static lv_obj_t *s_messages;
static lv_obj_t *s_messages_box;
static lv_obj_t *s_status;
static esp_timer_handle_t s_tick_timer;
static SemaphoreHandle_t s_lock;
static csaw_ui_send_cb s_send_cb;
static char s_history[UI_HISTORY_MAX];
static bool s_ready;

static void ui_lock(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void ui_unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* ---- Display flush (SH8601 native panel handle from csaw_hal) ---- */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    if (panel) {
        esp_lcd_panel_draw_bitmap(panel,
                                  area->x1, area->y1,
                                  area->x2 + 1, area->y2 + 1,
                                  px_map);
    }
    lv_display_flush_ready(disp);
}

/* ---- FT3168 touch input ---- */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x = 0, y = 0;
    bool pressed = false;
    if (csaw_hal_touch_read(&x, &y, &pressed) == ESP_OK) {
        data->point.x = x;
        data->point.y = y;
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ---- Chat screen ---- */

static void history_append(const char *prefix, const char *text)
{
    if (!s_messages || !text) return;
    size_t used = strlen(s_history);
    if (used + 1 < sizeof(s_history)) {
        int n = snprintf(s_history + used, sizeof(s_history) - used, "%s%s\n",
                         prefix ? prefix : "", text);
        if (n < 0) return;
    }
    lv_label_set_text(s_messages, s_history);
    lv_obj_scroll_to_y(s_messages_box, LV_COORD_MAX, LV_ANIM_OFF);
}

static void submit_text(const char *text)
{
    if (!text || !text[0]) return;
    history_append("You: ", text);
    if (s_send_cb) {
        s_send_cb(text);
    } else {
        history_append("CsAW: ", "Message bus bridge is not installed yet.");
    }
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *text = lv_textarea_get_text(s_textarea);
        if (text && text[0]) {
            submit_text(text);
            lv_textarea_set_text(s_textarea, "");
        }
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_group_focus_obj(s_messages_box);
    } else if (code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void textarea_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_keyboard, s_textarea);
    } else if (code == LV_EVENT_READY) {
        keyboard_event_cb(e);
    }
}

static void build_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B0E14), 0);
    lv_obj_set_style_pad_all(scr, 6, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CsAW  -  On-device Agent");
    lv_obj_set_width(title, UI_WIDTH - 16);
    lv_obj_set_style_text_color(title, lv_color_hex(0x66E0C8), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "Ready - tap the input field to type");
    lv_obj_set_width(s_status, UI_WIDTH - 16);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9AA4B2), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);

    s_messages_box = lv_obj_create(scr);
    lv_obj_set_size(s_messages_box, UI_WIDTH - 16, 300);
    lv_obj_set_style_bg_color(s_messages_box, lv_color_hex(0x141926), 0);
    lv_obj_set_style_border_width(s_messages_box, 1, 0);
    lv_obj_set_style_radius(s_messages_box, 8, 0);
    lv_obj_set_style_pad_all(s_messages_box, 8, 0);
    lv_obj_set_scrollbar_mode(s_messages_box, LV_SCROLLBAR_MODE_AUTO);

    s_messages = lv_label_create(s_messages_box);
    lv_obj_set_width(s_messages, UI_WIDTH - 40);
    lv_label_set_long_mode(s_messages, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_messages,
        "CsAW: Hello! Type a message below and press Enter (or the keyboard OK key).\n");

    s_textarea = lv_textarea_create(scr);
    lv_obj_set_width(s_textarea, UI_WIDTH - 16);
    lv_textarea_set_one_line(s_textarea, true);
    lv_textarea_set_placeholder_text(s_textarea, "Message CsAW...");
    lv_obj_add_event_cb(s_textarea, textarea_event_cb, LV_EVENT_ALL, NULL);
    lv_group_add_obj(s_group, s_textarea);
    lv_group_add_obj(s_group, s_messages_box);

    s_keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_keyboard, UI_WIDTH, 190);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_keyboard, s_textarea);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);

    lv_group_focus_obj(s_textarea);
    strncpy(s_history, "CsAW: Hello! Type a message below and press Enter (or the keyboard OK key).\n",
            sizeof(s_history) - 1);
}

static void ui_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");
    while (1) {
        ui_lock();
        lv_timer_handler();
        ui_unlock();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void tick_timer_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

esp_err_t csaw_ui_init(void)
{
    if (s_ready) return ESP_OK;
    if (!csaw_hal_display_panel()) return ESP_ERR_INVALID_STATE;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    lv_init();

    size_t buf_px = (size_t)UI_WIDTH * UI_BUF_LINES;
    lv_color_t *buf1 = heap_caps_calloc(buf_px, sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_calloc(buf_px, sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "No PSRAM for LVGL draw buffers");
        free(buf1);
        free(buf2);
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(UI_WIDTH, UI_HEIGHT);
    if (!s_disp) return ESP_ERR_NO_MEM;
    lv_display_set_user_data(s_disp, csaw_hal_display_panel());
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, buf1, buf2, buf_px * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touch_read_cb);
    s_group = lv_group_create();
    lv_indev_set_group(s_indev, s_group);

    build_screen();

    esp_timer_create_args_t timer_args = {
        .callback = tick_timer_cb,
        .name = "csaw_lvgl_tick",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_tick_timer);
    if (err != ESP_OK) return err;
    err = esp_timer_start_periodic(s_tick_timer, 1000);
    if (err != ESP_OK) return err;

    s_ready = true;
    if (xTaskCreatePinnedToCore(ui_task, "csaw_ui", UI_TASK_STACK, NULL,
                                UI_TASK_PRIO, NULL, 1) != pdPASS) {
        s_ready = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "On-screen keyboard + chat UI ready (%dx%d)", UI_WIDTH, UI_HEIGHT);
    return ESP_OK;
}

void csaw_ui_set_send_cb(csaw_ui_send_cb cb)
{
    s_send_cb = cb;
}

esp_err_t csaw_ui_show_response(const char *text)
{
    if (!s_ready || !text) return ESP_ERR_INVALID_STATE;
    ui_lock();
    history_append("CsAW: ", text);
    if (s_status) lv_label_set_text(s_status, "Reply received - tap input to continue");
    ui_unlock();
    return ESP_OK;
}

esp_err_t csaw_ui_show_status(const char *text)
{
    if (!s_ready || !text) return ESP_ERR_INVALID_STATE;
    ui_lock();
    if (s_status) lv_label_set_text(s_status, text);
    ui_unlock();
    return ESP_OK;
}

bool csaw_ui_ready(void)
{
    return s_ready;
}
