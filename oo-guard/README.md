# oo-guard

Host-side "immune guard" for the baremetal OO subsystem.

It validates that optional persisted artifacts stay ultra-minimal and machine-parseable:
- `OOJOUR.LOG` (marker-only journaling)
- `OOCONSULT.LOG` (compact consult summaries)
- `OOOUTCOME.LOG` (compact outcome markers)
- Optional `OO_DNA.DELTA` (key=value overlay suggestions)

## Usage

From `llm-baremetal/`:

- `cargo run --release --manifest-path oo-guard/Cargo.toml -- check --root .`

Notes:
- Missing files are skipped (not an error).
- Non-ASCII/control characters, unexpected formats, or oversized files fail the check.
