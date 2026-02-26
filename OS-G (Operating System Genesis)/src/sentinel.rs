// src/sentinel.rs

// Sentinel: Observability & Policy Enforcement
// This module provides logic for monitoring behavior through the Journal
// and applying automated enforcement actions (throttling, warnings, quarantine).

use crate::warden::MemoryWarden;
use crate::journal::EventKind;
use crate::dplus::judge::LawSentinelRule;

const MAX_TRACKED_CELLS: usize = 16;
const TRACK_MASK: usize = MAX_TRACKED_CELLS - 1;

/// Sentinel state tracking cells, violations, and confidence level.
#[derive(Debug, Clone, Copy)]
pub struct SentinelState {
	// cell_id mod 16 -> violation_count
	// Simple circular buffer for tracking violations.
	// In a complete implementation, this would be a hash map or larger array.
	violations: [u8; MAX_TRACKED_CELLS],
	last_tick: u64,
}

impl SentinelState {
	pub const fn new() -> Self {
		Self {
			violations: [0; MAX_TRACKED_CELLS],
			last_tick: 0,
		}
	}
}

pub struct Sentinel;

impl Sentinel {
	/// Run one iteration of the Sentinel logic.
	/// 
	/// 1. Reads new events from the Journal.
	/// 2. Updates `SentinelState`.
	/// 3. Executes enforcement actions via `MemoryWarden`.
	/// 
	/// Returns the number of cells quarantined in this pass.
	pub fn run<const W: usize, const C: usize, const S: usize>(
		warden: &mut MemoryWarden<W, C, S>,
		state: &mut SentinelState,
        rules: &[LawSentinelRule],
	) -> usize {
        // Determine dynamic threshold from policy rules.
        // Default to 3 if no rule specifies otherwise.
        let mut threshold = 3u8;
        for r in rules {
            // Currenly LawSentinelRule applies to 'behavior.access_denied'
            // We use the last rule found (override).
            if r.violation_threshold > 0 {
                threshold = core::cmp::min(255, r.violation_threshold) as u8;
            }
        }

		let stats = warden.journal_stats();
		let current_len = stats.len;
		
		// Typically we'd track "last_processed_idx" to avoid re-scanning.
		// For prototype simplicity, we scan the whole buffer (size is small).
		
		// Reset tracking for this pass (stateless heuristic for now).
		// A stateful heuristic would accumulate violations over time.
		state.violations = [0; MAX_TRACKED_CELLS];

		for i in 0..current_len {
			if let Some(ev) = warden.journal_get(i) {
				if ev.tick < state.last_tick {
					// Skip old events (journal is ring buffer, simplistic check)
					continue;
				}
				
				if ev.kind == EventKind::AccessDenied {
					let idx = (ev.cell as usize) & TRACK_MASK;
					state.violations[idx] = state.violations[idx].saturating_add(1);
				}
			}
		}

		state.last_tick = warden.now_ticks();

		// Enforcement: Find misbehaving cells and Quarantine them.
		let mut quarantined_count = 0;
		let mut pending_quarantine = [0u32; MAX_TRACKED_CELLS];
		let mut pending_len = 0;

		for i in 0..MAX_TRACKED_CELLS {
			if state.violations[i] > threshold {
				// Re-scan to find actual CellId (since we only tracked by modulo).
				for j in 0..current_len {
					if let Some(ev) = warden.journal_get(j) {
						if ev.kind == EventKind::AccessDenied {
							let idx = (ev.cell as usize) & TRACK_MASK;
							if idx == i {
								// Found a candidate. Add to list if unique.
								let mut found = false;
								for k in 0..pending_len {
									if pending_quarantine[k] == ev.cell {
										found = true;
										break;
									}
								}
								if !found && pending_len < MAX_TRACKED_CELLS {
									pending_quarantine[pending_len] = ev.cell;
									pending_len += 1;
								}
							}
						}
					}
				}
			}
		}

		// Apply Quarantine Actions
		for i in 0..pending_len {
			if warden.quarantine_cell(pending_quarantine[i]).is_ok() {
				quarantined_count += 1;
			}
		}

		// Auto-Repair: Reclaim expired capabilities to free up memory.
		// This cleans up after quarantined cells or normal TTL expirations.
		let _ = warden.reclaim_expired();

		quarantined_count
	}
}
