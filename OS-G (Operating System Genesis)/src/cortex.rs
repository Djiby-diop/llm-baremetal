use crate::journal::{Event, EventKind};
use crate::types::{CellId, MemIntent};

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct CortexConfig {
    pub enabled: bool,
    pub prefetch_repeat: u8,
}

impl Default for CortexConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            prefetch_repeat: 3,
        }
    }
}

#[derive(Copy, Clone)]
struct Entry {
    used: bool,
    cell: CellId,
    last_bytes: u64,
    repeats: u8,
}

impl Entry {
    const fn empty() -> Self {
        Self {
            used: false,
            cell: 0,
            last_bytes: 0,
            repeats: 0,
        }
    }
}

/// Phase 7 (MVP): heuristic cortex that *suggests* prefetch intents.
///
/// It observes `AllocateGranted` events and if a cell repeats the same allocation
/// size `prefetch_repeat` times, it suggests pre-allocating one more chunk of that size.
pub struct HeuristicCortex<const MAX_TRACKED: usize> {
    cfg: CortexConfig,
    entries: [Entry; MAX_TRACKED],
}

impl<const MAX_TRACKED: usize> HeuristicCortex<MAX_TRACKED> {
    pub const fn new() -> Self {
        Self {
            cfg: CortexConfig {
                enabled: false,
                prefetch_repeat: 3,
            },
            entries: [Entry::empty(); MAX_TRACKED],
        }
    }

    pub fn set_config(&mut self, cfg: CortexConfig) {
        self.cfg = cfg;
        if !self.cfg.enabled {
            for e in self.entries.iter_mut() {
                *e = Entry::empty();
            }
        }
    }

    pub fn config(&self) -> CortexConfig {
        self.cfg
    }

    pub fn observe(&mut self, ev: &Event) {
        if !self.cfg.enabled {
            return;
        }
        if ev.kind != EventKind::AllocateGranted {
            return;
        }

        let cell = ev.cell;
        let bytes = ev.bytes;

        let mut slot_idx: Option<usize> = None;
        for i in 0..MAX_TRACKED {
            let e = &self.entries[i];
            if e.used && e.cell == cell {
                slot_idx = Some(i);
                break;
            }
        }
        if slot_idx.is_none() {
            for i in 0..MAX_TRACKED {
                if !self.entries[i].used {
                    slot_idx = Some(i);
                    break;
                }
            }
        }

        if let Some(i) = slot_idx {
            let e = &mut self.entries[i];
            if !e.used {
                e.used = true;
                e.cell = cell;
                e.last_bytes = bytes;
                e.repeats = 1;
                return;
            }

            if e.last_bytes == bytes && bytes != 0 {
                e.repeats = e.repeats.saturating_add(1);
            } else {
                e.last_bytes = bytes;
                e.repeats = 1;
            }
        }
    }

    pub fn suggest_prefetch(&self, cell: CellId) -> Option<MemIntent> {
        if !self.cfg.enabled {
            return None;
        }

        let e = self.entries.iter().find(|e| e.used && e.cell == cell)?;
        if e.repeats < self.cfg.prefetch_repeat {
            return None;
        }
        if e.last_bytes == 0 {
            return None;
        }

        let mut intent = MemIntent::new(cell, e.last_bytes);
        intent.priority = 1; // low priority suggestion
        intent.label = 0xC0_7E_00_01; // "CORTEX" marker label (stable)
        Some(intent)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::journal::Event;
    use crate::types::Zone;

    #[test]
    fn suggests_after_repeats() {
        let mut c: HeuristicCortex<4> = HeuristicCortex::new();
        c.set_config(CortexConfig {
            enabled: true,
            prefetch_repeat: 3,
        });

        let ev = Event::new(0, 7, EventKind::AllocateGranted, Zone::Normal, None, 4096, 0);
        c.observe(&ev);
        c.observe(&ev);
        assert!(c.suggest_prefetch(7).is_none());
        c.observe(&ev);

        let sug = c.suggest_prefetch(7).unwrap();
        assert_eq!(sug.owner, 7);
        assert_eq!(sug.bytes, 4096);
        assert_eq!(sug.priority, 1);
        assert_eq!(sug.label, 0xC0_7E_00_01);
    }
}
