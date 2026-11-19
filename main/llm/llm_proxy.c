/*
 * CsAW LLM proxy — free-provider chain (Kilo -> OpenCode -> OpenRouter).
 *
 * All providers speak the same chat-completions wire format. The chain is
 * ordered and advances automatically on transport failure, rate limiting
 * (HTTP 429), auth/model rejection, or server error. No paid-key code path
 * exists. See docs/PROVIDER_CONFIG.md.
 */
#include "llm_proxy.h"
#include "csaw_config.h"
#include "provider_chain.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "llm";

#define LLM_DUMP_MAX_BYTES   (16 * 1024)
#define LLM_DUMP_CHUNK_BYTES 320

static void llm_log_payload(const char *label, const char *payload)
{
    if (!payload) {
        ESP_LOGI(TAG, "%s: <null>", label);
        return;
    }

    size_t total = strlen(payload);
#if CSAW_LLM_LOG_VERBOSE_PAYLOAD
    size_t shown = total > LLM_DUMP_MAX_BYTES ? LLM_DUMP_MAX_BYTES : total;
    ESP_LOGI(TAG, "%s (%u bytes)%s", label, (unsigned)total,
             (shown < total) ? " [truncated]" : "");
    char chunk[LLM_DUMP_CHUNK_BYTES + 1];
    for (size_t off = 0; off < shown; off += LLM_DUMP_CHUNK_BYTES) {
        size_t n = shown - off;
        if (n > LLM_DUMP_CHUNK_BYTES) n = LLM_DUMP_CHUNK_BYTES;
        memcpy(chunk, payload + off, n);
        chunk[n] = '\0';
        ESP_LOGI(TAG, "%s[%u]: %s", label, (unsigned)off, chunk);
    }
#else
    if (CSAW_LLM_LOG_PREVIEW_BYTES > 0) {
        size_t shown = total > CSAW_LLM_LOG_PREVIEW_BYTES ? CSAW_LLM_LOG_PREVIEW_BYTES : total;
        char preview[CSAW_LLM_LOG_PREVIEW_BYTES + 1];
        memcpy(preview, payload, shown);
        preview[shown] = '\0';
        for (size_t i = 0; i < shown; i++) {
            if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
                preview[i] = ' ';
            }
        }
        ESP_LOGI(TAG, "%s (%u bytes): %s%s", label, (unsigned)total, preview,
                 (shown < total) ? " ..." : "");
    } else {
        ESP_LOGI(TAG, "%s (%u bytes)", label, (unsigned)total);
    }
#endif
}

/* ── Response buffer ──────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} resp_buf_t;

static esp_err_t resp_buf_init(resp_buf_t *rb, size_t initial_cap)
{
    rb->data = heap_caps_calloc(1, initial_cap, MALLOC_CAP_SPIRAM);
    if (!rb->data) return ESP_ERR_NO_MEM;
    rb->len = 0;
    rb->cap = initial_cap;
    return ESP_OK;
}

static esp_err_t resp_buf_append(resp_buf_t *rb, const char *data, size_t len)
{
    while (rb->len + len >= rb->cap) {
        size_t new_cap = rb->cap * 2;
        char *tmp = heap_caps_realloc(rb->data, new_cap, MALLOC_CAP_SPIRAM);
        if (!tmp) return ESP_ERR_NO_MEM;
        rb->data = tmp;
        rb->cap = new_cap;
    }
    memcpy(rb->data + rb->len, data, len);
    rb->len += len;
    rb->data[rb->len] = '\0';
    return ESP_OK;
}

static void resp_buf_free(resp_buf_t *rb)
{
    free(rb->data);
    rb->data = NULL;
    rb->len = 0;
    rb->cap = 0;
}

static void resp_buf_decode_chunked(resp_buf_t *rb)
{
    if (!rb->data || rb->len == 0) return;
    size_t i = 0;
    while (i < rb->len && (rb->data[i] == ' ' || rb->data[i] == '\t')) i++;
    if (i < rb->len && (rb->data[i] == '{' || rb->data[i] == '[')) return;

    char *src = rb->data;
    char *dst = rb->data;
    char *end = rb->data + rb->len;
    while (src < end) {
        char *line_end = strstr(src, "\r\n");
        if (!line_end) break;
        unsigned long chunk_size = strtoul(src, NULL, 16);
        if (chunk_size == 0) break;
        src = line_end + 2;
        if (src + chunk_size > end) {
            size_t avail = end - src;
            memmove(dst, src, avail);
            dst += avail;
            break;
        }
        memmove(dst, src, chunk_size);
        dst += chunk_size;
        src += chunk_size;
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') src += 2;
    }
    rb->len = dst - rb->data;
    rb->data[rb->len] = '\0';
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *rb = (resp_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        return resp_buf_append(rb, (const char *)evt->data, evt->data_len);
    }
    return ESP_OK;
}

/* ── Init ─────────────────────────────────────────────────────── */

esp_err_t llm_proxy_init(void)
{
    esp_err_t err = provider_chain_init();
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "LLM proxy ready: active=%s, model=%s",
             provider_chain_active()->id, provider_chain_active()->model);
    if (!provider_chain_active()->api_key[0]) {
        ESP_LOGI(TAG, "No API key configured; free/anonymous tier is used where available");
    }
    return ESP_OK;
}

/* ── HTTP transport (direct and CONNECT-tunnel proxy) ────────── */

static esp_err_t split_chat_url(const char *url, char *host, size_t host_len,
                                char *path, size_t path_len)
{
    if (!url || strncasecmp(url, "https://", 8) != 0) return ESP_ERR_INVALID_ARG;
    const char *start = url + 8;
    const char *slash = strchr(start, '/');
    size_t hlen = slash ? (size_t)(slash - start) : strlen(start);
    if (hlen == 0 || hlen >= host_len) return ESP_ERR_INVALID_ARG;
    memcpy(host, start, hlen);
    host[hlen] = '\0';
    if (slash) {
        size_t plen = strlen(slash);
        if (plen >= path_len) return ESP_ERR_INVALID_ARG;
        memcpy(path, slash, plen);
        path[plen] = '\0';
    } else {
        snprintf(path, path_len, "/");
    }
    return ESP_OK;
}

static esp_err_t llm_http_direct(const csaw_provider_t *provider,
                                 const char *post_data, resp_buf_t *rb, int *out_status)
{
    esp_http_client_config_t config = {
        .url = provider->chat_url,
        .event_handler = http_event_handler,
        .user_data = rb,
        .timeout_ms = 120 * 1000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (provider->api_key[0]) {
        char auth[CSAW_PROVIDER_KEY_MAX + 16];
        snprintf(auth, sizeof(auth), "Bearer %s", provider->api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    *out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t llm_http_via_proxy(const csaw_provider_t *provider,
                                    const char *post_data, resp_buf_t *rb, int *out_status)
{
    char host[160];
    char path[192];
    esp_err_t err = split_chat_url(provider->chat_url, host, sizeof(host), path, sizeof(path));
    if (err != ESP_OK) return err;

    proxy_conn_t *conn = proxy_conn_open(host, 443, 30000);
    if (!conn) return ESP_ERR_HTTP_CONNECT;

    int body_len = strlen(post_data);
    char header[1400];
    int auth_len = 0;
    char auth[CSAW_PROVIDER_KEY_MAX + 32];
    if (provider->api_key[0]) {
        auth_len = snprintf(auth, sizeof(auth), "Authorization: Bearer %s\r\n",
                            provider->api_key);
    }
    int hlen = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "%s"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        path, host, auth_len > 0 ? auth : "", body_len);
    if (hlen < 0 || hlen >= (int)sizeof(header)) {
        proxy_conn_close(conn);
        return ESP_ERR_NO_MEM;
    }

    if (proxy_conn_write(conn, header, hlen) < 0 ||
        proxy_conn_write(conn, post_data, body_len) < 0) {
        proxy_conn_close(conn);
        return ESP_ERR_HTTP_WRITE_DATA;
    }

    char tmp[4096];
    while (1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 120000);
        if (n <= 0) break;
        if (resp_buf_append(rb, tmp, n) != ESP_OK) break;
    }
    proxy_conn_close(conn);

    *out_status = 0;
    if (rb->len > 5 && strncmp(rb->data, "HTTP/", 5) == 0) {
        const char *sp = strchr(rb->data, ' ');
        if (sp) *out_status = atoi(sp + 1);
    }
    char *body = strstr(rb->data, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = rb->len - (body - rb->data);
        memmove(rb->data, body, blen);
        rb->len = blen;
        rb->data[rb->len] = '\0';
    }
    resp_buf_decode_chunked(rb);
    return ESP_OK;
}

static esp_err_t llm_http_call(const csaw_provider_t *provider, const char *post_data,
                               resp_buf_t *rb, int *out_status)
{
    if (http_proxy_is_enabled()) {
        return llm_http_via_proxy(provider, post_data, rb, out_status);
    }
    return llm_http_direct(provider, post_data, rb, out_status);
}

/* ── Chat-completions request/response conversion ────────────── */

/* Agent messages use content blocks; convert them to the chat wire format. */
static cJSON *convert_tools_chat(const char *tools_json)
{
    if (!tools_json) return NULL;
    cJSON *arr = cJSON_Parse(tools_json);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return NULL;
    }
    cJSON *out = cJSON_CreateArray();
    cJSON *tool;
    cJSON_ArrayForEach(tool, arr) {
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        cJSON *desc = cJSON_GetObjectItem(tool, "description");
        cJSON *schema = cJSON_GetObjectItem(tool, "input_schema");
        if (!name || !cJSON_IsString(name)) continue;

        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", name->valuestring);
        if (desc && cJSON_IsString(desc)) {
            cJSON_AddStringToObject(func, "description", desc->valuestring);
        }
        if (schema) {
            cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(schema, 1));
        }
        cJSON *wrap = cJSON_CreateObject();
        cJSON_AddStringToObject(wrap, "type", "function");
        cJSON_AddItemToObject(wrap, "function", func);
        cJSON_AddItemToArray(out, wrap);
    }
    cJSON_Delete(arr);
    return out;
}

static void append_text(char **buf, size_t *off, const char *text)
{
    if (!text) return;
    size_t tlen = strlen(text);
    char *tmp = realloc(*buf, *off + tlen + 1);
    if (!tmp) return;
    *buf = tmp;
    memcpy(*buf + *off, text, tlen);
    *off += tlen;
    (*buf)[*off] = '\0';
}

static cJSON *convert_messages_chat(const char *system_prompt, cJSON *messages)
{
    cJSON *out = cJSON_CreateArray();
    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(out, sys);
    }
    if (!messages || !cJSON_IsArray(messages)) return out;

    cJSON *msg;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!role || !cJSON_IsString(role)) continue;

        if (content && cJSON_IsString(content)) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", role->valuestring);
            cJSON_AddStringToObject(m, "content", content->valuestring);
            cJSON_AddItemToArray(out, m);
            continue;
        }
        if (!content || !cJSON_IsArray(content)) continue;

        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", "assistant");
            char *text_buf = NULL;
            size_t off = 0;
            cJSON *tool_calls = NULL;
            cJSON *block;
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (!btype || !cJSON_IsString(btype)) continue;
                if (strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) append_text(&text_buf, &off, text->valuestring);
                } else if (strcmp(btype->valuestring, "tool_use") == 0) {
                    if (!tool_calls) tool_calls = cJSON_CreateArray();
                    cJSON *id = cJSON_GetObjectItem(block, "id");
                    cJSON *name = cJSON_GetObjectItem(block, "name");
                    cJSON *input = cJSON_GetObjectItem(block, "input");
                    if (!name || !cJSON_IsString(name)) continue;

                    cJSON *tc = cJSON_CreateObject();
                    if (id && cJSON_IsString(id)) {
                        cJSON_AddStringToObject(tc, "id", id->valuestring);
                    }
                    cJSON_AddStringToObject(tc, "type", "function");
                    cJSON *func = cJSON_CreateObject();
                    cJSON_AddStringToObject(func, "name", name->valuestring);
                    if (input) {
                        char *args = cJSON_PrintUnformatted(input);
                        if (args) {
                            cJSON_AddStringToObject(func, "arguments", args);
                            free(args);
                        }
                    }
                    cJSON_AddItemToObject(tc, "function", func);
                    cJSON_AddItemToArray(tool_calls, tc);
                }
            }
            cJSON_AddStringToObject(m, "content", text_buf ? text_buf : "");
            if (tool_calls) cJSON_AddItemToObject(m, "tool_calls", tool_calls);
            cJSON_AddItemToArray(out, m);
            free(text_buf);
        } else if (strcmp(role->valuestring, "user") == 0) {
            cJSON *block;
            bool has_user_text = false;
            char *text_buf = NULL;
            size_t off = 0;
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (!btype || !cJSON_IsString(btype)) continue;
                if (strcmp(btype->valuestring, "tool_result") == 0) {
                    cJSON *tool_id = cJSON_GetObjectItem(block, "tool_use_id");
                    cJSON *tcontent = cJSON_GetObjectItem(block, "content");
                    if (!tool_id || !cJSON_IsString(tool_id)) continue;
                    cJSON *tm = cJSON_CreateObject();
                    cJSON_AddStringToObject(tm, "role", "tool");
                    cJSON_AddStringToObject(tm, "tool_call_id", tool_id->valuestring);
                    cJSON_AddStringToObject(tm, "content",
                                            (tcontent && cJSON_IsString(tcontent)) ?
                                            tcontent->valuestring : "");
                    cJSON_AddItemToArray(out, tm);
                } else if (strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        append_text(&text_buf, &off, text->valuestring);
                        has_user_text = true;
                    }
                }
            }
            if (has_user_text) {
                cJSON *um = cJSON_CreateObject();
                cJSON_AddStringToObject(um, "role", "user");
                cJSON_AddStringToObject(um, "content", text_buf ? text_buf : "");
                cJSON_AddItemToArray(out, um);
            }
            free(text_buf);
        }
    }
    return out;
}

static void parse_chat_response(cJSON *root, llm_response_t *resp)
{
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    cJSON *choice0 = choices && cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    if (!choice0) return;

    cJSON *finish = cJSON_GetObjectItem(choice0, "finish_reason");
    if (finish && cJSON_IsString(finish)) {
        resp->tool_use = (strcmp(finish->valuestring, "tool_calls") == 0);
    }

    cJSON *message = cJSON_GetObjectItem(choice0, "message");
    if (!message) return;

    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (content && cJSON_IsString(content)) {
        size_t tlen = strlen(content->valuestring);
        resp->text = calloc(1, tlen + 1);
        if (resp->text) {
            memcpy(resp->text, content->valuestring, tlen);
            resp->text_len = tlen;
        }
    }

    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        cJSON *tc;
        cJSON_ArrayForEach(tc, tool_calls) {
            if (resp->call_count >= CSAW_MAX_TOOL_CALLS) break;
            llm_tool_call_t *call = &resp->calls[resp->call_count];
            cJSON *id = cJSON_GetObjectItem(tc, "id");
            cJSON *func = cJSON_GetObjectItem(tc, "function");
            if (id && cJSON_IsString(id)) {
                strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
            }
            if (func) {
                cJSON *name = cJSON_GetObjectItem(func, "name");
                cJSON *args = cJSON_GetObjectItem(func, "arguments");
                if (name && cJSON_IsString(name)) {
                    strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
                }
                if (args && cJSON_IsString(args)) {
                    call->input = strdup(args->valuestring);
                    if (call->input) call->input_len = strlen(call->input);
                }
            }
            resp->call_count++;
        }
        if (resp->call_count > 0) resp->tool_use = true;
    }
}

/* ── Public API ───────────────────────────────────────────────── */

void llm_response_free(llm_response_t *resp)
{
    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

esp_err_t llm_chat_tools(const char *system_prompt, cJSON *messages,
                         const char *tools_json, llm_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    size_t attempts = provider_chain_count();
    if (attempts == 0) return ESP_ERR_INVALID_STATE;

    esp_err_t last_err = ESP_FAIL;
    for (size_t attempt = 0; attempt < attempts; attempt++) {
        const csaw_provider_t *provider = provider_chain_active();
        if (!provider) return ESP_ERR_INVALID_STATE;
        if (!provider->model[0]) {
            ESP_LOGW(TAG, "%s has no model configured; skipping", provider->id);
            provider_chain_report_failure(provider, 404);
            continue;
        }

        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "model", provider->model);
        cJSON_AddNumberToObject(body, "max_tokens", CSAW_LLM_MAX_TOKENS);
        cJSON_AddItemToObject(body, "messages",
                              convert_messages_chat(system_prompt, messages));
        if (tools_json) {
            cJSON *tools = convert_tools_chat(tools_json);
            if (tools) {
                cJSON_AddItemToObject(body, "tools", tools);
                cJSON_AddStringToObject(body, "tool_choice", "auto");
            }
        }
        char *post_data = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        if (!post_data) return ESP_ERR_NO_MEM;

        ESP_LOGI(TAG, "LLM request [%zu/%zu] provider=%s model=%s body=%d bytes",
                 attempt + 1, attempts, provider->id, provider->model,
                 (int)strlen(post_data));
        llm_log_payload("LLM request", post_data);

        resp_buf_t rb;
        if (resp_buf_init(&rb, CSAW_LLM_STREAM_BUF_SIZE) != ESP_OK) {
            free(post_data);
            return ESP_ERR_NO_MEM;
        }
        int status = 0;
        esp_err_t err = llm_http_call(provider, post_data, &rb, &status);
        free(post_data);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s transport failed: %s", provider->id, esp_err_to_name(err));
            llm_log_payload("LLM partial response", rb.data);
            resp_buf_free(&rb);
            last_err = err;
            provider_chain_report_failure(provider, 503);
            continue;
        }
        llm_log_payload("LLM raw response", rb.data);
        if (status != 200) {
            ESP_LOGE(TAG, "%s returned HTTP %d: %.300s", provider->id, status,
                     rb.data ? rb.data : "");
            resp_buf_free(&rb);
            last_err = ESP_FAIL;
            provider_chain_report_failure(provider, status);
            continue;
        }

        cJSON *root = cJSON_Parse(rb.data);
        resp_buf_free(&rb);
        if (!root) {
            ESP_LOGE(TAG, "%s returned malformed JSON", provider->id);
            last_err = ESP_FAIL;
            provider_chain_report_failure(provider, 502);
            continue;
        }
        parse_chat_response(root, resp);
        cJSON_Delete(root);
        provider_chain_report_success(provider);
        ESP_LOGI(TAG, "Response from %s: %d bytes text, %d tool calls, tool_use=%d",
                 provider->id, (int)resp->text_len, resp->call_count, resp->tool_use);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "All free providers failed; last error=%s", esp_err_to_name(last_err));
    return last_err;
}

esp_err_t llm_set_api_key(const char *api_key)
{
    return provider_chain_set_key(provider_chain_active_id(), api_key);
}

esp_err_t llm_set_model(const char *model)
{
    return provider_chain_set_model(provider_chain_active_id(), model);
}

esp_err_t llm_set_provider(const char *provider)
{
    return provider_chain_select(provider);
}
