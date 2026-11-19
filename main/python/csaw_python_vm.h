/*
 * CsAW on-demand Python VM API.
 *
 * Nothing in this file is invoked from app_main or any boot path.  The parent
 * agent must wire the three CLI commands documented in docs/MICROPYTHON.md to
 * these functions; each command is an explicit user/agent demand.
 */
#ifndef CSAW_PYTHON_VM_H
#define CSAW_PYTHON_VM_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSAW_PYTHON_SD_ROOT "/sd/csaw/python"
#define CSAW_PYTHON_MAX_PATH 192
#define CSAW_PYTHON_MAX_SOURCE (64 * 1024)

/* Starts the embedded VM on explicit demand only.  Idempotent. */
esp_err_t csaw_python_vm_init(void);

/* True once the CsAW sandbox is installed and the VM can accept jobs. */
bool csaw_python_vm_is_running(void);

/* Execute a Python file.  Only absolute paths under /sd/csaw/python are
 * accepted; traversal, /spiffs, /, NVS-style paths and relative paths are
 * rejected. */
esp_err_t csaw_python_vm_exec_file(const char *sd_path);

/* Execute a Python source string.  The frozen CsAW sandbox restricts Python
 * writes to /sd/csaw/python; flash filesystems are never mounted. */
esp_err_t csaw_python_vm_exec_string(const char *source);

/* Stop accepting new jobs.  The MicroPython task remains resident (the upstream
 * port has no supported task teardown); this is a safe "pause" for CLI use. */
esp_err_t csaw_python_vm_stop(void);

/* Exposed for tests/CLI diagnostics: pure path policy check. */
bool csaw_python_vm_path_allowed(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CSAW_PYTHON_VM_H */
