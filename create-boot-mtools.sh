#!/bin/bash
# Create bootable USB image with mtools (no sudo required)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "════════════════════════════════════════════════"
echo "🚀 Creating Bootable USB Image (mtools method)"
echo "════════════════════════════════════════════════"
echo ""

#+#+#+#+########
# Model selection
#
# Backward-compatible vars:
#   MODEL_BIN=stories110M.bin ./create-boot-mtools.sh
#
# New preferred var (supports base name OR explicit file):
#   MODEL=stories110M          ./create-boot-mtools.sh   # tries stories110M.bin then stories110M.gguf
#   MODEL=models/my-instruct   ./create-boot-mtools.sh   # tries models/my-instruct.bin then .gguf
#   MODEL=models/my-instruct.gguf ./create-boot-mtools.sh # will also copy sibling .bin if present
MODEL_BIN="${MODEL_BIN:-stories15M.bin}"
MODEL="${MODEL:-}"

# Optional additional models (copied to /models on the FAT partition)
# Usage:
#   EXTRA_MODELS='stories110M.bin;my-instruct.bin' MODEL_BIN=stories110M.bin ./create-boot-mtools.sh
EXTRA_MODELS="${EXTRA_MODELS:-}"

# EFI payload selection (default: llama2.efi)
# Usage:
#   EFI_BIN=llmkernel.efi ./create-boot-mtools.sh
EFI_BIN="${EFI_BIN:-llama2.efi}"

# Check required files
echo "[1/4] Checking required files..."
for file in "$EFI_BIN" tokenizer.bin; do
    if [ ! -f "$file" ]; then
        echo "❌ Missing: $file"
        exit 1
    fi
done

find_src() {
    local rel="$1"
    if [ -f "$rel" ]; then
        echo "$rel"
        return 0
    fi
    if [ -f "../$rel" ]; then
        echo "../$rel"
        return 0
    fi
    return 1
}

has_ext() {
    local s="$1"
    # crude but sufficient: has a dot after last slash
    local base="${s##*/}"
    [[ "$base" == *.* ]]
}

base_no_ext() {
    local s="$1"
    local base="${s%.*}"
    echo "$base"
}

build_candidates() {
    local spec="$1"
    local cands=()

    if has_ext "$spec"; then
        cands+=("$spec")
        case "$spec" in
            *.gguf|*.GGUF)
                cands+=("$(base_no_ext "$spec").bin")
                ;;
            *.bin|*.BIN)
                cands+=("$(base_no_ext "$spec").gguf")
                ;;
        esac
    else
        cands+=("${spec}.bin")
        cands+=("${spec}.gguf")
    fi

    printf "%s\n" "${cands[@]}"
}

add_resolved_files() {
    # Args: spec, arrays (by name): SRCS_ARR, NAMES_ARR
    local spec="$1"
    local -n _srcs="$2"
    local -n _names="$3"

    local seen_local=()
    while IFS= read -r cand; do
        [ -z "$cand" ] && continue
        # avoid duplicates in candidate list
        local dup=0
        for x in "${seen_local[@]}"; do
            [ "$x" = "$cand" ] && dup=1 && break
        done
        [ $dup -eq 1 ] && continue
        seen_local+=("$cand")

        local src
        src="$(find_src "$cand" 2>/dev/null || true)"
        if [ -n "$src" ]; then
            _srcs+=("$src")
            _names+=("$(basename "$cand")")
        fi
    done < <(build_candidates "$spec")
}

# Resolve primary model spec -> one or two files (.bin/.gguf)
MODEL_SPEC="$MODEL_BIN"
if [ -n "$MODEL" ]; then
    MODEL_SPEC="$MODEL"
fi

PRIMARY_SRCS=()
PRIMARY_NAMES=()
add_resolved_files "$MODEL_SPEC" PRIMARY_SRCS PRIMARY_NAMES

if [ ${#PRIMARY_SRCS[@]} -le 0 ]; then
    echo "❌ Missing model: $MODEL_SPEC (looked in current dir and parent dir; supports base name + .bin/.gguf)"
    exit 1
fi

# The first resolved file is treated as the 'primary' for display purposes.
MODEL_SRC="${PRIMARY_SRCS[0]}"
MODEL_OUT_NAME="${PRIMARY_NAMES[0]}"

# Resolve + validate extra models, and compute total size for auto-sizing.
EXTRA_SRCS=()
EXTRA_NAMES=()
TOTAL_MODEL_BYTES=0
for src in "${PRIMARY_SRCS[@]}"; do
    bytes=$(stat -c %s "$src")
    TOTAL_MODEL_BYTES=$((TOTAL_MODEL_BYTES + bytes))
done
if [ -n "$EXTRA_MODELS" ]; then
    IFS=';' read -r -a extra_arr <<< "$EXTRA_MODELS"
    for m in "${extra_arr[@]}"; do
        m_trim="${m//[[:space:]]/}"
        [ -z "$m_trim" ] && continue
        # Skip if same as primary spec (by basename)
        if [ "$(basename "$m_trim")" = "$(basename "$MODEL_SPEC")" ]; then
            continue
        fi

        tmp_sr=()
        tmp_nm=()
        add_resolved_files "$m_trim" tmp_sr tmp_nm
        if [ ${#tmp_sr[@]} -le 0 ]; then
            echo "❌ Missing extra model: $m_trim (supports base name + .bin/.gguf; looked in current dir and parent dir)"
            exit 1
        fi
        for j in "${!tmp_sr[@]}"; do
            src="${tmp_sr[$j]}"
            name="${tmp_nm[$j]}"
            EXTRA_SRCS+=("$src")
            EXTRA_NAMES+=("$name")
            bytes=$(stat -c %s "$src")
            TOTAL_MODEL_BYTES=$((TOTAL_MODEL_BYTES + bytes))
        done
    done
fi
echo "✅ All files present"

# Create image file (auto-sized)
echo ""
MODEL_BYTES=$(stat -c %s "$MODEL_SRC")
MODEL_MIB=$(( (MODEL_BYTES + 1024*1024 - 1) / (1024*1024) ))
TOTAL_MIB=$(( (TOTAL_MODEL_BYTES + 1024*1024 - 1) / (1024*1024) ))
# Slack for FAT + GPT + EFI + tokenizer + alignment
SLACK_MIB=80
IMAGE_MIB=$(( TOTAL_MIB + SLACK_MIB ))
if [ $IMAGE_MIB -lt 100 ]; then IMAGE_MIB=100; fi

echo "[2/4] Creating ${IMAGE_MIB}MB FAT32 image..."
IMAGE="llm-baremetal-boot.img"

# On Windows hosts, a running QEMU may keep the image open, which prevents
# deletion from WSL (/mnt/c) and would otherwise abort the build.
rm -f "$IMAGE" 2>/dev/null || true
if [ -f "$IMAGE" ]; then
    ts="$(date +%Y%m%d-%H%M%S)"
    IMAGE="llm-baremetal-boot-${ts}.img"
    echo "  ⚠️  Existing image is in use; writing new image: $IMAGE"
fi
dd if=/dev/zero of="$IMAGE" bs=1M count=$IMAGE_MIB status=progress
echo "✅ Image created"

# Format as FAT32 with partition table
echo ""
echo "[3/4] Formatting as GPT + FAT32..."
# Use parted for GPT (doesn't need mount)
parted "$IMAGE" --script mklabel gpt
parted "$IMAGE" --script mkpart primary fat32 1MiB 100%
parted "$IMAGE" --script set 1 boot on
parted "$IMAGE" --script set 1 esp on

# Add hybrid MBR for maximum compatibility
echo "  Adding hybrid MBR bootcode..."
# Install GRUB MBR bootcode (helps some BIOS detect the disk)
dd if=/usr/lib/grub/i386-pc/boot.img of="$IMAGE" bs=440 count=1 conv=notrunc 2>/dev/null || echo "  (grub bootcode not found, skipping)"

# Format partition with mformat (no sudo!)
# Calculate offset: 1MiB = 2048 sectors * 512 bytes
OFFSET_BYTES=$((1024 * 1024))
mtoolsrc_tmp="$(mktemp)"
echo "mtools_skip_check=1" > "$mtoolsrc_tmp"
echo "drive z: file=\"${PWD}/${IMAGE}\" offset=${OFFSET_BYTES}" >> "$mtoolsrc_tmp"
export MTOOLSRC="$mtoolsrc_tmp"
mformat -F -v LLMBOOT z:
echo "✅ Formatted"

# Copy files with mcopy
echo ""
echo "[4/4] Copying files with mtools..."
mmd z:/EFI
mmd z:/EFI/BOOT
mcopy "$EFI_BIN" z:/EFI/BOOT/BOOTX64.EFI
echo "  ✅ Copied BOOTX64.EFI"

# Also keep a convenient copy at the root for manual launch in the UEFI shell
mcopy "$EFI_BIN" z:/KERNEL.EFI
echo "  ✅ Copied KERNEL.EFI"

for i in "${!PRIMARY_SRCS[@]}"; do
    src="${PRIMARY_SRCS[$i]}"
    name="${PRIMARY_NAMES[$i]}"
    mcopy "$src" z:/"$name"
    mib=$(( ( $(stat -c %s "$src") + 1024*1024 - 1) / (1024*1024) ))
    echo "  ✅ Copied $name (${mib} MB)"
done

if [ ${#EXTRA_SRCS[@]} -gt 0 ]; then
    mmd z:/models
    echo "  ✅ Created /models"
    for i in "${!EXTRA_SRCS[@]}"; do
        src="${EXTRA_SRCS[$i]}"
        name="${EXTRA_NAMES[$i]}"
        mcopy "$src" z:/models/"$name"
        mib=$(( ( $(stat -c %s "$src") + 1024*1024 - 1) / (1024*1024) ))
        echo "  ✅ Copied models/$name (${mib} MB)"
    done
fi

mcopy tokenizer.bin z:/
echo "  ✅ Copied tokenizer.bin"

# Optional REPL config (key=value). If present, copy to root.
if [ -f repl.cfg ]; then
    mcopy repl.cfg z:/
    echo "  ✅ Copied repl.cfg"
fi

# Optional splash screen (Cyberpunk Interface)
if [ -f splash.bmp ]; then
    mcopy splash.bmp z:/
    echo "  ✅ Copied splash.bmp"
fi

# Optional autorun script. If present, copy to root.
# This lets QEMU/CI run scripted REPL commands without manual typing.
if [ -f llmk-autorun.txt ]; then
    mcopy llmk-autorun.txt z:/
    echo "  ✅ Copied llmk-autorun.txt"
fi

# Create startup.nsh for auto-boot.
# Keep the UEFI shell alive after BOOTX64.EFI returns (avoids landing in the firmware boot manager UI).
cat > startup.nsh <<'EOF'
echo -off
\EFI\BOOT\BOOTX64.EFI
echo.
echo BOOTX64.EFI returned. You are in the UEFI shell.
echo Type 'reset' to reboot or 'poweroff' to exit.
pause
EOF
mcopy startup.nsh z:/
rm -f startup.nsh
echo "  ✅ Copied startup.nsh"

# Cleanup
rm -f "$mtoolsrc_tmp"
echo "✅ Image finalized"

# Show result
echo ""
echo "════════════════════════════════════════════════"
echo "✅ BOOTABLE IMAGE CREATED!"
echo "════════════════════════════════════════════════"
echo ""
echo "📁 File: $(pwd)/$IMAGE"
echo "📊 Size: $(ls -lh $IMAGE | awk '{print $5}')"
echo ""
echo "🔥 Next steps:"
echo "  1. Open Rufus on Windows"
echo "  2. Select your USB drive"
echo "  3. Click SELECT and choose: $IMAGE"
echo "  4. Partition scheme: GPT"
echo "  5. Target system: UEFI (non CSM)"
echo "  6. Click START"
echo ""
echo "🚀 Boot will show optimized matmul message!"
