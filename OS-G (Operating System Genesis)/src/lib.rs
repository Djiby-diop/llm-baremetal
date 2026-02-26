#![cfg_attr(not(any(test, feature = "std")), no_std)]

pub mod alloc;
pub mod cortex;
pub mod dplus;
pub mod journal;
pub mod policy_vm;
pub mod resonance;
pub mod sentinel;
pub mod soma;
pub mod types;
pub mod warden;

pub use types::{Access, CapHandle, CellId, MemIntent, Rights, Zone};
pub use warden::{CapMeta, MemError, MemoryWarden, WardenSnapshot};

pub use journal::{Event, EventKind, Journal, JournalStats};

pub use soma::NeuralSoma;

pub use resonance::{ResonanceConfig, ResonanceProfile, ResonanceVerdict};

pub use cortex::{CortexConfig, HeuristicCortex};
pub use policy_vm::{compile_rpn as compile_policy_rpn, CompileError as PolicyCompileError, PolicyOutcome, PolicyProgram, PolicyVerdict};

pub use dplus::{
	apply_caps,
	compute_merit_profile,
	extract_mem_allocate_rules,
	format_reasons_csv,
	parse as dplus_parse,
	ConsensusMode,
	DPlusModule,
	DPlusSection,
	LawMemAllocate,
	MeritProfile,
	MeritReasons,
	ParseError,
	SectionKind,
	SectionTag,
	VerifyError,
	VerifyOptions,
};
