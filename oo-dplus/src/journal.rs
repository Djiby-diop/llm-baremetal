use crate::types::{CapHandle, CellId, Zone};

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum EventKind {
    AllocateGranted,
    AllocateDenied,
    Freed,
    AccessDenied,
    Expired,
    Delegated,
    Quarantined,
    Reclaimed,
    MeritDecision,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct Event {
    pub tick: u64,
    pub cell: CellId,
    pub kind: EventKind,
    pub zone: Zone,
    pub handle_raw: u32,
    pub bytes: u64,
    pub info: u32,
}

impl Event {
    pub const fn new(
        tick: u64,
        cell: CellId,
        kind: EventKind,
        zone: Zone,
        handle: Option<CapHandle>,
        bytes: u64,
        info: u32,
    ) -> Self {
        Self {
            tick,
            cell,
            kind,
            zone,
            handle_raw: match handle {
                Some(h) => h.raw(),
                None => 0,
            },
            bytes,
            info,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct JournalStats {
    pub capacity: usize,
    pub len: usize,
    pub dropped: u64,
}

/// Fixed-size ring buffer journal.
pub struct Journal<const N: usize> {
    buf: [Event; N],
    head: usize,
    len: usize,
    dropped: u64,
}

impl<const N: usize> Journal<N> {
    pub const fn new() -> Self {
        Self {
            buf: [Event {
                tick: 0,
                cell: 0,
                kind: EventKind::AllocateDenied,
                zone: Zone::Normal,
                handle_raw: 0,
                bytes: 0,
                info: 0,
            }; N],
            head: 0,
            len: 0,
            dropped: 0,
        }
    }

    pub fn push(&mut self, ev: Event) {
        if N == 0 {
            self.dropped = self.dropped.saturating_add(1);
            return;
        }

        if self.len < N {
            let idx = (self.head + self.len) % N;
            self.buf[idx] = ev;
            self.len += 1;
        } else {
            // overwrite oldest
            self.buf[self.head] = ev;
            self.head = (self.head + 1) % N;
            self.dropped = self.dropped.saturating_add(1);
        }
    }

    pub fn stats(&self) -> JournalStats {
        JournalStats {
            capacity: N,
            len: self.len,
            dropped: self.dropped,
        }
    }

    pub fn get(&self, i: usize) -> Option<Event> {
        if i >= self.len {
            return None;
        }
        let idx = (self.head + i) % N;
        Some(self.buf[idx])
    }

    pub fn clear(&mut self) {
        self.head = 0;
        self.len = 0;
    }
}
