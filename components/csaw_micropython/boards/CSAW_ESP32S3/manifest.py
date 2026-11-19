# CsAW frozen module manifest.
#
# Deliberately do NOT include $(PORT_DIR)/boards/manifest.py: that manifest
# freezes MicroPython's own _boot.py, which mounts flash FAT at "/" and would
# violate the CsAW storage policy (docs/STORAGE_POLICY.md).
freeze("$(BOARD_DIR)/modules")
