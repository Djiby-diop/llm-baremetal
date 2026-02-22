#!/bin/bash
# Run llm-baremetal-boot.img in QEMU (Linux/WSL)

echo "Starting QEMU..."
echo ""

IMAGE="llm-baremetal-boot.img"

# Verify image
if [ ! -f "$IMAGE" ]; then
    echo "ERROR: $IMAGE not found"
    exit 1
fi

echo "[OK] Image: $IMAGE ($(du -h $IMAGE | cut -f1))"

# Verify QEMU
if ! command -v qemu-system-x86_64 &> /dev/null; then
    echo "ERROR: QEMU not found"
    echo ""
    echo "Install (Debian/Ubuntu):"
    echo "  sudo apt update"
    echo "  sudo apt install qemu-system-x86 ovmf"
    exit 1
fi

echo "[OK] QEMU: $(which qemu-system-x86_64)"

# Locate OVMF
OVMF_PATHS=(
    "/usr/share/OVMF/OVMF_CODE.fd"
    "/usr/share/ovmf/OVMF.fd"
    "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd"
)

OVMF=""
for path in "${OVMF_PATHS[@]}"; do
    if [ -f "$path" ]; then
        OVMF="$path"
        break
    fi
done

if [ -z "$OVMF" ]; then
    echo "WARN: OVMF not found; attempting legacy boot..."
    echo ""
    
    # Boot without UEFI
    qemu-system-x86_64 \
        -drive format=raw,file=$IMAGE \
        -m 4096 \
        -cpu max \
        -smp 2 \
        -serial stdio \
        -vga std \
        -display gtk
else
    echo "[OK] OVMF: $OVMF"
    echo ""
    echo "Note: Ctrl+Alt+G releases the mouse (GTK display)"
    echo ""
    
    # Create temporary OVMF vars
    OVMF_VARS="ovmf-vars-temp.fd"
    if [ ! -f "$OVMF_VARS" ]; then
        cp "${OVMF/_CODE/_VARS}" "$OVMF_VARS" 2>/dev/null || \
        dd if=/dev/zero of="$OVMF_VARS" bs=1K count=128 2>/dev/null
    fi
    
    # Boot UEFI
    qemu-system-x86_64 \
        -drive if=pflash,format=raw,readonly=on,file=$OVMF \
        -drive if=pflash,format=raw,file=$OVMF_VARS \
        -drive format=raw,file=$IMAGE \
        -m 4096 \
        -cpu max \
        -smp 2 \
        -serial stdio \
        -vga std \
        -display gtk
fi

echo ""
echo "[OK] QEMU finished"
