# Olympe

Olympe is a multi-pillar engine concept inspired by OS-G.

- The system is split into **pillars** (p01…p10…+) where each pillar owns a domain.
- Each pillar can define its own D+ policy dialect (LAW/PROOF), while sharing a minimal common bus.

## Layout

- `core_hermes/`: shared bus + cross-pillar contract surface.
- `pillars/p01` … `pillars/p10`: pillar-owned domains.

This folder is intentionally a skeleton for now; implementation will be filled pillar-by-pillar.
