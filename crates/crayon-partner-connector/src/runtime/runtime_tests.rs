//! HB-014 coverage: quota window, minimum spacing, circuit breaker state
//! transitions, retry budget rules (mutations never retry) and
//! cooperative cancellation.

use super::{
    CallLimiter, CancellationFlag, CircuitBreaker, CircuitState, RetryBudget, RuntimeRejection,
};

#[test]
fn quota_window_admits_then_sheds_then_recovers() {
    let mut limiter = CallLimiter::new(1_000, 3, 0);
    assert!(limiter.admit(100).is_ok());
    assert!(limiter.admit(200).is_ok());
    assert!(limiter.admit(300).is_ok());
    assert_eq!(limiter.admit(400), Err(RuntimeRejection::QuotaExhausted));
    // Window slides: the oldest call ages out at 100+1000.
    assert!(limiter.admit(1_101).is_ok());
}

#[test]
fn minimum_spacing_is_enforced() {
    let mut limiter = CallLimiter::new(10_000, 10, 50);
    assert!(limiter.admit(1_000).is_ok());
    assert_eq!(limiter.admit(1_020), Err(RuntimeRejection::TooSoon));
    assert!(limiter.admit(1_050).is_ok());
}

#[test]
fn circuit_breaker_full_transition_cycle() {
    let mut breaker = CircuitBreaker::new(3, 1_000);
    assert_eq!(breaker.state(), CircuitState::Closed);
    // Failures below threshold keep the circuit closed.
    breaker.on_failure(100);
    breaker.on_failure(200);
    assert_eq!(breaker.state(), CircuitState::Closed);
    assert!(breaker.admit(300).is_ok());
    // Threshold crossed: open, everything refused.
    breaker.on_failure(300);
    assert_eq!(breaker.state(), CircuitState::Open);
    assert_eq!(breaker.admit(400), Err(RuntimeRejection::CircuitOpen));
    assert_eq!(breaker.state(), CircuitState::Open);
    // Cooldown elapsed (300+1000): half-open admits exactly one probe.
    assert!(breaker.admit(1_300).is_ok());
    assert_eq!(breaker.state(), CircuitState::HalfOpen);
    assert_eq!(breaker.admit(1_101), Err(RuntimeRejection::CircuitOpen));
    // Probe success closes the circuit.
    breaker.on_success();
    assert_eq!(breaker.state(), CircuitState::Closed);
    assert!(breaker.admit(1_200).is_ok());
}

#[test]
fn half_open_probe_failure_reopens() {
    let mut breaker = CircuitBreaker::new(1, 500);
    breaker.on_failure(100);
    assert_eq!(breaker.state(), CircuitState::Open);
    assert!(breaker.admit(700).is_ok()); // probe
    breaker.on_failure(700);
    assert_eq!(breaker.state(), CircuitState::Open);
    assert_eq!(breaker.admit(800), Err(RuntimeRejection::CircuitOpen));
}

#[test]
fn mutations_never_retry_and_reads_respect_budget() {
    let mut budget = RetryBudget::new(2);
    assert!(!budget.can_retry(false)); // side-effectful: never
    budget.consume(); // consuming must not enable mutation retries either
    assert!(!budget.can_retry(false));

    budget.refill();
    assert!(budget.can_retry(true));
    budget.consume();
    assert!(budget.can_retry(true));
    budget.consume();
    assert!(!budget.can_retry(true)); // budget exhausted
    budget.refill();
    assert!(budget.can_retry(true));
    assert_eq!(budget.remaining(), 2);
}

#[test]
fn cancellation_flag_is_cooperative() {
    let flag = CancellationFlag::new();
    assert!(!flag.is_cancelled());
    flag.cancel();
    assert!(flag.is_cancelled());
    // Clone shares state with the original.
    let clone = flag.clone();
    assert!(clone.is_cancelled());
}
