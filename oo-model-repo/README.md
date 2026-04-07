# oo-model-repo — SomaMind Cortex Training

Training pipeline for the **SomaMind cortex model** — a small OOSS (15M–60M param)
bare-metal neural network that runs as a fast pre-processor alongside Mamba-2.8B.

## Pipeline (Phase X → Z)

```
USB boot → /soma_export → soma_train.jsonl
                ↓
    export_soma_dataset.py  →  soma_dataset/
                ↓
    train_soma_cortex.py    →  models/cortex_oo.bin
                ↓
    train_soma_homeostatic.py → models/cortex_oo_homeostatic.bin
                ↓
    Copy to USB FAT → /cortex_load cortex_oo_homeostatic.bin
```

## Scripts

| Script | Phase | Purpose |
|--------|-------|---------|
| `export_soma_dataset.py` | X | Convert soma_train.jsonl → training dataset |
| `train_soma_cortex.py` | X | Train cortex SSM model (LM + domain cls) |
| `train_soma_homeostatic.py` | Z | Fine-tune with L_survival + L_routing + L_memory |

## Quick Start

```bash
# 1. Generate synthetic data (no USB needed for testing)
python scripts/export_soma_dataset.py --gen-synthetic --output soma_dataset/

# 2. Train cortex model
python scripts/train_soma_cortex.py --dataset soma_dataset/ --epochs 30

# 3. Fine-tune with homeostatic loss
python scripts/train_soma_homeostatic.py \
    --model models/cortex_oo_best.pt \
    --dataset soma_dataset/ \
    --epochs 20

# 4. Copy to USB and load on bare-metal
cp models/cortex_oo_homeostatic.bin /mnt/usb/EFI/BOOT/
# On bare-metal: /cortex_load cortex_oo_homeostatic.bin
```

## Homeostatic Loss (Phase Z)

```
L_total = L_lm + λ_s * L_survival + λ_r * L_routing + λ_m * L_memory

L_survival  = penalty on halt-trigger token probability (HALT/PANIC/FATAL...)
L_routing   = domain classification CE + KL vs keyword-based soft labels
L_memory    = -reward for high probability on memory-coherent continuations
```

Defaults: λ_s=0.3, λ_r=0.2, λ_m=0.1

## OOSS Binary Format

```
Offset  Size  Field
0       4     magic = 0x4F4F5353 ("OOSS")
4       4     version = 1
8       4     vocab_size
12      4     d_model
16      4     n_layers
20      4     n_domains = 7
24      4     total_floats
28      4     n_tensors
32      32    padding
64      ...   F32 weights (little-endian, row-major)
```

Load on bare-metal: `/cortex_load <filename>`
