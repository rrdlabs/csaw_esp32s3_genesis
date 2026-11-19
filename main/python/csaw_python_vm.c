/*
 * CsAW on-demand Python VM execution API.
 *
 * IMPORTANT: there is deliberately no call to csaw_python_vm_init() from
 * app_main/boot.  The VM is created only when a user/agent issues python_exec,
 * python_eval or an equivalent explicit request.  See docs/MICROPYTHON.md.
 */
#include "csaw_python_vm.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "csaw_vm_bridge.h"

static const char *TAG = "csaw_python_vm";

#define CSAW_PYTHON_READY_TIMEOUT_MS 15000
#define CSAW_PYTHON_EXEC_TIMEOUT_MS 30000

static SemaphoreHandle_t s_state_lock;
static bool s_start_requested;
static bool s_ready;
static bool s_accepting_jobs;

static void csaw_python_vm_lock_init(void) {
    if (s_state_lock == NULL) {
        s_state_lock = xSemaphoreCreateMutex();
    }
}

bool csaw_python_vm_path_allowed(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    size_t len = strlen(path);
    if (len > CSAW_PYTHON_MAX_PATH) {
        return false;
    }
    /* Reject traversal in any form, Windows separators, NUL escapes and every
     * path outside the SD sandbox (including /, /spiffs, /nvs and NVS URIs). */
    if (strstr(path, "..") != NULL || strchr(path, '\\') != NULL ||
        strchr(path, ':') != NULL || strchr(path, '%') != NULL) {
        return false;
    }
    static const char prefix[] = CSAW_PYTHON_SD_ROOT "/";
    return strncmp(path, prefix, sizeof(prefix) - 1) == 0 &&
           len > sizeof(prefix) - 1;
}

esp_err_t csaw_python_vm_init(void) {
    csaw_python_vm_lock_init();
    if (s_state_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (s_start_requested) {
        bool ready = s_ready;
        xSemaphoreGive(s_state_lock);
        return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    s_start_requested = true;
    xSemaphoreGive(s_state_lock);

    ESP_LOGI(TAG, "Starting CsAW Python VM on explicit demand (SD-only sandbox)");
    csaw_micropython_start();

    const TickType_t poll = pdMS_TO_TICKS(50);
    const int max_polls = CSAW_PYTHON_READY_TIMEOUT_MS / 50;
    for (int i = 0; i < max_polls; i++) {
        if (csaw_mp_is_ready()) {
            xSemaphoreTake(s_state_lock, portMAX_DELAY);
            s_ready = true;
            s_accepting_jobs = true;
            xSemaphoreGive(s_state_lock);
            ESP_LOGI(TAG, "CsAW Python VM ready; scripts must live under %s", CSAW_PYTHON_SD_ROOT);
            return ESP_OK;
        }
        vTaskDelay(poll);
    }
    ESP_LOGE(TAG, "CsAW Python VM did not become ready within %d ms", CSAW_PYTHON_READY_TIMEOUT_MS);
    return ESP_ERR_TIMEOUT;
}

bool csaw_python_vm_is_running(void) {
    return s_ready && csaw_mp_is_ready();
}

esp_err_t csaw_python_vm_exec_file(const char *sd_path) {
    if (!csaw_python_vm_path_allowed(sd_path)) {
        ESP_LOGE(TAG, "Rejected script path (must be under %s): %s", CSAW_PYTHON_SD_ROOT,
                 sd_path != NULL ? sd_path : "(null)");
        return ESP_ERR_INVALID_ARG;
    }
    if (!csaw_python_vm_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    bool accepting = s_accepting_jobs;
    xSemaphoreGive(s_state_lock);
    if (!accepting) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Executing CsAW Python script: %s", sd_path);
    esp_err_t err = csaw_mp_exec_file(sd_path, CSAW_PYTHON_EXEC_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Script execution failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t csaw_python_vm_exec_string(const char *source) {
    if (source == NULL || source[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = strlen(source);
    if (len > CSAW_PYTHON_MAX_SOURCE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!csaw_python_vm_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    bool accepting = s_accepting_jobs;
    xSemaphoreGive(s_state_lock);
    if (!accepting) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Executing CsAW Python source (%u bytes)", (unsigned)len);
    esp_err_t err = csaw_mp_exec_source(source, len, CSAW_PYTHON_EXEC_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Source execution failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t csaw_python_vm_stop(void) {
    csaw_python_vm_lock_init();
    if (s_state_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_accepting_jobs = false;
    xSemaphoreGive(s_state_lock);
    ESP_LOGI(TAG, "CsAW Python VM paused; new jobs are rejected");
    return ESP_OK;
}
