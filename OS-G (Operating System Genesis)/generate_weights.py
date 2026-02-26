import argparse
import struct


def main() -> int:
    p = argparse.ArgumentParser(description="Generate OS-G WEIGHTS.BIN (layers * dim * dim f32).")
    p.add_argument("--dim", type=int, default=128, help="Model dim (default: 128)")
    p.add_argument("--layers", type=int, default=1, help="Number of layers (default: 1)")
    p.add_argument("--value", type=float, default=0.02, help="Fill value for all weights (default: 0.02)")
    p.add_argument("--out", type=str, default="weights.bin", help="Output path (default: weights.bin)")
    p.add_argument(
        "--header",
        action="store_true",
        help="Write 16-byte header (magic OSGW, ver=1, dim, layers) before weights payload",
    )
    args = p.parse_args()

    if args.dim <= 0:
        raise SystemExit("--dim must be > 0")
    if args.layers <= 0:
        raise SystemExit("--layers must be > 0")

    floats = args.layers * args.dim * args.dim
    byte_len = floats * 4

    with open(args.out, "wb") as f:
        if args.header:
            # Header layout (little-endian):
            # [0..4]   magic: b"OSGW"
            # [4..6]   u16 version = 1
            # [6..8]   u16 reserved = 0
            # [8..12]  u32 dim
            # [12..16] u32 layers
            f.write(b"OSGW")
            f.write((1).to_bytes(2, "little"))
            f.write((0).to_bytes(2, "little"))
            f.write(int(args.dim).to_bytes(4, "little"))
            f.write(int(args.layers).to_bytes(4, "little"))
        packed = struct.pack("f", float(args.value))
        for _ in range(floats):
            f.write(packed)

    total = byte_len + (16 if args.header else 0)
    hdr = ", header=on" if args.header else ""
    print(f"Created {args.out} ({total} bytes, layers={args.layers}, dim={args.dim}, value={args.value}{hdr})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())