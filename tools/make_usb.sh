#!/usr/bin/env bash
set -euo pipefail

DEV="${1:-}"
IMG="${2:-build/steamdeck.img}"

if [ -z "$DEV" ]; then
    echo "Usage: $0 /dev/sdX [image]"
    echo "Writes the Ruby on Bare Metal UEFI image to a USB device."
    exit 1
fi

if [ ! -b "$DEV" ]; then
    echo "error: $DEV is not a block device" >&2
    exit 1
fi

if [ ! -f "$IMG" ]; then
    echo "error: image $IMG not found. Run 'make usb-image' first." >&2
    exit 1
fi

case "$DEV" in
    /dev/sda|/dev/sda[0-9]*|/dev/nvme0n1|/dev/nvme0n1p*)
        echo "error: refusing to write to likely system disk $DEV" >&2
        exit 1
        ;;
esac

SIZE=$(lsblk -bno SIZE "$DEV" | head -1)
MODEL=$(lsblk -no MODEL "$DEV" | head -1 | xargs)

echo "About to overwrite:"
echo "  Device: $DEV ($MODEL, $(numfmt --to=iec "$SIZE"))"
echo "  Source: $IMG"
echo ""
read -r -p "Type 'yes' to confirm: " CONFIRM
if [ "$CONFIRM" != "yes" ]; then
    echo "aborted."
    exit 1
fi

echo "writing..."
sudo dd if="$IMG" of="$DEV" bs=4M conv=fsync status=progress
sudo sync
echo "done. Eject with: sudo eject $DEV"
