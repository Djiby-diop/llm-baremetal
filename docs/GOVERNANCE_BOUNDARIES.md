# Governance Boundaries for OS-G Runtime

## 1. Scope of Authority

Operating System Genesis (OS-G) is the sovereign bare-metal runtime for the Operating Organism. Its authority is granted by the D+ Governance Layer.

### OS-G Authority (The Soma)
- Hardware interaction (MMIO, Port I/O, Interrupts).
- Basic memory management (Stack/Heap primitives).
- Driver execution and interrupt handling.
- Cognitive inference execution (LLM/SSM loop).

### D+ Authority (The Warden)
- Policy enforcement (Memory bounds, TTLs, Quotas).
- Mutation validation (Gating self-patching code).
- Health monitoring and isolation (Quarantine).
- Final verdict on high-impact actions.

## 2. Hand-off Protocols

### Boot-time Hand-off
The UEFI loader initializes the Soma, then immediately loads the `D+` policy table. OS-G is not considered "Active" until the Warden confirms policy integrity.

### Runtime Arbitration
When a Soma component (e.g., a Driver or a Cell) requests an action that breaches the local "Green Zone":
1. OS-G freezes the requesting thread.
2. An event is emitted to the Warden.
3. The Warden evaluates against `LAW` and `PROOF`.
4. If denied, the Warden triggers a `HEAL` (Restore/Reclaim) sequence.

## 3. Boundary Invariants

- **No Malloc in critical paths**: OS-G must use static or slab allocation unless explicitly authorized by a temporary D+ memory grant.
- **Zero-Copy requirement**: Data crossing the OS-G / Host boundary must stay in the same physical memory frame when possible.
## 4. Architecture Note: Boot Path to D+ Guardrails

The following sequence ensures that no unmanaged code executes during the critical transition from UEFI to the Operating Organism:

1.  **UEFI Entry**: The `llama2_efi_final.c` loader starts.
2.  **Hardware Discovery**: `oo_hardware_report()` enumerates PCI and storage.
3.  **Warden Init**: The `D+ Warden` is initialized in a protected memory segment.
4.  **Policy Loading (Guardrail 1)**: `policy.dplus` is read from the EFI partition. If the signature is invalid or the policy is missing, the system halts.
5.  **NeuralFS Mounting (Guardrail 2)**: Persistent state is verified.
6.  **Mutation Check (Guardrail 3)**: Any "Mutated" drivers in `engine/drivers/` are compared against the active `D+` whitelist.
7.  **Soma Activation**: The inference loop starts ONLY after the Warden signals `GREEN_ZONE`.

Each step is journaled in `OOJOUR.LOG` with a unique `op:<id>` that can be replayed for audit.
