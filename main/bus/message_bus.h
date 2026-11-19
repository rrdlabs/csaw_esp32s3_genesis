#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Channel identifiers */
#define CSAW_CHAN_TELEGRAM   "telegram"
#define CSAW_CHAN_FEISHU     "feishu"
#define CSAW_CHAN_WEBSOCKET  "websocket"
#define CSAW_CHAN_CLI        "cli"
#define CSAW_CHAN_UI         "ui"         /* primary on-screen chat UI */
#define CSAW_CHAN_SYSTEM     "system"

/* Message types on the bus */
typedef struct {
    char channel[16];       /* "telegram", "websocket", "cli" */
    char chat_id[96];       /* Telegram/Feishu chat_id, open_id, or WS client id */
    char *content;          /* Heap-allocated message text (caller must free) */
} csaw_msg_t;

/**
 * Initialize the message bus (inbound + outbound FreeRTOS queues).
 */
esp_err_t message_bus_init(void);

/**
 * Push a message to the inbound queue (towards Agent Loop).
 * The bus takes ownership of msg->content.
 */
esp_err_t message_bus_push_inbound(const csaw_msg_t *msg);

/**
 * Pop a message from the inbound queue (blocking).
 * Caller must free msg->content when done.
 */
esp_err_t message_bus_pop_inbound(csaw_msg_t *msg, uint32_t timeout_ms);

/**
 * Push a message to the outbound queue (towards channels).
 * The bus takes ownership of msg->content.
 */
esp_err_t message_bus_push_outbound(const csaw_msg_t *msg);

/**
 * Pop a message from the outbound queue (blocking).
 * Caller must free msg->content when done.
 */
esp_err_t message_bus_pop_outbound(csaw_msg_t *msg, uint32_t timeout_ms);
