/// Phase 8 --- Resonance Profile
///
/// Statistical behavioral fingerprint that learns the normal inter-event
/// interval rhythm and scores each new sample via Mean Absolute Deviation (MAD).
///
/// Scoring: score = clamp(|sample - mean| * 50 / max(MAD,1), 0, 100)
/// score >= threshold  =>  ForeignRhythm.  No heap; const-generic `N`.

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct ResonanceConfig {
    /// Threshold (0-100). Scores at or above this trigger ForeignRhythm.
    pub threshold: u8,
}
impl Default for ResonanceConfig {
    fn default() -> Self { Self { threshold: 40 } }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum ResonanceVerdict {
    Normal,
    ForeignRhythm { score: u8 },
}

pub struct ResonanceProfile<const N: usize> {
    cfg: ResonanceConfig,
    buf: [u64; N],
    head: usize,
    count: usize,
    last_tsc: u64,
    has_last: bool,
}

impl<const N: usize> ResonanceProfile<N> {
    pub const fn new() -> Self {
        Self {
            cfg: ResonanceConfig { threshold: 40 },
            buf: [0u64; N],
            head: 0,
            count: 0,
            last_tsc: 0,
            has_last: false,
        }
    }
    pub fn set_config(&mut self, cfg: ResonanceConfig) { self.cfg = cfg; }
    pub fn config(&self) -> ResonanceConfig { self.cfg }

    pub fn reset(&mut self) {
        for v in self.buf.iter_mut() { *v = 0; }
        self.head = 0; self.count = 0; self.last_tsc = 0; self.has_last = false;
    }

    /// Record a new timestamp. Returns None until 2 intervals collected.
    pub fn record(&mut self, tsc: u64) -> Option<ResonanceVerdict> {
        if !self.has_last { self.last_tsc = tsc; self.has_last = true; return None; }
        let interval = tsc.saturating_sub(self.last_tsc);
        self.last_tsc = tsc;
        self.buf[self.head % N] = interval;
        self.head = self.head.wrapping_add(1);
        if self.count < N { self.count += 1; }
        if self.count < 2 { return None; }
        let score = self.deviation_score(interval);
        Some(if score >= self.cfg.threshold {
            ResonanceVerdict::ForeignRhythm { score }
        } else {
            ResonanceVerdict::Normal
        })
    }

    /// Score interval 0-100 without updating buffer.
    pub fn deviation_score(&self, interval: u64) -> u8 {
        let active = self.count.min(N);
        if active == 0 { return 0; }
        let mut sum = 0u64;
        for v in &self.buf[..active] { sum = sum.saturating_add(*v); }
        let mean = sum / (active as u64);
        let mut mad_sum = 0u64;
        for v in &self.buf[..active] {
            let d = if *v >= mean { *v - mean } else { mean - *v };
            mad_sum = mad_sum.saturating_add(d);
        }
        let mad = mad_sum / (active as u64);
        let diff = if interval >= mean { interval - mean } else { mean - interval };
        if mad == 0 { return if diff == 0 { 0 } else { 100 }; }
        (diff.saturating_mul(50) / mad).min(100) as u8
    }

    pub fn sample_count(&self) -> usize { self.count }

    pub fn mean_interval(&self) -> u64 {
        let active = self.count.min(N);
        if active == 0 { return 0; }
        let mut sum = 0u64;
        for v in &self.buf[..active] { sum = sum.saturating_add(*v); }
        sum / (active as u64)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_verdict_until_two_intervals() {
        let mut p: ResonanceProfile<8> = ResonanceProfile::new();
        assert!(p.record(0).is_none());
        assert!(p.record(1000).is_none());
        assert!(p.record(2000).is_some());
    }

    #[test]
    fn stable_rhythm_scores_zero() {
        let mut p: ResonanceProfile<8> = ResonanceProfile::new();
        let _ = p.record(0);
        for i in 1u64..=8 { let _ = p.record(i * 1000); }
        assert_eq!(p.deviation_score(1000), 0);
    }

    #[test]
    fn foreign_rhythm_scores_max() {
        let mut p: ResonanceProfile<8> = ResonanceProfile::new();
        let _ = p.record(0);
        for i in 1u64..=8 { let _ = p.record(i * 1000); }
        assert_eq!(p.deviation_score(100_000), 100);
    }

    #[test]
    fn foreign_rhythm_verdict() {
        let mut p: ResonanceProfile<8> = ResonanceProfile::new();
        p.set_config(ResonanceConfig { threshold: 40 });
        let _ = p.record(0);
        for i in 1u64..=8 { let _ = p.record(i * 1000); }
        let v = p.record(8000 + 500_000);
        assert!(matches!(v, Some(ResonanceVerdict::ForeignRhythm { .. })), "got {:?}", v);
    }

    #[test]
    fn minor_jitter_scores_low() {
        let mut p: ResonanceProfile<8> = ResonanceProfile::new();
        p.set_config(ResonanceConfig { threshold: 40 });
        let _ = p.record(0);
        let offsets: [u64; 8] = [900, 950, 1000, 1050, 1100, 980, 1020, 1010];
        let mut t = 0u64;
        for &dt in &offsets { t += dt; let _ = p.record(t); }
        let score = p.deviation_score(1000);
        assert!(score < 40, "expected low score, got {score}");
    }

    #[test]
    fn reset_empties_profile() {
        let mut p: ResonanceProfile<8> = ResonanceProfile::new();
        let _ = p.record(0);
        for i in 1u64..=8 { let _ = p.record(i * 1000); }
        p.reset();
        assert_eq!(p.sample_count(), 0);
        assert_eq!(p.mean_interval(), 0);
        assert!(p.record(999).is_none());
    }
}
