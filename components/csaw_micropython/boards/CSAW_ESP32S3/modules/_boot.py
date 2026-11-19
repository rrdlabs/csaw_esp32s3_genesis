# CsAW on-demand MicroPython boot shim.
#
# The CsAW native HAL has already mounted the 1-bit SDMMC TF card at /sd via
# IDF VFS (components/csaw_hal/csaw_hal.c).  We expose that mount to Python
# with VfsPosix so Python file I/O goes through IDF VFS.  Flash (/ and
# /spiffs) is never mounted here, so Python writes cannot reach flash.
import gc
import sys
import vfs

SD_ROOT = "/sd/csaw/python"

try:
    vfs.mount(vfs.VfsPosix("/sd"), "/sd")
    import uos

    for _dir in ("/sd/csaw", SD_ROOT):
        try:
            uos.mkdir(_dir)
        except OSError:
            pass
    sys.path.append(SD_ROOT)
    print("CsAW MicroPython: SD card mounted at /sd via IDF VFS")
except Exception as _exc:
    # The VM still starts, but exec calls fail closed (no flash fallback).
    print("CsAW MicroPython: SD mount failed -", _exc)

import csaw_sandbox

csaw_sandbox.enforce()

try:
    import csaw_vm

    csaw_vm.mark_ready()
except ImportError:
    print("CsAW MicroPython: readiness bridge unavailable")
gc.collect()
