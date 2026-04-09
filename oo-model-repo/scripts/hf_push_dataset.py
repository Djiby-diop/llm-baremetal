#!/usr/bin/env python3
"""
hf_push_dataset.py — OO Training Feedback Loop: Step 3

Pushes data/processed/train.jsonl + val.jsonl + metadata.json
to the HuggingFace dataset repo: djibydiop/llm-baremetal-training

Requirements: pip install huggingface_hub
Token: set HF_TOKEN env variable or pass --token

Usage:
  HF_TOKEN=hf_xxx python scripts/hf_push_dataset.py
  python scripts/hf_push_dataset.py --token hf_xxx --dry-run
  python scripts/hf_push_dataset.py --repo djibydiop/my-other-repo
"""

import argparse
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT  = SCRIPT_DIR.parent
OUT_DIR    = REPO_ROOT / "data" / "processed"

DEFAULT_HF_REPO = "djibydiop/llm-baremetal-training"

def push_to_hf(repo_id: str, token: str, dry_run: bool):
    try:
        from huggingface_hub import HfApi, CommitOperationAdd
    except ImportError:
        print("[ERROR] pip install huggingface_hub", file=sys.stderr)
        sys.exit(1)

    api = HfApi()

    # Verify repo exists or create it
    try:
        api.repo_info(repo_id=repo_id, repo_type="dataset", token=token)
    except Exception:
        print(f"[hf_push] Creating dataset repo {repo_id}...")
        if not dry_run:
            api.create_repo(repo_id=repo_id, repo_type="dataset",
                            private=False, token=token)

    # Collect files to upload
    CANDIDATE_FILES = [
        "train.jsonl",
        "val.jsonl",
        "metadata.json",
        "mamba3_training.jsonl",   # Phase N: Mamba3 merged dataset
        "mamba3_stats.json",       # Phase N: stats
    ]
    files_to_push = []
    for fname in CANDIDATE_FILES:
        fpath = OUT_DIR / fname
        if fpath.exists():
            files_to_push.append(fpath)
        else:
            print(f"[WARN] {fname} not found — skipping")

    if not files_to_push:
        print("[ERROR] No files to push. Run generate_mamba3_dataset.py or auto_export_training.py first.", file=sys.stderr)
        sys.exit(1)

    # Load metadata for commit message
    meta_path = OUT_DIR / "metadata.json"
    meta = {}
    if meta_path.exists():
        meta = json.loads(meta_path.read_text())

    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    commit_msg = (
        f"[OO auto-export] {ts} | "
        f"train={meta.get('train', '?')} val={meta.get('val', '?')} | "
        f"domains: {', '.join(sorted(meta.get('domains', {}).keys()))}"
    )

    print(f"[hf_push] repo={repo_id}")
    print(f"[hf_push] commit: {commit_msg}")
    print(f"[hf_push] files: {[f.name for f in files_to_push]}")

    if dry_run:
        print("[DRY RUN] — no files pushed")
        return

    operations = [
        CommitOperationAdd(
            path_in_repo=f"data/{fp.name}",
            path_or_fileobj=str(fp),
        )
        for fp in files_to_push
    ]

    api.create_commit(
        repo_id=repo_id,
        repo_type="dataset",
        operations=operations,
        commit_message=commit_msg,
        token=token,
    )
    print(f"[hf_push] ✓ pushed {len(operations)} files to {repo_id}")
    print(f"  https://huggingface.co/datasets/{repo_id}")

def main():
    ap = argparse.ArgumentParser(description="Push OO training data to HuggingFace")
    ap.add_argument('--repo',    default=DEFAULT_HF_REPO)
    ap.add_argument('--token',   default=os.environ.get('HF_TOKEN', ''))
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    if not args.token and not args.dry_run:
        print("[ERROR] Set HF_TOKEN env var or pass --token", file=sys.stderr)
        sys.exit(1)

    push_to_hf(args.repo, args.token, args.dry_run)

if __name__ == '__main__':
    main()
