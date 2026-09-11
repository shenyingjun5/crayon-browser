//! WFL-13 version history: capture, rollback consumption, bounded
//! history and no-prior rejection.

use super::{VersionHistory, MAX_HISTORY_PER_SKILL};
use crayon_domain::{ActionKind, Recipe, SemanticNodeId};

fn recipe(name: &str, summary: &str) -> Recipe {
    Recipe::new(
        "https://example.com".to_owned(),
        name,
        1,
        vec![crayon_domain::RecipeStep {
            node: SemanticNodeId::new("node1").unwrap(),
            action: ActionKind::Click,
            summary: summary.to_owned(),
        }],
    )
    .unwrap()
}

#[test]
fn capture_and_rollback_roundtrip() {
    let mut history = VersionHistory::new();
    history.capture("alpha", recipe("alpha", "v1"), 1);
    history.capture("alpha", recipe("alpha", "v2"), 2);
    assert_eq!(history.history_len("alpha"), 2);

    let prior = history.pop_previous("alpha").expect("prior");
    assert_eq!(prior.revision, 2);
    assert_eq!(prior.recipe.steps[0].summary, "v2");
    let older = history.pop_previous("alpha").expect("older prior");
    assert_eq!(older.revision, 1);
    assert!(history.pop_previous("alpha").is_none()); // exhausted
}

#[test]
fn skills_are_isolated_in_history() {
    let mut history = VersionHistory::new();
    history.capture("alpha", recipe("alpha", "a"), 1);
    assert!(history.previous("beta").is_none());
    assert!(history.pop_previous("beta").is_none());
}

#[test]
fn history_bound_evicts_oldest() {
    let mut history = VersionHistory::new();
    for index in 0..MAX_HISTORY_PER_SKILL + 3 {
        history.capture(
            "alpha",
            recipe("alpha", &format!("v{index}")),
            index as u64 + 1,
        );
    }
    assert_eq!(history.history_len("alpha"), MAX_HISTORY_PER_SKILL);
    let most_recent = history.previous("alpha").expect("prior");
    // The 3 oldest (rev 1-3) were evicted; rev 11 is the most recent.
    assert_eq!(most_recent.revision, 11);
}
