#!/usr/bin/env bash
# tools/oo-installer.sh — OO USB Installer  Phase 4C
# =====================================================
# Creates a bootable USB drive with OO system:
#   Partition 1: ESP (FAT32, 512MB) with EFI/BOOT/BOOTx64.EFI
#   Partition 2: OO data (FAT32, remainder) with models + config
#
# Usage:
#   sudo ./tools/oo-installer.sh /dev/sdX
#   sudo ./tools/oo-installer.sh /dev/sdX --model models/stories260K.bin
#   sudo ./tools/oo-installer.sh /dev/sdX --dry-run
#
# CAUTION: This ERASES the target device!

set -euo pipefail
IFS=$'\n\t'

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${GREEN}[ok]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC} $*"; }
error() { echo -e "${RED}[err]${NC}  $*"; exit 1; }
step()  { echo -e "${CYAN}[>>]${NC}  $*"; }

# ── Parse args ───────────────────────────────────────────────────────────────
DEVICE="${1:-}"
MODEL=""
DRY_RUN=0
PROXY_HOST="10.0.2.2"
PROXY_PORT=8080

shift || true
while [[ $# -gt 0 ]]; do
    case "$1" in
        --model)     MODEL="$2"; shift 2 ;;
        --dry-run)   DRY_RUN=1; shift ;;
        --proxy)     PROXY_HOST="$2"; PROXY_PORT="$3"; shift 3 ;;
        *) warn "Unknown arg: $1"; shift ;;
    esac
done

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
BUILD="$ROOT/build"
EFI="$BUILD/llama2.efi"
MODELS_DIR="$ROOT/models"

echo -e "\n${BOLD}  ╔═════════════════════════════════╗${NC}"
echo -e "${BOLD}  ║   OO USB Installer — Phase 4C   ║${NC}"
echo -e "${BOLD}  ╚═════════════════════════════════╝${NC}\n"

# ── Checks ───────────────────────────────────────────────────────────────────
[[ -z "$DEVICE" ]] && error "Usage: $0 /dev/sdX [--model path] [--dry-run]"
[[ $EUID -ne 0 ]] && error "Must run as root (sudo)"
[[ ! -b "$DEVICE" ]] && error "Not a block device: $DEVICE"
[[ ! -f "$EFI" ]]   && error "llama2.efi not found: $EFI  (run 'make' first)"

# Safety: refuse known system disks
for sys_disk in /dev/sda /dev/nvme0n1 /dev/mmcblk0; do
    if [[ "$DEVICE" == "$sys_disk" ]]; then
        error "Refusing to write to likely system disk: $DEVICE"
    fi
done

DEVICE_SIZE=$(blockdev --getsize64 "$DEVICE" 2>/dev/null || echo 0)
DEVICE_GB=$(( DEVICE_SIZE / 1024 / 1024 / 1024 ))
info "Target: $DEVICE ($DEVICE_GB GB)"
info "EFI:    $EFI ($(du -h "$EFI" | cut -f1))"

if [[ -n "$MODEL" ]]; then
    [[ ! -f "$MODEL" ]] && error "Model not found: $MODEL"
    info "Model:  $MODEL ($(du -h "$MODEL" | cut -f1))"
fi

if [[ $DRY_RUN -eq 1 ]]; then
    warn "DRY RUN — no changes will be made"
fi

# Confirm
echo -e "\n${RED}${BOLD}  ⚠ WARNING: $DEVICE will be COMPLETELY ERASED ⚠${NC}"
read -r -p "  Type 'YES' to confirm: " confirm
[[ "$confirm" != "YES" ]] && error "Aborted"

# ── Unmount any existing partitions ──────────────────────────────────────────
step "Unmounting $DEVICE..."
for part in "${DEVICE}"[0-9]* "${DEVICE}p"[0-9]*; do
    if [[ -b "$part" ]]; then
        umount "$part" 2>/dev/null || true
    fi
done

# ── Partition ────────────────────────────────────────────────────────────────
step "Partitioning $DEVICE (GPT)..."
if [[ $DRY_RUN -eq 0 ]]; then
    sgdisk --zap-all "$DEVICE"
    sgdisk \
        --new=1:0:+512M --typecode=1:ef00 --change-name=1:"OO-EFI" \
        --new=2:0:0     --typecode=2:0700 --change-name=2:"OO-DATA" \
        "$DEVICE"
    partprobe "$DEVICE" 2>/dev/null || sleep 1
fi

# Determine partition names
if [[ "$DEVICE" =~ nvme|mmcblk ]]; then
    PART1="${DEVICE}p1"; PART2="${DEVICE}p2"
else
    PART1="${DEVICE}1";  PART2="${DEVICE}2"
fi
info "Partitions: $PART1 (ESP) $PART2 (data)"

# ── Format ───────────────────────────────────────────────────────────────────
step "Formatting partitions (FAT32)..."
if [[ $DRY_RUN -eq 0 ]]; then
    mkfs.fat -F 32 -n "OO-EFI"  "$PART1"
    mkfs.fat -F 32 -n "OO-DATA" "$PART2"
fi

# ── Mount + copy ESP ─────────────────────────────────────────────────────────
MNT_ESP="/tmp/oo-install-esp"
MNT_DATA="/tmp/oo-install-data"
mkdir -p "$MNT_ESP" "$MNT_DATA"

step "Mounting ESP → $MNT_ESP..."
if [[ $DRY_RUN -eq 0 ]]; then
    mount "$PART1" "$MNT_ESP"
    trap "umount $MNT_ESP $MNT_DATA 2>/dev/null; rmdir $MNT_ESP $MNT_DATA 2>/dev/null" EXIT
fi

# EFI structure
step "Installing OO EFI loader..."
if [[ $DRY_RUN -eq 0 ]]; then
    mkdir -p "$MNT_ESP/EFI/BOOT"
    cp "$EFI" "$MNT_ESP/EFI/BOOT/BOOTx64.EFI"
    info "Copied: BOOTx64.EFI ($(du -h "$MNT_ESP/EFI/BOOT/BOOTx64.EFI" | cut -f1))"
fi

# Write startup.nsh for EFI shell auto-launch
step "Writing startup.nsh..."
if [[ $DRY_RUN -eq 0 ]]; then
    cat > "$MNT_ESP/startup.nsh" << 'NSH'
@echo -off
echo OO - Operating Organism - Booting...
fs0:EFI\BOOT\BOOTx64.EFI
NSH
fi

# Write OO config (REPL preload)
step "Writing OO config..."
if [[ $DRY_RUN -eq 0 ]]; then
    cat > "$MNT_ESP/oo.cfg" << EOF
# OO Configuration — generated by oo-installer.sh
net_server=$PROXY_HOST $PROXY_PORT
tls_proxy=$PROXY_HOST $PROXY_PORT
fed_port=8181
diop_preset=base
log_level=2
EOF
fi

# ── Mount + copy data ─────────────────────────────────────────────────────────
step "Mounting data → $MNT_DATA..."
if [[ $DRY_RUN -eq 0 ]]; then
    mount "$PART2" "$MNT_DATA"
fi

# Copy model if provided
if [[ -n "$MODEL" ]]; then
    step "Copying model: $MODEL..."
    if [[ $DRY_RUN -eq 0 ]]; then
        mkdir -p "$MNT_DATA/models"
        cp "$MODEL" "$MNT_DATA/models/"
        MNAME=$(basename "$MODEL")
        echo "model_path=\models\$MNAME" >> "$MNT_ESP/oo.cfg"
        info "Model copied: $MNAME"
    fi
fi

# Copy tools (oracle proxy)
step "Copying oracle proxy..."
if [[ $DRY_RUN -eq 0 ]]; then
    mkdir -p "$MNT_DATA/tools"
    [[ -f "$ROOT/tools/oo-oracle-proxy.py" ]] && \
        cp "$ROOT/tools/oo-oracle-proxy.py" "$MNT_DATA/tools/"
    [[ -f "$ROOT/tools/README.md" ]] && \
        cp "$ROOT/tools/README.md" "$MNT_DATA/tools/"
fi

# Copy any existing models
if [[ -d "$MODELS_DIR" ]]; then
    step "Copying existing models from $MODELS_DIR..."
    if [[ $DRY_RUN -eq 0 ]]; then
        mkdir -p "$MNT_DATA/models"
        find "$MODELS_DIR" -maxdepth 1 -name "*.bin" -o -name "*.gguf" | \
            while read -r f; do
                size=$(du -m "$f" | cut -f1)
                if [[ $size -lt 500 ]]; then   # Skip >500MB models (USB space)
                    cp "$f" "$MNT_DATA/models/"
                    info "Model: $(basename "$f") (${size}MB)"
                else
                    warn "Skipping large model: $(basename "$f") (${size}MB > 500MB)"
                fi
            done
    fi
fi

# ── Sync + unmount ────────────────────────────────────────────────────────────
step "Syncing..."
if [[ $DRY_RUN -eq 0 ]]; then
    sync
    umount "$MNT_ESP" 2>/dev/null || true
    umount "$MNT_DATA" 2>/dev/null || true
    rmdir "$MNT_ESP" "$MNT_DATA" 2>/dev/null || true
    trap - EXIT
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}${GREEN}  ✓ OO USB Installer Complete${NC}\n"
echo -e "  Device  : ${BOLD}$DEVICE${NC}"
echo -e "  ESP     : ${BOLD}$PART1${NC} (512MB, FAT32)"
echo -e "  Data    : ${BOLD}$PART2${NC} (remainder, FAT32)"
echo -e ""
echo -e "  Boot order: Insert USB → power on → select USB in BIOS/UEFI"
echo -e "  Or: set USB as first boot device in UEFI settings"
echo -e ""
echo -e "  Oracle proxy: Run on host PC before booting OO:"
echo -e "    python3 tools/oo-oracle-proxy.py --port $PROXY_PORT"
echo -e ""
echo -e "  OO REPL commands:"
echo -e "    /net_server <host-ip> $PROXY_PORT"
echo -e "    /net_oracle gpt4 hello"
echo -e "    /patch_oracle"
echo -e "    /fed_discover"
echo -e ""
if [[ $DRY_RUN -eq 1 ]]; then
    warn "DRY RUN — no actual changes made"
fi
