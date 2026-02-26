use core::cmp;

pub const PAGE_SIZE: usize = 4096;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct AllocRange {
    pub start_page: u32,
    pub page_count: u32,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum AllocError {
    OutOfMemory,
    InvalidRequest,
}

#[derive(Copy, Clone)]
/// A simple contiguous-page allocator backed by a bitmap.
///
/// This is designed to compile on stable Rust in `no_std` without requiring
/// `generic_const_exprs`. To keep metadata bounded, we cap the managed memory
/// using a bitmap of fixed word count.
///
/// - `WORDS` is the number of `u64` words in the bitmap.
/// - Maximum pages managed is `WORDS * 64`.
/// - During `init`, any extra pages beyond that cap are ignored.
pub struct BitmapPageAllocator<const WORDS: usize> {
    base_addr: usize,
    total_pages: usize,
    bits: [u64; WORDS],
}

impl<const WORDS: usize> BitmapPageAllocator<WORDS> {
    pub const fn new() -> Self {
        Self {
            base_addr: 0,
            total_pages: 0,
            bits: [0; WORDS],
        }
    }

    /// # Safety
    /// Caller ensures `[base_addr, base_addr + bytes)` is usable RAM.
    pub unsafe fn init(&mut self, base_addr: usize, bytes: usize) {
        self.base_addr = align_up(base_addr, PAGE_SIZE);
        let end = base_addr.saturating_add(bytes);
        let aligned_end = align_down(end, PAGE_SIZE);

        if aligned_end <= self.base_addr {
            self.total_pages = 0;
        } else {
            let bytes_aligned = aligned_end - self.base_addr;
            self.total_pages = cmp::min(bytes_aligned / PAGE_SIZE, WORDS * 64);
        }

        for b in self.bits.iter_mut() {
            *b = 0;
        }
    }

    pub fn total_pages(&self) -> usize {
        self.total_pages
    }

    pub fn base_addr(&self) -> usize {
        self.base_addr
    }

    pub fn bits(&self) -> &[u64; WORDS] {
        &self.bits
    }

    pub fn bits_mut(&mut self) -> &mut [u64; WORDS] {
        &mut self.bits
    }

    pub fn page_addr(&self, page_index: u32) -> usize {
        self.base_addr + (page_index as usize) * PAGE_SIZE
    }

    pub fn alloc_contiguous(&mut self, pages: u32) -> Result<AllocRange, AllocError> {
        if pages == 0 {
            return Err(AllocError::InvalidRequest);
        }
        let pages = pages as usize;
        if pages > self.total_pages {
            return Err(AllocError::OutOfMemory);
        }

        let mut run_start: usize = 0;
        let mut run_len: usize = 0;

        for i in 0..self.total_pages {
            if !self.is_allocated(i) {
                if run_len == 0 {
                    run_start = i;
                }
                run_len += 1;
                if run_len == pages {
                    for p in run_start..(run_start + pages) {
                        self.set_allocated(p, true);
                    }
                    return Ok(AllocRange {
                        start_page: run_start as u32,
                        page_count: pages as u32,
                    });
                }
            } else {
                run_len = 0;
            }
        }

        Err(AllocError::OutOfMemory)
    }

    pub fn free_contiguous(&mut self, range: AllocRange) -> Result<(), AllocError> {
        let start = range.start_page as usize;
        let count = range.page_count as usize;
        if count == 0 {
            return Err(AllocError::InvalidRequest);
        }
        if start.checked_add(count).map(|end| end <= self.total_pages) != Some(true) {
            return Err(AllocError::InvalidRequest);
        }

        for p in start..(start + count) {
            self.set_allocated(p, false);
        }
        Ok(())
    }

    fn is_allocated(&self, page: usize) -> bool {
        let word = page / 64;
        let bit = page % 64;
        (self.bits[word] & (1u64 << bit)) != 0
    }

    fn set_allocated(&mut self, page: usize, allocated: bool) {
        let word = page / 64;
        let bit = page % 64;
        let mask = 1u64 << bit;
        if allocated {
            self.bits[word] |= mask;
        } else {
            self.bits[word] &= !mask;
        }
    }
}

const fn align_up(value: usize, align: usize) -> usize {
    (value + align - 1) & !(align - 1)
}

const fn align_down(value: usize, align: usize) -> usize {
    value & !(align - 1)
}
