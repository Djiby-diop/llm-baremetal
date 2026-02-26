#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct CapHandle(u32);

impl CapHandle {
    pub const fn from_parts(index: u16, generation: u16) -> Self {
        Self(((generation as u32) << 16) | (index as u32))
    }

    pub const fn index(self) -> usize {
        (self.0 as u16) as usize
    }

    pub const fn generation(self) -> u16 {
        (self.0 >> 16) as u16
    }

    pub const fn raw(self) -> u32 {
        self.0
    }
}

pub type CellId = u32;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Zone {
    Normal,
    Sandbox,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Access {
    Read,
    Write,
    Execute,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct Rights(u8);

impl Rights {
    pub const NONE: Rights = Rights(0);
    pub const R: Rights = Rights(1 << 0);
    pub const W: Rights = Rights(1 << 1);
    pub const X: Rights = Rights(1 << 2);

    pub const fn contains(self, other: Rights) -> bool {
        (self.0 & other.0) == other.0
    }

    pub const fn union(self, other: Rights) -> Rights {
        Rights(self.0 | other.0)
    }

    pub const fn for_access(access: Access) -> Rights {
        match access {
            Access::Read => Rights::R,
            Access::Write => Rights::W,
            Access::Execute => Rights::X,
        }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct MemIntent {
    pub owner: CellId,
    pub bytes: u64,
    pub rights: Rights,
    pub ttl_ticks: u64,
    pub priority: u8,
    /// If true, allocation is done from the sandbox zone (SandRAM).
    pub sandbox: bool,
    pub label: u32,
}

impl MemIntent {
    pub const fn new(owner: CellId, bytes: u64) -> Self {
        Self {
            owner,
            bytes,
            rights: Rights::R.union(Rights::W),
            ttl_ticks: 0,
            priority: 0,
            sandbox: false,
            label: 0,
        }
    }
}
