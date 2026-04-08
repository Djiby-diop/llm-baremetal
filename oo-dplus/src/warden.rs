use crate::alloc::bitmap::{AllocError, AllocRange, BitmapPageAllocator, PAGE_SIZE};
use crate::journal::{Event, EventKind, Journal};
use crate::policy_vm::{PolicyOutcome, PolicyProgram, PolicyVerdict};
use crate::types::{Access, CapHandle, CellId, MemIntent, Rights, Zone};
use core::cell::UnsafeCell;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum MemError {
    NotInitialized,
    OutOfMemory,
    QuotaExceeded,
    InvalidHandle,
    Expired,
    AccessDenied,
    InvalidRequest,
    RateLimited,
    PolicyDenied,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct CapMeta {
    pub owner: CellId,
    pub rights: Rights,
    pub sandbox: bool,
    pub zone: Zone,
    pub label: u32,
    pub bytes: u64,
    /// 0 means "never expires".
    pub expires_at: u64,
}

#[derive(Copy, Clone)]
pub struct WardenSnapshot<const BITMAP_WORDS: usize, const MAX_CAPS: usize, const MAX_CELLS: usize> {
    now_ticks: u64,
    alloc_normal: BitmapPageAllocator<BITMAP_WORDS>,
    alloc_sandbox: BitmapPageAllocator<BITMAP_WORDS>,
    caps: [CapEntry; MAX_CAPS],
    cells: [CellQuota; MAX_CELLS],
    policy: PolicyProgram<64>,
}

#[derive(Copy, Clone)]
struct CapEntry {
    used: bool,
    generation: u16,
    owner: CellId,
    start_page: u32,
    page_count: u32,
    rights: Rights,
    zone: Zone,
    sandbox: bool,
    label: u32,
    expires_at: u64,
    bytes: u64,
    owns_pages: bool,
}

impl CapEntry {
    const fn empty() -> Self {
        Self {
            used: false,
            generation: 1,
            owner: 0,
            start_page: 0,
            page_count: 0,
            rights: Rights::NONE,
            zone: Zone::Normal,
            sandbox: false,
            label: 0,
            expires_at: 0,
            bytes: 0,
            owns_pages: false,
        }
    }
}

#[derive(Copy, Clone)]
struct CellQuota {
    used: bool,
    cell: CellId,
    limit_bytes: u64,
    used_bytes: u64,
    rate_limit_bytes: u64,
    rate_window_ticks: u64,
    rate_window_start: u64,
    rate_used_bytes: u64,
}

impl CellQuota {
    const fn empty() -> Self {
        Self {
            used: false,
            cell: 0,
            limit_bytes: 0,
            used_bytes: 0,
            rate_limit_bytes: 0,
            rate_window_ticks: 0,
            rate_window_start: 0,
            rate_used_bytes: 0,
        }
    }
}

/// A minimal "Memory Warden" prototype:
/// - contiguous page allocation (bitmap)
/// - capability table (handles + generation)
/// - per-cell quota accounting
/// - TTL expiration
pub struct MemoryWarden<const BITMAP_WORDS: usize, const MAX_CAPS: usize, const MAX_CELLS: usize> {
    initialized: bool,
    now_ticks: u64,
    alloc_normal: BitmapPageAllocator<BITMAP_WORDS>,
    alloc_sandbox: BitmapPageAllocator<BITMAP_WORDS>,
    caps: [CapEntry; MAX_CAPS],
    cells: [CellQuota; MAX_CELLS],
    policy: PolicyProgram<64>,
    journal: UnsafeCell<Journal<256>>,
}

impl<const BITMAP_WORDS: usize, const MAX_CAPS: usize, const MAX_CELLS: usize>
    MemoryWarden<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>
{
    pub const fn new() -> Self {
        Self {
            initialized: false,
            now_ticks: 0,
            alloc_normal: BitmapPageAllocator::new(),
            alloc_sandbox: BitmapPageAllocator::new(),
            caps: [CapEntry::empty(); MAX_CAPS],
            cells: [CellQuota::empty(); MAX_CELLS],
            policy: PolicyProgram::new_disabled(),
            journal: UnsafeCell::new(Journal::new()),
        }
    }

    /// # Safety
    /// `base_addr..base_addr+bytes` must be RAM usable for page allocation.
    pub unsafe fn init(&mut self, base_addr: usize, bytes: usize) {
        self.alloc_normal.init(base_addr, bytes);
        self.alloc_sandbox.init(0, 0);
        self.initialized = true;
        self.now_ticks = 0;

        for c in self.caps.iter_mut() {
            *c = CapEntry::empty();
        }
        for c in self.cells.iter_mut() {
            *c = CellQuota::empty();
        }

        self.policy.clear();

        self.journal_clear();
    }

    /// Initialize with separate RAM ranges for normal and sandbox allocations.
    ///
    /// # Safety
    /// Both regions must refer to usable RAM.
    pub unsafe fn init_zones(
        &mut self,
        normal_base: usize,
        normal_bytes: usize,
        sandbox_base: usize,
        sandbox_bytes: usize,
    ) {
        self.alloc_normal.init(normal_base, normal_bytes);
        self.alloc_sandbox.init(sandbox_base, sandbox_bytes);
        self.initialized = true;
        self.now_ticks = 0;

        for c in self.caps.iter_mut() {
            *c = CapEntry::empty();
        }
        for c in self.cells.iter_mut() {
            *c = CellQuota::empty();
        }

        self.policy.clear();

        self.journal_clear();
    }

    pub fn tick(&mut self, delta: u64) {
        self.now_ticks = self.now_ticks.saturating_add(delta);
    }

    pub fn journal_stats(&self) -> crate::journal::JournalStats {
        // Safe under the single-writer assumption (warden is the only mutator).
        unsafe { (&*self.journal.get()).stats() }
    }

    pub fn journal_get(&self, i: usize) -> Option<Event> {
        unsafe { (&*self.journal.get()).get(i) }
    }

    pub fn journal_clear(&mut self) {
        unsafe { (&mut *self.journal.get()).clear() }
    }

    fn journal_push(&self, ev: Event) {
        unsafe { (&mut *self.journal.get()).push(ev) }
    }

    /// Record a merit/policy decision into the journal.
    ///
    /// Encoding (stable, deterministic):
    /// - `Event.kind` = `MeritDecision`
    /// - `Event.zone` = default zone (Normal/Sandbox)
    /// - `Event.bytes` packs caps: low32=bytes_cap, high32=ttl_cap_ms
    /// - `Event.info` packs: bits0..7=score, bit8=default_sandbox, bits16..31=reasons_bits (low 16)
    pub fn journal_merit_decision(
        &self,
        cell: CellId,
        default_zone: Zone,
        score_0_100: u8,
        reasons_bits: u32,
        bytes_cap: Option<u64>,
        ttl_cap_ms: Option<u64>,
    ) {
        if self.ensure_init().is_err() {
            return;
        }

        let bytes_cap_u32 = bytes_cap
            .unwrap_or(0)
            .min(u32::MAX as u64) as u32;
        let ttl_cap_u32 = ttl_cap_ms
            .unwrap_or(0)
            .min(u32::MAX as u64) as u32;

        let packed_caps = (bytes_cap_u32 as u64) | ((ttl_cap_u32 as u64) << 32);
        let default_sandbox = matches!(default_zone, Zone::Sandbox);
        let info = (score_0_100 as u32)
            | ((default_sandbox as u32) << 8)
            | ((reasons_bits & 0xFFFF) << 16);

        self.journal_push(Event::new(
            self.now_ticks,
            cell,
            EventKind::MeritDecision,
            default_zone,
            None,
            packed_caps,
            info,
        ));
    }

    pub fn snapshot(&self) -> Result<WardenSnapshot<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>, MemError> {
        self.ensure_init()?;
        Ok(WardenSnapshot {
            now_ticks: self.now_ticks,
            alloc_normal: self.alloc_normal,
            alloc_sandbox: self.alloc_sandbox,
            caps: self.caps,
            cells: self.cells,
            policy: self.policy,
        })
    }

    pub fn restore(
        &mut self,
        snap: &WardenSnapshot<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>,
    ) -> Result<(), MemError> {
        // restore implies initialized
        self.initialized = true;
        self.now_ticks = snap.now_ticks;
        self.alloc_normal = snap.alloc_normal;
        self.alloc_sandbox = snap.alloc_sandbox;
        self.caps = snap.caps;
        self.cells = snap.cells;
        self.policy = snap.policy;
        self.journal_clear();
        Ok(())
    }

    /// Phase 6: Install or replace the policy VM program.
    ///
    /// The program is verified on execution time; compilation should already verify it.
    pub fn set_policy_program(&mut self, program: PolicyProgram<64>) -> Result<(), MemError> {
        self.ensure_init()?;
        self.policy = program;
        Ok(())
    }

    pub fn clear_policy_program(&mut self) -> Result<(), MemError> {
        self.ensure_init()?;
        self.policy.clear();
        Ok(())
    }

    /// Phase 5: transactional helper. If `f` returns an error, restore the previous snapshot.
    pub fn try_with_rollback<R, F>(&mut self, f: F) -> Result<R, MemError>
    where
        F: FnOnce(&mut Self) -> Result<R, MemError>,
    {
        self.ensure_init()?;
        let snap = self.snapshot()?;
        match f(self) {
            Ok(v) => Ok(v),
            Err(e) => {
                let _ = self.restore(&snap);
                Err(e)
            }
        }
    }

    /// Phase 5: crash a cell.
    ///
    /// Semantics (MVP):
    /// - Immediately expires all caps owned by the cell (expires_at = now_ticks).
    /// - Reclaims expired caps to free pages and release quota.
    /// - Journals a Quarantined event with info=1 (crash).
    ///
    /// Returns the number of capabilities reclaimed.
    pub fn crash_cell(&mut self, cell: CellId) -> Result<usize, MemError> {
        self.ensure_init()?;

        // We reserve `expires_at=0` to mean "never expires". To ensure an immediate
        // reclaim is possible even right after init (tick=0), we advance time to 1.
        if self.now_ticks == 0 {
            self.now_ticks = 1;
        }

        for cap in self.caps.iter_mut() {
            if cap.used && cap.owner == cell {
                cap.expires_at = self.now_ticks;
            }
        }

        self.journal_push(Event::new(
            self.now_ticks,
            cell,
            EventKind::Quarantined,
            Zone::Normal,
            None,
            0,
            1,
        ));

        self.reclaim_expired()
    }

    pub fn now_ticks(&self) -> u64 {
        self.now_ticks
    }

    pub fn page_size(&self) -> usize {
        PAGE_SIZE
    }

    pub fn total_pages(&self) -> Result<usize, MemError> {
        self.ensure_init()?;
        Ok(self.alloc_normal.total_pages() + self.alloc_sandbox.total_pages())
    }

    pub fn total_pages_in(&self, zone: Zone) -> Result<usize, MemError> {
        self.ensure_init()?;
        Ok(self.allocator(zone).total_pages())
    }

    pub fn set_quota(&mut self, cell: CellId, limit_bytes: u64) -> Result<(), MemError> {
        self.ensure_init()?;
        let idx = self.find_or_create_cell(cell)?;
        self.cells[idx].limit_bytes = limit_bytes;
        Ok(())
    }

    /// Configure a per-cell allocation rate limit.
    ///
    /// If enabled, each cell may allocate at most `limit_bytes` within any sliding window
    /// of `window_ticks` starting at `now_ticks` (window resets when `now_ticks - start >= window_ticks`).
    ///
    /// Disable by passing `window_ticks=0` or `limit_bytes=0`.
    pub fn set_rate_limit(
        &mut self,
        cell: CellId,
        window_ticks: u64,
        limit_bytes: u64,
    ) -> Result<(), MemError> {
        self.ensure_init()?;
        let idx = self.find_or_create_cell(cell)?;

        if window_ticks == 0 || limit_bytes == 0 {
            self.cells[idx].rate_window_ticks = 0;
            self.cells[idx].rate_limit_bytes = 0;
            self.cells[idx].rate_window_start = 0;
            self.cells[idx].rate_used_bytes = 0;
            return Ok(());
        }

        self.cells[idx].rate_window_ticks = window_ticks;
        self.cells[idx].rate_limit_bytes = limit_bytes;
        self.cells[idx].rate_window_start = self.now_ticks;
        self.cells[idx].rate_used_bytes = 0;
        Ok(())
    }

    /// Allocate memory according to intent. Returns a capability handle.
    pub fn allocate(&mut self, intent: MemIntent) -> Result<CapHandle, MemError> {
        self.ensure_init()?;
        if intent.bytes == 0 {
            self.journal_push(Event::new(
                self.now_ticks,
                intent.owner,
                EventKind::AllocateDenied,
                if intent.sandbox { Zone::Sandbox } else { Zone::Normal },
                None,
                intent.bytes,
                MemError::InvalidRequest as u32,
            ));
            return Err(MemError::InvalidRequest);
        }

        // Phase 6 policy VM: may deny or force sandbox.
        let mut intent = intent;
        if self.policy.is_enabled() {
            let out: PolicyOutcome = self
                .policy
                .eval(&intent)
                .map_err(|_| MemError::PolicyDenied)?;
            if out.force_sandbox {
                intent.sandbox = true;
            }
            if out.verdict == PolicyVerdict::Deny {
                self.journal_push(Event::new(
                    self.now_ticks,
                    intent.owner,
                    EventKind::AllocateDenied,
                    if intent.sandbox { Zone::Sandbox } else { Zone::Normal },
                    None,
                    intent.bytes,
                    MemError::PolicyDenied as u32,
                ));
                return Err(MemError::PolicyDenied);
            }
        }

        let cell_idx = self.find_or_create_cell(intent.owner)?;

        // Rate limiting: check before attempting to allocate physical pages.
        // We only *charge* the window after allocation succeeds.
        let mut should_charge_rate = false;
        if self.cells[cell_idx].rate_limit_bytes != 0 && self.cells[cell_idx].rate_window_ticks != 0 {
            let start = self.cells[cell_idx].rate_window_start;
            let window = self.cells[cell_idx].rate_window_ticks;
            if self.now_ticks.saturating_sub(start) >= window {
                self.cells[cell_idx].rate_window_start = self.now_ticks;
                self.cells[cell_idx].rate_used_bytes = 0;
            }

            let new_used = self.cells[cell_idx]
                .rate_used_bytes
                .saturating_add(intent.bytes);
            if new_used > self.cells[cell_idx].rate_limit_bytes {
                self.journal_push(Event::new(
                    self.now_ticks,
                    intent.owner,
                    EventKind::AllocateDenied,
                    if intent.sandbox { Zone::Sandbox } else { Zone::Normal },
                    None,
                    intent.bytes,
                    MemError::RateLimited as u32,
                ));
                return Err(MemError::RateLimited);
            }

            should_charge_rate = true;
        }

        if self.cells[cell_idx].limit_bytes != 0 {
            let new_used = self.cells[cell_idx]
                .used_bytes
                .saturating_add(intent.bytes);
            if new_used > self.cells[cell_idx].limit_bytes {
                self.journal_push(Event::new(
                    self.now_ticks,
                    intent.owner,
                    EventKind::AllocateDenied,
                    if intent.sandbox { Zone::Sandbox } else { Zone::Normal },
                    None,
                    intent.bytes,
                    MemError::QuotaExceeded as u32,
                ));
                return Err(MemError::QuotaExceeded);
            }
        }

        let pages = bytes_to_pages(intent.bytes);
        let zone = if intent.sandbox { Zone::Sandbox } else { Zone::Normal };
        let range = self
            .allocator_mut(zone)
            .alloc_contiguous(pages)
            .map_err(|e| {
                let err = map_alloc_err(e);
                self.journal_push(Event::new(
                    self.now_ticks,
                    intent.owner,
                    EventKind::AllocateDenied,
                    zone,
                    None,
                    intent.bytes,
                    err as u32,
                ));
                err
            })?;

        let cap_idx = self.find_free_cap()?;
        let generation = self.caps[cap_idx].generation;
        let expires_at = if intent.ttl_ticks == 0 {
            0
        } else {
            self.now_ticks.saturating_add(intent.ttl_ticks)
        };

        self.caps[cap_idx] = CapEntry {
            used: true,
            generation,
            owner: intent.owner,
            start_page: range.start_page,
            page_count: range.page_count,
            rights: intent.rights,
            zone,
            sandbox: intent.sandbox,
            label: intent.label,
            expires_at,
            bytes: intent.bytes,
            owns_pages: true,
        };

        self.cells[cell_idx].used = true;
        self.cells[cell_idx].cell = intent.owner;
        self.cells[cell_idx].used_bytes = self.cells[cell_idx]
            .used_bytes
            .saturating_add(intent.bytes);

        if should_charge_rate {
            self.cells[cell_idx].rate_used_bytes = self.cells[cell_idx]
                .rate_used_bytes
                .saturating_add(intent.bytes);
        }

        let handle = CapHandle::from_parts(cap_idx as u16, generation);
        self.journal_push(Event::new(
            self.now_ticks,
            intent.owner,
            EventKind::AllocateGranted,
            zone,
            Some(handle),
            intent.bytes,
            0,
        ));
        Ok(handle)
    }

    pub fn free(&mut self, handle: CapHandle) -> Result<(), MemError> {
        self.ensure_init()?;
        let cap_idx = handle.index();

        // Copy cap data first to avoid overlapping mutable borrows of `self`.
        let cap_copy = *self.get_cap(cap_idx, handle.generation())?;
        if cap_copy.owns_pages {
            let range = AllocRange {
                start_page: cap_copy.start_page,
                page_count: cap_copy.page_count,
            };

            // Quota accounting.
            if let Some(cell_idx) = self.find_cell(cap_copy.owner) {
                self.cells[cell_idx].used_bytes = self.cells[cell_idx]
                    .used_bytes
                    .saturating_sub(cap_copy.bytes);
            }

            self.allocator_mut(cap_copy.zone)
                .free_contiguous(range)
                .map_err(map_alloc_err)?;
        }

        // Finally, invalidate the capability slot.
        let slot = &mut self.caps[cap_idx];
        slot.used = false;
        slot.generation = slot.generation.wrapping_add(1).max(1);

        self.journal_push(Event::new(
            self.now_ticks,
            cap_copy.owner,
            EventKind::Freed,
            cap_copy.zone,
            Some(handle),
            cap_copy.bytes,
            if cap_copy.owns_pages { 1 } else { 0 },
        ));
        Ok(())
    }

    /// Validate a requested access to an address range against the capability.
    pub fn check_access(
        &self,
        handle: CapHandle,
        access: Access,
        addr: usize,
        len: usize,
    ) -> Result<(), MemError> {
        self.ensure_init()?;
        let cap_idx = handle.index();
        let cap = self.get_cap(cap_idx, handle.generation())?;

        if self.is_expired(cap) {
            self.journal_push(Event::new(
                self.now_ticks,
                cap.owner,
                EventKind::Expired,
                cap.zone,
                Some(handle),
                0,
                0,
            ));
            return Err(MemError::Expired);
        }

        let required = Rights::for_access(access);
        if !cap.rights.contains(required) {
            self.journal_push(Event::new(
                self.now_ticks,
                cap.owner,
                EventKind::AccessDenied,
                cap.zone,
                Some(handle),
                len as u64,
                1,
            ));
            return Err(MemError::AccessDenied);
        }

        let (start, end) = self.cap_phys_range(cap);
        let req_start = addr;
        let req_end = addr.checked_add(len).ok_or(MemError::InvalidRequest)?;
        if req_start < start || req_end > end {
            self.journal_push(Event::new(
                self.now_ticks,
                cap.owner,
                EventKind::AccessDenied,
                cap.zone,
                Some(handle),
                len as u64,
                2,
            ));
            return Err(MemError::AccessDenied);
        }

        Ok(())
    }

    /// Returns (start_addr, end_addr) for the capability range.
    pub fn cap_range(&self, handle: CapHandle) -> Result<(usize, usize), MemError> {
        self.ensure_init()?;
        let cap_idx = handle.index();
        let cap = self.get_cap(cap_idx, handle.generation())?;
        Ok(self.cap_phys_range(cap))
    }

    pub fn cap_meta(&self, handle: CapHandle) -> Result<CapMeta, MemError> {
        self.ensure_init()?;
        let cap_idx = handle.index();
        let cap = self.get_cap(cap_idx, handle.generation())?;
        Ok(CapMeta {
            owner: cap.owner,
            rights: cap.rights,
            sandbox: cap.sandbox,
            zone: cap.zone,
            label: cap.label,
            bytes: cap.bytes,
            expires_at: cap.expires_at,
        })
    }

    /// Quarantine a cell: expire all its capabilities (optionally caller can free later).
    pub fn quarantine_cell(&mut self, cell: CellId) -> Result<(), MemError> {
        self.ensure_init()?;
        for cap in self.caps.iter_mut() {
            if cap.used && cap.owner == cell {
                cap.expires_at = 1; // will be expired for any now_ticks >= 1
            }
        }

        self.journal_push(Event::new(
            self.now_ticks,
            cell,
            EventKind::Quarantined,
            Zone::Normal,
            None,
            0,
            0,
        ));
        Ok(())
    }

    /// Reclaim resources from all expired capabilities.
    /// Returns the number of capabilities reclaimed.
    pub fn reclaim_expired(&mut self) -> Result<usize, MemError> {
        self.ensure_init()?;
        let mut count = 0;

        for i in 0..MAX_CAPS {
            // 1. Check expiration (immutable borrow scope)
            let (should_reclaim, cap_copy) = {
                let c = &self.caps[i];
                if c.used && self.is_expired(c) {
                    (true, *c)
                } else {
                    (false, *c) 
                }
            };

            if should_reclaim {
                // 2. Perform cleanup (mutable borrow scope)
                
                // 2a. Free physical pages if owned
                if cap_copy.owns_pages {
                    let range = AllocRange {
                        start_page: cap_copy.start_page,
                        page_count: cap_copy.page_count,
                    };

                    // 2b. Update quota
                    // We inline find_cell logic to avoid borrowing conflict if we used helper
                    if let Some(cell_idx) = self.cells.iter().position(|c| c.used && c.cell == cap_copy.owner) {
                        self.cells[cell_idx].used_bytes = self.cells[cell_idx]
                            .used_bytes
                            .saturating_sub(cap_copy.bytes);
                    }

                    self.allocator_mut(cap_copy.zone)
                        .free_contiguous(range)
                        .map_err(map_alloc_err)?;
                }

                // 2c. Invalidate cap slot
                let slot = &mut self.caps[i];
                slot.used = false;
                slot.generation = slot.generation.wrapping_add(1).max(1);

                // 2d. Log reclamation
                self.journal_push(Event::new(
                    self.now_ticks,
                    cap_copy.owner,
                    EventKind::Reclaimed,
                    cap_copy.zone,
                    Some(CapHandle::from_parts(i as u16, cap_copy.generation)),
                    cap_copy.bytes,
                    if cap_copy.owns_pages { 1 } else { 0 },
                ));

                count += 1;
            }
        }
        Ok(count)
    }

    /// Delegate a derived capability from an existing one.
    ///
    /// - The derived capability does NOT own pages (`owns_pages=false`).
    /// - `rights` must be a subset of the parent rights.
    /// - TTL cannot extend beyond the parent expiration (if any).
    pub fn delegate(
        &mut self,
        parent: CapHandle,
        to_cell: CellId,
        rights: Rights,
        ttl_ticks: u64,
        label: u32,
    ) -> Result<CapHandle, MemError> {
        self.ensure_init()?;
        let parent_idx = parent.index();
        let parent_cap = *self.get_cap(parent_idx, parent.generation())?;

        if self.is_expired(&parent_cap) {
            return Err(MemError::Expired);
        }
        if !parent_cap.rights.contains(rights) {
            return Err(MemError::AccessDenied);
        }

        let expires_at = if ttl_ticks == 0 {
            parent_cap.expires_at
        } else {
            let requested = self.now_ticks.saturating_add(ttl_ticks);
            if parent_cap.expires_at == 0 {
                requested
            } else {
                core::cmp::min(requested, parent_cap.expires_at)
            }
        };

        // Ensure target cell exists for observability/quarantine hooks.
        let _ = self.find_or_create_cell(to_cell)?;

        let cap_idx = self.find_free_cap()?;
        let generation = self.caps[cap_idx].generation;
        self.caps[cap_idx] = CapEntry {
            used: true,
            generation,
            owner: to_cell,
            start_page: parent_cap.start_page,
            page_count: parent_cap.page_count,
            rights,
            zone: parent_cap.zone,
            sandbox: parent_cap.sandbox,
            label,
            expires_at,
            bytes: 0,
            owns_pages: false,
        };

        let handle = CapHandle::from_parts(cap_idx as u16, generation);
        self.journal_push(Event::new(
            self.now_ticks,
            to_cell,
            EventKind::Delegated,
            parent_cap.zone,
            Some(handle),
            0,
            parent.raw(),
        ));
        Ok(handle)
    }

    fn ensure_init(&self) -> Result<(), MemError> {
        if !self.initialized {
            Err(MemError::NotInitialized)
        } else {
            Ok(())
        }
    }

    fn find_free_cap(&self) -> Result<usize, MemError> {
        for (i, c) in self.caps.iter().enumerate() {
            if !c.used {
                return Ok(i);
            }
        }
        Err(MemError::OutOfMemory)
    }

    fn get_cap(&self, idx: usize, gen: u16) -> Result<&CapEntry, MemError> {
        if idx >= MAX_CAPS {
            return Err(MemError::InvalidHandle);
        }
        let cap = &self.caps[idx];
        if !cap.used || cap.generation != gen {
            return Err(MemError::InvalidHandle);
        }
        Ok(cap)
    }

    fn is_expired(&self, cap: &CapEntry) -> bool {
        cap.expires_at != 0 && self.now_ticks >= cap.expires_at
    }

    fn cap_phys_range(&self, cap: &CapEntry) -> (usize, usize) {
        let start = self.allocator(cap.zone).page_addr(cap.start_page);
        let bytes = (cap.page_count as usize) * PAGE_SIZE;
        (start, start + bytes)
    }

    fn allocator(&self, zone: Zone) -> &BitmapPageAllocator<BITMAP_WORDS> {
        match zone {
            Zone::Normal => &self.alloc_normal,
            Zone::Sandbox => &self.alloc_sandbox,
        }
    }

    fn allocator_mut(&mut self, zone: Zone) -> &mut BitmapPageAllocator<BITMAP_WORDS> {
        match zone {
            Zone::Normal => &mut self.alloc_normal,
            Zone::Sandbox => &mut self.alloc_sandbox,
        }
    }

    fn find_cell(&self, cell: CellId) -> Option<usize> {
        self.cells
            .iter()
            .position(|c| c.used && c.cell == cell)
    }

    fn find_or_create_cell(&mut self, cell: CellId) -> Result<usize, MemError> {
        if let Some(i) = self.find_cell(cell) {
            return Ok(i);
        }
        for (i, c) in self.cells.iter_mut().enumerate() {
            if !c.used {
                c.used = true;
                c.cell = cell;
                c.limit_bytes = 0;
                c.used_bytes = 0;
                c.rate_limit_bytes = 0;
                c.rate_window_ticks = 0;
                c.rate_window_start = 0;
                c.rate_used_bytes = 0;
                return Ok(i);
            }
        }
        Err(MemError::OutOfMemory)
    }
}

fn bytes_to_pages(bytes: u64) -> u32 {
    let bytes = bytes as usize;
    let pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    pages as u32
}

fn map_alloc_err(err: AllocError) -> MemError {
    match err {
        AllocError::OutOfMemory => MemError::OutOfMemory,
        AllocError::InvalidRequest => MemError::InvalidRequest,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::policy_vm::compile_rpn;

    const BITMAP_WORDS: usize = 16; // 16 * 64 pages = 1024 pages managed
    const MAX_CAPS: usize = 64;
    const MAX_CELLS: usize = 8;

    #[test]
    fn alloc_and_access_check() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0x1000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };
        w.set_quota(1, 64 * 1024).unwrap();

        let mut intent = MemIntent::new(1, 8192);
        intent.rights = Rights::R.union(Rights::W);
        let cap = w.allocate(intent).unwrap();

        let (start, end) = w.cap_range(cap).unwrap();
        assert!(end > start);
        w.check_access(cap, Access::Read, start, 16).unwrap();
        w.check_access(cap, Access::Write, start + 32, 16).unwrap();
        assert_eq!(w.check_access(cap, Access::Execute, start, 4), Err(MemError::AccessDenied));

        assert_eq!(w.check_access(cap, Access::Read, end - 8, 16), Err(MemError::AccessDenied));

        w.free(cap).unwrap();
        assert_eq!(w.cap_range(cap), Err(MemError::InvalidHandle));

        // Journal should have at least grant + free.
        let stats = w.journal_stats();
        assert!(stats.len >= 2);
    }

    #[test]
    fn quota_enforced() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0x2000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };
        w.set_quota(7, 4096).unwrap();

        let cap = w.allocate(MemIntent::new(7, 4096)).unwrap();
        assert_eq!(w.allocate(MemIntent::new(7, 4096)), Err(MemError::QuotaExceeded));
        w.free(cap).unwrap();
        let _cap2 = w.allocate(MemIntent::new(7, 4096)).unwrap();
    }

    #[test]
    fn ttl_expires() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0x3000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };

        let mut intent = MemIntent::new(2, 4096);
        intent.ttl_ticks = 10;
        let cap = w.allocate(intent).unwrap();

        let (start, _) = w.cap_range(cap).unwrap();
        w.check_access(cap, Access::Read, start, 1).unwrap();

        w.tick(10);
        assert_eq!(w.check_access(cap, Access::Read, start, 1), Err(MemError::Expired));
    }

    #[test]
    fn zones_allocate_to_distinct_ranges() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe {
            w.init_zones(
                0x4000_0000,
                (BITMAP_WORDS * 64) * PAGE_SIZE,
                0x5000_0000,
                (BITMAP_WORDS * 64) * PAGE_SIZE,
            )
        };

        let cap_normal = w.allocate(MemIntent::new(1, 4096)).unwrap();
        let mut sandbox_intent = MemIntent::new(2, 4096);
        sandbox_intent.sandbox = true;
        let cap_sandbox = w.allocate(sandbox_intent).unwrap();

        let (n_start, _) = w.cap_range(cap_normal).unwrap();
        let (s_start, _) = w.cap_range(cap_sandbox).unwrap();
        assert!(n_start >= 0x4000_0000 && n_start < 0x5000_0000);
        assert!(s_start >= 0x5000_0000);

        let meta_n = w.cap_meta(cap_normal).unwrap();
        let meta_s = w.cap_meta(cap_sandbox).unwrap();
        assert_eq!(meta_n.zone, Zone::Normal);
        assert_eq!(meta_s.zone, Zone::Sandbox);
    }

    #[test]
    fn delegation_does_not_free_pages() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0x6000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };

        let mut intent = MemIntent::new(10, 8192);
        intent.rights = Rights::R.union(Rights::W);
        let parent = w.allocate(intent).unwrap();
        let (start, _) = w.cap_range(parent).unwrap();

        // delegate read-only to another cell
        let child = w
            .delegate(parent, 11, Rights::R, 0, 123)
            .unwrap();
        w.check_access(child, Access::Read, start, 4).unwrap();
        assert_eq!(w.check_access(child, Access::Write, start, 4), Err(MemError::AccessDenied));

        // freeing derived cap should not free the underlying memory
        w.free(child).unwrap();
        w.check_access(parent, Access::Write, start, 4).unwrap();

        // freeing parent should free
        w.free(parent).unwrap();
        assert_eq!(w.check_access(parent, Access::Read, start, 1), Err(MemError::InvalidHandle));
    }

    #[test]
    fn snapshot_restore_roundtrip() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0x7000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };
        w.set_quota(1, 8192).unwrap();

        let cap = w.allocate(MemIntent::new(1, 4096)).unwrap();
        w.tick(42);

        let snap = w.snapshot().unwrap();

        // mutate state
        w.free(cap).unwrap();
        w.set_quota(1, 0).unwrap();
        w.tick(100);

        // restore
        w.restore(&snap).unwrap();
        assert_eq!(w.now_ticks(), 42);
        let (start, _) = w.cap_range(cap).unwrap();
        w.check_access(cap, Access::Read, start, 1).unwrap();
    }

    #[test]
    fn rate_limit_enforced_per_window() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0x8000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };

        // 4KiB per 10 ticks
        w.set_rate_limit(1, 10, 4096).unwrap();

        let cap1 = w.allocate(MemIntent::new(1, 4096)).unwrap();
        w.free(cap1).unwrap();

        // Still within the same window: should be denied even though quota isn't exceeded.
        assert_eq!(w.allocate(MemIntent::new(1, 4096)), Err(MemError::RateLimited));

        // After window reset, allocation should succeed.
        w.tick(10);
        let cap2 = w.allocate(MemIntent::new(1, 4096)).unwrap();
        w.free(cap2).unwrap();
    }

    #[test]
    fn rate_limit_is_per_cell() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0x9000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };

        w.set_rate_limit(1, 10, 4096).unwrap();

        let cap1 = w.allocate(MemIntent::new(1, 4096)).unwrap();
        w.free(cap1).unwrap();

        // Cell 2 has no rate limit configured; should still be able to allocate.
        let cap2 = w.allocate(MemIntent::new(2, 4096)).unwrap();
        w.free(cap2).unwrap();
    }

    #[test]
    fn policy_denies_allocation() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0xA000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };
        w.set_quota(1, 1024 * 1024).unwrap();

        let mut p: PolicyProgram<64> = PolicyProgram::new_disabled();
        compile_rpn("bytes 4096 > deny_if_true allow", &mut p).unwrap();
        w.set_policy_program(p).unwrap();

        assert_eq!(w.allocate(MemIntent::new(1, 8192)), Err(MemError::PolicyDenied));
        let cap = w.allocate(MemIntent::new(1, 4096)).unwrap();
        w.free(cap).unwrap();
    }

    #[test]
    fn policy_can_force_sandbox() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe {
            w.init_zones(
                0xB000_0000,
                (BITMAP_WORDS * 64) * PAGE_SIZE,
                0xC000_0000,
                (BITMAP_WORDS * 64) * PAGE_SIZE,
            )
        };
        w.set_quota(2, 1024 * 1024).unwrap();

        let mut p: PolicyProgram<64> = PolicyProgram::new_disabled();
        compile_rpn("bytes 0 > sandbox_if_true allow", &mut p).unwrap();
        w.set_policy_program(p).unwrap();

        let cap = w.allocate(MemIntent::new(2, 4096)).unwrap();
        let meta = w.cap_meta(cap).unwrap();
        assert_eq!(meta.zone, Zone::Sandbox);
        w.free(cap).unwrap();
    }

    #[test]
    fn crash_cell_expires_and_reclaims() {
        let mut w = MemoryWarden::<BITMAP_WORDS, MAX_CAPS, MAX_CELLS>::new();
        unsafe { w.init(0xD000_0000, (BITMAP_WORDS * 64) * PAGE_SIZE) };
        w.set_quota(3, 1024 * 1024).unwrap();

        let cap = w.allocate(MemIntent::new(3, 4096)).unwrap();
        assert!(w.cap_range(cap).is_ok());

        let reclaimed = w.crash_cell(3).unwrap();
        assert_eq!(reclaimed, 1);
        assert_eq!(w.cap_range(cap), Err(MemError::InvalidHandle));

        // Journal should contain quarantine + reclaimed events.
        let mut saw_quarantine = false;
        let mut saw_reclaimed = false;
        for i in 0..w.journal_stats().len {
            let ev = w.journal_get(i).unwrap();
            if ev.kind == EventKind::Quarantined {
                saw_quarantine = true;
                assert_eq!(ev.cell, 3);
                assert_eq!(ev.info, 1);
            }
            if ev.kind == EventKind::Reclaimed {
                saw_reclaimed = true;
                assert_eq!(ev.cell, 3);
            }
        }
        assert!(saw_quarantine);
        assert!(saw_reclaimed);
    }
}
