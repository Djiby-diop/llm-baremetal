pub mod module;
pub mod merit;
pub mod ops;
pub mod judge;
pub mod verifier;

pub use module::{parse, DPlusModule, DPlusSection, ParseError, SectionKind, SectionTag};
pub use merit::{apply_caps, compute_merit_profile, format_reasons_csv, MeritProfile, MeritReasons};
pub use judge::{extract_mem_allocate_rules, extract_sentinel_rules, LawMemAllocate, LawSentinelRule};
pub use ops::{
	extract_cortex_heur_config,
	extract_soma_io_config,
	extract_warden_mem_config,
	extract_warden_policy_rpn,
	extract_warden_policy_rpn_line,
	for_each_op,
	SomaIoConfig,
	WardenMemConfig,
};
pub use verifier::{ConsensusMode, VerifyError, VerifyOptions};

#[cfg(test)]
mod tests;
