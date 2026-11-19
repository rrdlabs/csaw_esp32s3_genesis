/*
 * CsAW MicroPython bridge - public C API exported by the
 * components/csaw_micropython user C module to the rest of the firmware.
 *
 * The MicroPython task owns its VM state.  These functions never execute Python
 * on the caller's task; they hand work to the VM through mp_sched_schedule().
 */
#ifndef CSAW_VM_BRIDGE_H
#define CSAW_VM_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Must match MICROPY_ESP_IDF_ENTRY in
 * components/csaw_micropython/boards/CSAW_ESP32S3/mpconfigboard.h.  MicroPython
 * exports this function (it does not export app_main in this build). */
#ifndef CSAW_MICROPYTHON_ENTRY
#define CSAW_MICROPYTHON_ENTRY csaw_micropython_entry
#endif
void CSAW_MICROPYTHON_ENTRY(void);

/* Creates the MicroPython task exactly once.  Never call from app_main/boot:
 * only main/python/csaw_python_vm.c (explicit user/agent demand) may call it. */
void csaw_micropython_start(void);

/* True after the frozen CsAW _boot.py has installed the sandbox and called
 * csaw_vm.mark_ready(). */
bool csaw_mp_is_ready(void);

/* Queue a script/file for execution on the MicroPython task and wait.
 * csaw_mp_exec_file() accepts an absolute path already validated by the caller
 * as living under /sd/csaw/python.  Returns ESP_ERR_INVALID_STATE before the
 * VM is ready, ESP_ERR_TIMEOUT if the VM does not run the job in time. */
esp_err_t csaw_mp_exec_file(const char *sd_path, uint32_t timeout_ms);
esp_err_t csaw_mp_exec_source(const char *source, size_t len, uint32_t timeout_ms);

/* Reclaim completed job slots (used internally and by diagnostics). */
void csaw_mp_reap(void);

#ifdef __cplusplus
}
#endif

#endif /* CSAW_VM_BRIDGE_H */
