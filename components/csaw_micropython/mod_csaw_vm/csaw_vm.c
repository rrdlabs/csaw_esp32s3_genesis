/*
 * CsAW MicroPython user C module.
 *
 * Implements the on-demand hand-off between CsAW firmware tasks and the
 * MicroPython interpreter task.  Verified against the vendored v1.29.0 sources:
 *   - mp_sched_schedule()       py/runtime.h
 *   - pyexec_file()             shared/runtime/pyexec.h
 *   - pyexec_vstr()             shared/runtime/pyexec.h
 *   - MICROPY_ESP_IDF_ENTRY     ports/esp32/mpconfigport.h:450
 * No invented MicroPython symbols are used here.
 */
#include "csaw_vm_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "py/misc.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"

#define CSAW_VM_JOB_SLOTS 4

typedef enum {
    CSAW_VM_JOB_FREE = 0,
    CSAW_VM_JOB_QUEUED,
    CSAW_VM_JOB_DONE,
} csaw_vm_job_state_t;

typedef enum {
    CSAW_VM_JOB_FILE = 0,
    CSAW_VM_JOB_SOURCE,
} csaw_vm_job_kind_t;

typedef struct {
    csaw_vm_job_state_t state;
    csaw_vm_job_kind_t kind;
    char *payload;
    size_t len;
    SemaphoreHandle_t done;
    esp_err_t result;
} csaw_vm_job_t;

static csaw_vm_job_t s_jobs[CSAW_VM_JOB_SLOTS];
static SemaphoreHandle_t s_jobs_lock;
static volatile bool s_vm_ready;
static volatile bool s_vm_started;

/* Called from the frozen CsAW _boot.py via `import csaw_vm;
 * csaw_vm.mark_ready()`. */
static mp_obj_t csaw_vm_mark_ready(void) {
    s_vm_ready = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(csaw_vm_mark_ready_obj, csaw_vm_mark_ready);

/* Scheduler callback: executes on the MicroPython task, never on the caller. */
static mp_obj_t csaw_vm_run_job(mp_obj_t slot_obj) {
    uint32_t slot = (uint32_t)mp_obj_get_int(slot_obj);
    if (slot >= CSAW_VM_JOB_SLOTS) {
        return mp_const_none;
    }

    csaw_vm_job_t *job = &s_jobs[slot];
    esp_err_t result = ESP_FAIL;

    if (job->state == CSAW_VM_JOB_QUEUED && job->payload != NULL) {
        int exec_result;
        if (job->kind == CSAW_VM_JOB_FILE) {
            exec_result = pyexec_file(job->payload);
        } else {
            vstr_t source;
            vstr_init(&source, job->len);
            vstr_add_strn(&source, job->payload, job->len);
            exec_result = pyexec_vstr(&source, false);
            vstr_clear(&source);
        }
        result = (exec_result & PYEXEC_FORCED_EXIT) ? ESP_FAIL : ESP_OK;
    }

    free(job->payload);
    job->payload = NULL;
    job->result = result;
    job->state = CSAW_VM_JOB_DONE;
    if (job->done != NULL) {
        xSemaphoreGive(job->done);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(csaw_vm_run_job_obj, csaw_vm_run_job);

static const mp_rom_map_elem_t csaw_vm_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_csaw_vm) },
    { MP_ROM_QSTR(MP_QSTR_mark_ready), MP_ROM_PTR(&csaw_vm_mark_ready_obj) },
};
static MP_DEFINE_CONST_DICT(csaw_vm_globals, csaw_vm_globals_table);

const mp_obj_module_t csaw_vm_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&csaw_vm_globals,
};

MP_REGISTER_MODULE(MP_QSTR_csaw_vm, csaw_vm_module);

static void csaw_vm_init_lock(void) {
    if (s_jobs_lock == NULL) {
        s_jobs_lock = xSemaphoreCreateMutex();
    }
}

/* Called with s_jobs_lock held. */
static void csaw_vm_reap_locked(void) {
    for (uint32_t i = 0; i < CSAW_VM_JOB_SLOTS; i++) {
        if (s_jobs[i].state != CSAW_VM_JOB_DONE) {
            continue;
        }
        if (s_jobs[i].done != NULL) {
            vSemaphoreDelete(s_jobs[i].done);
            s_jobs[i].done = NULL;
        }
        free(s_jobs[i].payload);
        s_jobs[i].payload = NULL;
        s_jobs[i].state = CSAW_VM_JOB_FREE;
    }
}

void csaw_mp_reap(void) {
    csaw_vm_init_lock();
    if (s_jobs_lock != NULL && xSemaphoreTake(s_jobs_lock, portMAX_DELAY) == pdTRUE) {
        csaw_vm_reap_locked();
        xSemaphoreGive(s_jobs_lock);
    }
}

bool csaw_mp_is_ready(void) {
    return s_vm_ready;
}

void csaw_micropython_start(void) {
    if (s_vm_started) {
        return;
    }
    s_vm_started = true;
    csaw_vm_init_lock();
    /* MICROPY_ESP_IDF_ENTRY creates the interpreter task and returns. */
    CSAW_MICROPYTHON_ENTRY();
}

static esp_err_t csaw_vm_submit(csaw_vm_job_kind_t kind, const char *payload,
                                size_t len, uint32_t timeout_ms) {
    if (!s_vm_ready || payload == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    csaw_vm_init_lock();
    if (s_jobs_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_jobs_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    csaw_vm_reap_locked();

    uint32_t slot = CSAW_VM_JOB_SLOTS;
    for (uint32_t i = 0; i < CSAW_VM_JOB_SLOTS; i++) {
        if (s_jobs[i].state == CSAW_VM_JOB_FREE) {
            slot = i;
            break;
        }
    }
    if (slot == CSAW_VM_JOB_SLOTS) {
        xSemaphoreGive(s_jobs_lock);
        return ESP_ERR_NOT_FOUND;
    }

    csaw_vm_job_t *job = &s_jobs[slot];
    job->payload = malloc(len + 1);
    if (job->payload == NULL) {
        xSemaphoreGive(s_jobs_lock);
        return ESP_ERR_NO_MEM;
    }
    memcpy(job->payload, payload, len);
    job->payload[len] = '\0';
    job->len = len;
    job->kind = kind;
    job->result = ESP_FAIL;
    job->done = xSemaphoreCreateBinary();
    if (job->done == NULL) {
        free(job->payload);
        job->payload = NULL;
        xSemaphoreGive(s_jobs_lock);
        return ESP_ERR_NO_MEM;
    }
    job->state = CSAW_VM_JOB_QUEUED;
    xSemaphoreGive(s_jobs_lock);

    /* Small ints need no GC allocation, so this is safe from a non-VM task. */
    bool queued = mp_sched_schedule(MP_OBJ_FROM_PTR(&csaw_vm_run_job_obj), MP_OBJ_NEW_SMALL_INT(slot));
    if (!queued) {
        xSemaphoreTake(s_jobs_lock, portMAX_DELAY);
        vSemaphoreDelete(job->done);
        job->done = NULL;
        free(job->payload);
        job->payload = NULL;
        job->state = CSAW_VM_JOB_FREE;
        xSemaphoreGive(s_jobs_lock);
        return ESP_ERR_NO_MEM;
    }

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(job->done, ticks) != pdTRUE) {
        /* The job may still run later; csaw_mp_reap() frees it once DONE. */
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(s_jobs_lock, portMAX_DELAY);
    esp_err_t result = job->result;
    if (job->done != NULL) {
        vSemaphoreDelete(job->done);
    }
    job->done = NULL;
    job->payload = NULL; /* already freed by the callback */
    job->state = CSAW_VM_JOB_FREE;
    xSemaphoreGive(s_jobs_lock);
    return result;
}

esp_err_t csaw_mp_exec_file(const char *sd_path, uint32_t timeout_ms) {
    return csaw_vm_submit(CSAW_VM_JOB_FILE, sd_path, strlen(sd_path), timeout_ms);
}

esp_err_t csaw_mp_exec_source(const char *source, size_t len, uint32_t timeout_ms) {
    return csaw_vm_submit(CSAW_VM_JOB_SOURCE, source, len, timeout_ms);
}
