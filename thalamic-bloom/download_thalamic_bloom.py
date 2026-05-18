#!/usr/bin/env python3
"""
download_thalamic_bloom.py
Download thalamic-bloom weights from HuggingFace and export to bare-metal MAMB format.

Requirements:
    pip install torch huggingface_hub numpy

Usage:
    python download_thalamic_bloom.py
    python download_thalamic_bloom.py --out-dir /mnt/uefi-fat/
"""
import argparse
import os
import sys

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out-dir', default='.', help='Output directory (default: current)')
    p.add_argument('--arm', type=int, default=0, help='MIMO arm to export (0=general/OO)')
    args = p.parse_args()

    try:
        from huggingface_hub import hf_hub_download
    except ImportError:
        print("ERROR: pip install huggingface_hub")
        sys.exit(1)

    print("[download] Fetching thalamic_bloom_150m_oo.pth from batteryphil/thalamic-bloom...")
    local_path = hf_hub_download(
        repo_id='batteryphil/thalamic-bloom',
        filename='thalamic_bloom_150m_oo.pth',
        local_dir=args.out_dir
    )
    print(f"[download] Saved to: {local_path}")

    out_mamb = os.path.join(args.out_dir, 'thalamic_bloom.mamb')
    export_script = os.path.join(os.path.dirname(__file__), 'engine', 'ssm', 'export_thalamic_bloom.py')

    print(f"\n[download] Exporting to MAMB bare-metal format...")
    import subprocess
    result = subprocess.run(
        [sys.executable, export_script,
         '--model', local_path,
         '--out', out_mamb,
         '--arm', str(args.arm)],
        check=False
    )

    if result.returncode == 0:
        size_mb = os.path.getsize(out_mamb) / (1024*1024)
        print(f"\n✅ Ready: {out_mamb} ({size_mb:.1f} MB)")
        print("Next steps:")
        print("  1. Copy thalamic_bloom.mamb to your UEFI FAT partition (EFI/ folder)")
        print("  2. Boot OO")
        print("  3. In REPL: /ssm_load thalamic_bloom.mamb")
        print("  4. In REPL: /ssm_gen What are the 5 Organic Laws?")
    else:
        print("ERROR: export failed. Check above for details.")
        sys.exit(1)

if __name__ == '__main__':
    main()
