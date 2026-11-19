# CsAW Python runtime sandbox: SD-only writes, no flash access.
# Installed by the frozen _boot.py before any user script is executed.
import builtins
import uos

SD_PREFIX = "/sd/csaw/python/"
_real_open = builtins.open
_enforced = False


def _sd_only(path):
    try:
        p = str(path)
    except Exception:
        return False
    if not p or "\\" in p or ".." in p or "\x00" in p:
        return False
    return p.startswith(SD_PREFIX) and len(p) > len(SD_PREFIX)


def _guarded_open(path, mode="r", *args, **kwargs):
    if not _sd_only(path):
        raise OSError("CsAW: Python writes are restricted to /sd/csaw/python")
    return _real_open(path, mode, *args, **kwargs)


def _strip(module_name, attributes):
    try:
        module = __import__(module_name)
    except ImportError:
        return
    for name in attributes:
        try:
            delattr(module, name)
        except (AttributeError, TypeError):
            # Some MicroPython module dictionaries reject deletion.
            try:
                setattr(module, name, None)
            except Exception:
                pass


def enforce():
    """Apply the CsAW sandbox once, before any user code runs."""
    global _enforced
    if _enforced:
        return
    builtins.open = _guarded_open

    # Raw flash and NVS/partition write surfaces must not be reachable.
    _strip("esp", ("flash_write", "flash_erase"))
    _strip("esp32", ("NVS", "Partition"))

    # Filesystem mutation helpers are limited to the SD sandbox by open();
    # remove mount/umount so a script cannot swap in a flash-backed VFS.
    _strip("uos", ("mount", "umount", "mkfs", "rename", "remove", "mkdir", "rmdir"))
    _strip("os", ("mount", "umount", "mkfs", "rename", "remove", "mkdir", "rmdir"))
    # A script must not be able to mount a second (flash-backed) VFS after the
    # boot shim has installed the SD-only mount.
    _strip("vfs", ("mount", "umount", "VfsPosix", "VfsFat", "VfsLfs2"))
    _enforced = True


def sd_root():
    return SD_PREFIX
