from __future__ import annotations

import argparse
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from src.oo_model import load_config


def resolve_dataset_path(path_value: str) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return REPO_ROOT / path


def main() -> None:
    parser = argparse.ArgumentParser(description="Dry-run trainer for oo-v1.")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    config = load_config(args.config)

    print(f"model: {config['model_name']}")
    print(f"params_target: {config['params_target']}")
    print(f"context_length: {config['context_length']}")
    print(f"dataset/train: {config['dataset']['train']}")
    print(f"quantization_target: {config['integration']['quantization_target']}")

    for split_name, path_value in config["dataset"].items():
        dataset_path = resolve_dataset_path(path_value)
        print(f"dataset/{split_name}_exists: {dataset_path.exists()} -> {dataset_path}")

    if args.dry_run:
        print("dry-run only: no training launched")
        return

    raise SystemExit("training loop not implemented yet; start with --dry-run")


if __name__ == "__main__":
    main()
