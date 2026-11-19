#!/usr/bin/env bash
# Flash the CsAW Genesis merged image to a Waveshare ESP32-S3-Touch-AMOLED-1.8
# Rev 1 board. Usage: scripts/flash_genesis_merged.sh [ttyUSB0]
set -euo pipefail

PORT="${1:-/dev/ttyUSB0}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="$REPO/build/merged-binary.bin"

if [[ ! -f "$IMAGE" ]]; then
    echo "error: $IMAGE not found - run 'idf.py merge-bin' first" >&2
    exit 2
fi
if [[ ! -e "$PORT" ]]; then
    echo "error: $PORT not found - connect the Genesis board and check the driver" >&2
    exit 3
fi

# shellcheck disable=SC1091
source /home/d31337m3/devenv/env-6.1.sh >/dev/null 2>&1 || true

echo "Flashing $IMAGE to $PORT ..."
# esptool v5 requires the flash-mode/size/freq options AFTER the write-flash
# subcommand; placing them before it fails with "No such option '--flash-mode'".
python -m esptool --chip esp32s3 --port "$PORT" -b 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
    0x0 "$IMAGE"
echo "Done. Run 'idf.py -p $PORT monitor' to verify bringup and the >=30% brightness gate."
