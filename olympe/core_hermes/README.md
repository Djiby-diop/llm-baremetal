# CORE_HERMES

CORE_HERMES is the minimal shared bus between Olympe pillars.

Goals (initial):

- Define the smallest stable interface between pillars.
- Provide a place for shared invariants (naming, message shape, error semantics).
- Keep policy integration explicit: each pillar may have its own D+ dialect, but the bus should allow verifying/attesting pillar actions.

## Contract

- `hermes.h`: minimal C contract surface (no transport implied).
- `hermes.md`: human-readable contract notes.
