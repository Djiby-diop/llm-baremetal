//! Phase 3: Smart policy runtime state machine.

use super::CompileError;
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum RuntimeState {
    Idle,
    Normal,
    Throttled,
    Cooled,
    Safe,
    Recovery,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum RuntimeEventKind {
    CpuSpike,
    MemoryPressure,
    DivergenceDetected,
    HealthDrop,
    LatencyReduced,
    MetricsGreen,
}

#[derive(Debug, Clone)]
pub struct RuntimeEvent {
    pub kind: RuntimeEventKind,
    pub value: f32,
    pub source: String,
}

#[derive(Debug, Clone)]
pub struct Transition {
    pub from: RuntimeState,
    pub on: RuntimeEventKind,
    pub to: RuntimeState,
}

#[derive(Debug, Clone)]
pub struct StateMachine {
    current: RuntimeState,
    transitions: HashMap<(RuntimeState, RuntimeEventKind), RuntimeState>,
}

impl StateMachine {
    pub fn new(initial: RuntimeState) -> Self {
        let mut sm = Self {
            current: initial,
            transitions: HashMap::new(),
        };
        sm.install_default_transitions();
        sm
    }

    fn install_default_transitions(&mut self) {
        self.add_transition(RuntimeState::Idle, RuntimeEventKind::MetricsGreen, RuntimeState::Normal);
        self.add_transition(RuntimeState::Normal, RuntimeEventKind::CpuSpike, RuntimeState::Throttled);
        self.add_transition(RuntimeState::Normal, RuntimeEventKind::MemoryPressure, RuntimeState::Safe);
        self.add_transition(RuntimeState::Normal, RuntimeEventKind::DivergenceDetected, RuntimeState::Safe);
        self.add_transition(RuntimeState::Throttled, RuntimeEventKind::LatencyReduced, RuntimeState::Cooled);
        self.add_transition(RuntimeState::Cooled, RuntimeEventKind::MetricsGreen, RuntimeState::Normal);
        self.add_transition(RuntimeState::Safe, RuntimeEventKind::HealthDrop, RuntimeState::Recovery);
        self.add_transition(RuntimeState::Recovery, RuntimeEventKind::MetricsGreen, RuntimeState::Safe);
    }

    pub fn add_transition(&mut self, from: RuntimeState, on: RuntimeEventKind, to: RuntimeState) {
        self.transitions.insert((from, on), to);
    }

    pub fn current(&self) -> RuntimeState {
        self.current
    }

    pub fn apply_event(&mut self, event: &RuntimeEvent) -> bool {
        if let Some(next) = self.transitions.get(&(self.current, event.kind)).copied() {
            self.current = next;
            return true;
        }
        false
    }

    pub fn reset(&mut self, state: RuntimeState) {
        self.current = state;
    }

    pub fn ensure_viable(&self) -> Result<(), CompileError> {
        if self.current == RuntimeState::Recovery {
            return Ok(());
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_transitions_progress() {
        let mut sm = StateMachine::new(RuntimeState::Idle);

        let moved = sm.apply_event(&RuntimeEvent {
            kind: RuntimeEventKind::MetricsGreen,
            value: 1.0,
            source: "boot".into(),
        });
        assert!(moved);
        assert_eq!(sm.current(), RuntimeState::Normal);

        let moved = sm.apply_event(&RuntimeEvent {
            kind: RuntimeEventKind::CpuSpike,
            value: 0.91,
            source: "cpu".into(),
        });
        assert!(moved);
        assert_eq!(sm.current(), RuntimeState::Throttled);
    }

    #[test]
    fn unknown_transition_is_noop() {
        let mut sm = StateMachine::new(RuntimeState::Idle);
        let moved = sm.apply_event(&RuntimeEvent {
            kind: RuntimeEventKind::CpuSpike,
            value: 0.99,
            source: "cpu".into(),
        });
        assert!(!moved);
        assert_eq!(sm.current(), RuntimeState::Idle);
    }
}
