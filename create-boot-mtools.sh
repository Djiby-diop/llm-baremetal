#!/bin/bash
# Create bootable USB image with mtools (no sudo required)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "==============================================="
echo "Creating Bootable USB Image (mtools method)"
echo "==============================================="
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
MODEL_BIN="${MODEL_BIN:-stories110M.bin}"
MODEL="${MODEL:-}"

# Build an image without embedding any model weights.
# Useful for CI/release artifacts and for users who want to copy their own model later.
NO_MODEL="${NO_MODEL:-0}"
if [ -n "$MODEL" ]; then
    case "${MODEL}" in
        none|nomodel|no-model)
            NO_MODEL=1
            ;;
    esac
fi

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
        echo "ERROR: Missing: $file"
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

PRIMARY_SRCS=()
PRIMARY_NAMES=()
EXTRA_SRCS=()
EXTRA_NAMES=()
TOTAL_MODEL_BYTES=0

MODEL_SPEC="$MODEL_BIN"
if [ -n "$MODEL" ]; then
    MODEL_SPEC="$MODEL"
fi

if [ "$NO_MODEL" -eq 1 ]; then
    echo "WARN: NO_MODEL=1: building image without embedding model weights"
    MODEL_SRC=""
    MODEL_OUT_NAME="(none)"
else
    # Resolve primary model spec -> one or two files (.bin/.gguf)
    add_resolved_files "$MODEL_SPEC" PRIMARY_SRCS PRIMARY_NAMES

    if [ ${#PRIMARY_SRCS[@]} -le 0 ]; then
        echo "ERROR: Missing model: $MODEL_SPEC (looked in current dir and parent dir; supports base name + .bin/.gguf)"
        exit 1
    fi

    # The first resolved file is treated as the 'primary' for display purposes.
    MODEL_SRC="${PRIMARY_SRCS[0]}"
    MODEL_OUT_NAME="${PRIMARY_NAMES[0]}"

    # Resolve + validate extra models, and compute total size for auto-sizing.
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
                echo "ERROR: Missing extra model: $m_trim (supports base name + .bin/.gguf; looked in current dir and parent dir)"
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
fi
echo "[OK] All files present"

# Create image file (auto-sized)
echo ""
TOTAL_MIB=$(( (TOTAL_MODEL_BYTES + 1024*1024 - 1) / (1024*1024) ))
# Slack for FAT + GPT + EFI + tokenizer + alignment
SLACK_MIB=80
IMAGE_MIB=$(( TOTAL_MIB + SLACK_MIB ))
if [ $IMAGE_MIB -lt 100 ]; then IMAGE_MIB=100; fi

# Optional override: force a specific image size in MiB.
# Example:
#   IMG_MB=1200 NO_MODEL=1 ./create-boot-mtools.sh
if [ -n "${IMG_MB:-}" ]; then
    if [[ "${IMG_MB}" =~ ^[0-9]+$ ]] && [ "${IMG_MB}" -gt 0 ]; then
        IMAGE_MIB="${IMG_MB}"
    else
        echo "ERROR: Invalid IMG_MB: '${IMG_MB}' (expected positive integer MiB)"
        exit 1
    fi
fi

if [ "$NO_MODEL" -eq 1 ]; then
    # Make the no-model image large enough for users to drop in weights later.
    # (Still modest for a release artifact.)
    if [ $IMAGE_MIB -lt 300 ]; then IMAGE_MIB=300; fi
fi

echo "[2/4] Creating ${IMAGE_MIB}MB FAT32 image..."
IMAGE="llm-baremetal-boot.img"

# On Windows hosts, a running QEMU may keep the image open, which prevents
# deletion from WSL (/mnt/c) and would otherwise abort the build.
rm -f "$IMAGE" 2>/dev/null || true
if [ -f "$IMAGE" ]; then
    ts="$(date +%Y%m%d-%H%M%S)"
    IMAGE="llm-baremetal-boot-${ts}.img"
    echo "  WARN: Existing image is in use; writing new image: $IMAGE"
fi
dd if=/dev/zero of="$IMAGE" bs=1M count=$IMAGE_MIB status=progress
echo "[OK] Image created"

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
echo "[OK] Formatted"

# Copy files with mcopy
echo ""
echo "[4/4] Copying files with mtools..."
mmd z:/EFI
mmd z:/EFI/BOOT
mcopy "$EFI_BIN" z:/EFI/BOOT/BOOTX64.EFI
echo "  [OK] Copied BOOTX64.EFI"

# Also keep a convenient copy at the root for manual launch in the UEFI shell
mcopy "$EFI_BIN" z:/KERNEL.EFI
echo "  [OK] Copied KERNEL.EFI"

if [ "$NO_MODEL" -ne 1 ]; then
    for i in "${!PRIMARY_SRCS[@]}"; do
        src="${PRIMARY_SRCS[$i]}"
        name="${PRIMARY_NAMES[$i]}"
        mcopy "$src" z:/"$name"
        mib=$(( ( $(stat -c %s "$src") + 1024*1024 - 1) / (1024*1024) ))
        echo "  [OK] Copied $name (${mib} MB)"
    done

    # Convenience: if a small GGUF is available (e.g. stories15M.q8_0.gguf),
    # bundle it into /models so automated tests can boot it without per-run injection.
    {
        small_gguf_src="$(find_src 'models/stories15M.q8_0.gguf' 2>/dev/null || find_src 'stories15M.q8_0.gguf' 2>/dev/null || true)"
        if [ -n "$small_gguf_src" ]; then
            mmd z:/models 2>/dev/null || true
            mcopy "$small_gguf_src" z:/models/stories15M.q8_0.gguf
            mib=$(( ( $(stat -c %s "$small_gguf_src") + 1024*1024 - 1) / (1024*1024) ))
            echo "  [OK] Copied models/stories15M.q8_0.gguf (${mib} MB)"
        fi
    }

    if [ ${#EXTRA_SRCS[@]} -gt 0 ]; then
        mmd z:/models
        echo "  [OK] Created /models"
        for i in "${!EXTRA_SRCS[@]}"; do
            src="${EXTRA_SRCS[$i]}"
            name="${EXTRA_NAMES[$i]}"
            mcopy "$src" z:/models/"$name"
            mib=$(( ( $(stat -c %s "$src") + 1024*1024 - 1) / (1024*1024) ))
            echo "  [OK] Copied models/$name (${mib} MB)"
        done
    fi
else
    echo "  [INFO] Skipping model copy (NO_MODEL=1)"
fi

mcopy tokenizer.bin z:/
echo "  [OK] Copied tokenizer.bin"

# REPL config (key=value).
# If we're bundling a primary model, write a repl.cfg that points at it so the image boots correctly.
# This avoids stale repl.cfg files referencing a different default model.
AUTO_SET_REPL_MODEL="${AUTO_SET_REPL_MODEL:-1}"
if [ "$NO_MODEL" -ne 1 ] && [ "$AUTO_SET_REPL_MODEL" -eq 1 ]; then
    # Some UEFI FAT drivers can fail to open long filenames reliably.
    # mtools creates a short 8.3 alias alongside the long name; prefer that for repl.cfg.
    model_long="${MODEL_OUT_NAME}"
    mdir_path="z:/"
    model_leaf="$model_long"
    case "$model_long" in
        models/*)
            mdir_path="z:/models"
            model_leaf="${model_long#models/}"
            ;;
    esac

    short_model="$model_long"
    alias_83="$(mdir "$mdir_path" 2>/dev/null | tr -d '\r' | awk -v t="$model_leaf" '
        {
            long=$NF;
            if (tolower(long)==tolower(t) && $2!="<DIR>") {
                print $1 "." $2;
                exit 0;
            }
        }
    ')"
    if [ -n "$alias_83" ]; then
        if [ "$mdir_path" = "z:/models" ]; then
            short_model="models/${alias_83}"
        else
            short_model="$alias_83"
        fi
    fi
    tmp_cfg="$(mktemp)"
    {
        echo "model=${short_model}"
        if [ -f repl.cfg ]; then
            # Keep existing settings, but override any previous model= line.
            grep -vi '^model=' repl.cfg || true
        fi
    } > "$tmp_cfg"
    mcopy "$tmp_cfg" z:/repl.cfg
    rm -f "$tmp_cfg"
    if [ "$short_model" != "$model_long" ]; then
        echo "  [OK] Wrote repl.cfg (model=${short_model}) [alias for ${model_long}]"
    else
        echo "  [OK] Wrote repl.cfg (model=${short_model})"
    fi
else
    # Optional REPL config (key=value). If present, copy to root.
    if [ -f repl.cfg ]; then
        mcopy repl.cfg z:/
        echo "  [OK] Copied repl.cfg"
    fi
fi

# Optional splash screen (Cyberpunk Interface)
if [ -f splash.bmp ]; then
    mcopy splash.bmp z:/
    echo "  [OK] Copied splash.bmp"
fi

# Optional autorun scripts. If present, copy to root.
# This lets QEMU/CI run scripted REPL commands without manual typing.
for f in llmk-autorun*.txt; do
    [ -f "$f" ] || continue
    mcopy "$f" z:/
    echo "  [OK] Copied $f"
done

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
echo "  [OK] Copied startup.nsh"

# Cleanup
rm -f "$mtoolsrc_tmp"
echo "[OK] Image finalized"

# Show result
echo ""
echo "==============================================="
echo "BOOTABLE IMAGE CREATED"
echo "==============================================="
echo ""
echo "File: $(pwd)/$IMAGE"
echo "Size: $(ls -lh $IMAGE | awk '{print $5}')"
echo ""
echo "Next steps:"
echo "  1. Open Rufus on Windows"
echo "  2. Select your USB drive"
echo "  3. Click SELECT and choose: $IMAGE"
echo "  4. Partition scheme: GPT"
echo "  5. Target system: UEFI (non CSM)"
echo "  6. Click START"
echo ""
echo "Boot will show optimized matmul message"
