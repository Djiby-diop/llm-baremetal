# CORE_HERMES Contract (v0.1)

CORE_HERMES defines the smallest stable contract for cross-pillar messaging.

## Non-goals (for now)

- No transport is mandated (shared memory, firmware calls, sockets, etc.).
- No wire format is standardized yet.
- No cryptography/policy is implied by default.

## Message shape

The canonical message shape is represented by the C struct in `hermes.h`:

- `version_major`, `version_minor`: contract version.
- `kind`: `COMMAND`, `EVENT`, or `RESPONSE`.
- `flags`: reserved.
- `payload_len`: payload size in bytes (validated by the chosen transport).
- `correlation_id`: used to pair a `RESPONSE` to a `COMMAND`.
- `source`, `dest`: pillar-defined identifiers.

## Compatibility

- `version_major` mismatch: reject.
- `version_minor` higher than supported: may reject or ignore unknown fields depending on implementation.
