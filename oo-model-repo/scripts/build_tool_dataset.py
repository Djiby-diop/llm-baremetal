"""
Build a compact tool-use dataset for the OO Mamba phase-4 fine-tune.

Output rows (JSONL):
  {
    "instruction": "...",
    "dark_loops": 6,
    "tool_command": "python --version",
    "tool_result": "Python 3.13.3",
    "post_tool_loops": 2,
    "response": "Python 3.13.3 is installed.",
    "domain": "tool"
  }
"""
from __future__ import annotations

import json
import random
from pathlib import Path

random.seed(42)

TASKS = [
    {
        "instruction": "Check which Python version is installed on this machine.",
        "tool_command": "python --version",
        "tool_result": "Python 3.13.3",
        "response": "Python 3.13.3 is installed on this machine.",
    },
    {
        "instruction": "Show the current working directory.",
        "tool_command": "pwd",
        "tool_result": r"C:\Users\djibi\OneDrive\Bureau\baremetal\oo-model",
        "response": "The current working directory is the oo-model workspace.",
    },
    {
        "instruction": "List the top-level files in the repository.",
        "tool_command": "Get-ChildItem -Name",
        "tool_result": "configs\ndata\ndocs\nnotebooks\nscripts\nsrc\nREADME.md\nROADMAP.md",
        "response": "The repository contains configs, data, docs, notebooks, scripts, src, README.md, and ROADMAP.md.",
    },
    {
        "instruction": "Read the end of the latest training log.",
        "tool_command": "Get-Content checkpoints/oo-native-v1/train.log -Tail 5",
        "tool_result": "step=4960 loss=1.84\nstep=4970 loss=1.79\nstep=4980 loss=1.76\nstep=4990 loss=1.74\nstep=5000 loss=1.71",
        "response": "The training log shows the loss continuing to decrease through step 5000.",
    },
    {
        "instruction": "Check free space on the main drive.",
        "tool_command": "Get-PSDrive C | Select-Object Free,Used",
        "tool_result": "Free         Used\n----         ----\n112.4 GB     358.7 GB",
        "response": "The main drive still has more than 100 GB of free space.",
    },
    {
        "instruction": "Show whether CUDA is available to PyTorch.",
        "tool_command": "python -c \"import torch; print(torch.cuda.is_available())\"",
        "tool_result": "False",
        "response": "CUDA is not available in the current PyTorch environment.",
    },
    {
        "instruction": "Display the available checkpoints in the project.",
        "tool_command": "Get-ChildItem checkpoints -Name",
        "tool_result": "oo-mamba-phase1\noo-mamba-phase2-halt\noo-native-v1",
        "response": "The available checkpoints include the Mamba phase-1, Mamba phase-2 halt head, and OO native checkpoints.",
    },
    {
        "instruction": "Inspect the tokenizer vocabulary size saved for OO Native.",
        "tool_command": "python -c \"import json; print(len(json.load(open('checkpoints/oo-native-v1/tokenizer.json'))['vocab']))\"",
        "tool_result": "833",
        "response": "The saved OO Native tokenizer currently contains 833 vocabulary entries.",
    },
]

PRE_LOOPS = [3, 4, 5, 6]
POST_LOOPS = [1, 2]


def build(output_path: str = "data/processed/tool_use.jsonl") -> None:
    out_path = Path(output_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    rows = []
    for task in TASKS:
        for pre in PRE_LOOPS:
            for post in POST_LOOPS:
                row = {
                    **task,
                    "dark_loops": pre,
                    "post_tool_loops": post,
                    "domain": "tool",
                }
                rows.append(row)

    random.shuffle(rows)
    with out_path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    print(f"[tool-dataset] Wrote {len(rows)} rows -> {out_path}")


if __name__ == "__main__":
    build()