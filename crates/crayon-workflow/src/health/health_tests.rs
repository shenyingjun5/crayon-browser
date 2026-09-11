//! WFL-13 health: failure window threshold, sliding window, success
//! reset and disable-acknowledgement reset.

use super::{SkillHealth, FAILURE_WINDOW_MS, FAILURE_WINDOW_THRESHOLD};

#[test]
fn threshold_crossing_disables() {
    let mut health = SkillHealth::with_defaults();
    assert_eq!(
        health.record_failure("alpha", 1_000),
        super::HealthVerdict::Healthy
    );
    assert_eq!(
        health.record_failure("alpha", 2_000),
        super::HealthVerdict::Healthy
    );
    assert_eq!(
        health.record_failure("alpha", 3_000),
        super::HealthVerdict::ShouldDisable
    );
}

#[test]
fn success_resets_streak() {
    let mut health = SkillHealth::with_defaults();
    health.record_failure("alpha", 1_000);
    health.record_failure("alpha", 2_000);
    health.record_success("alpha");
    assert_eq!(health.recent_failures("alpha", 3_000), 0);
    // Two fresh failures are not enough again.
    health.record_failure("alpha", 4_000);
    assert_eq!(
        health.record_failure("alpha", 5_000),
        super::HealthVerdict::Healthy
    );
    assert_eq!(
        health.record_failure("alpha", 6_000),
        super::HealthVerdict::ShouldDisable
    );
}

#[test]
fn window_slides_failures_age_out() {
    let mut health = SkillHealth::with_defaults();
    health.record_failure("alpha", 1_000);
    health.record_failure("alpha", 1_100);
    // Past the window, old failures no longer count.
    let much_later = FAILURE_WINDOW_MS + 2_000; // both failures aged out
    assert_eq!(health.recent_failures("alpha", much_later), 0);
    assert_eq!(
        health.record_failure("alpha", much_later),
        super::HealthVerdict::Healthy
    );
    let _ = FAILURE_WINDOW_THRESHOLD;
}

#[test]
fn skills_are_tracked_independently() {
    let mut health = SkillHealth::with_defaults();
    health.record_failure("alpha", 1_000);
    health.record_failure("alpha", 1_100);
    assert_eq!(
        health.record_failure("beta", 1_200),
        super::HealthVerdict::Healthy
    );
    assert_eq!(
        health.record_failure("alpha", 1_300),
        super::HealthVerdict::ShouldDisable
    );
}

#[test]
fn reset_after_disable_starts_clean() {
    let mut health = SkillHealth::with_defaults();
    health.record_failure("alpha", 1_000);
    health.record_failure("alpha", 1_100);
    health.reset("alpha");
    assert_eq!(health.recent_failures("alpha", 5_000), 0);
}
