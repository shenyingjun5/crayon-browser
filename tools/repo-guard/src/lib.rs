mod delivery_rules;
mod manifest_rules;
mod mermaid_rules;
mod model;
mod source_rules;
mod walk;

pub use mermaid_rules::write_release_metadata as write_mermaid_release_metadata;
pub use model::{CheckResult, CheckStatus, Finding, Report, Severity};

use std::io;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug)]
pub struct GuardConfig {
    pub root: PathBuf,
    pub artifact_dir: Option<PathBuf>,
}

impl GuardConfig {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        Self {
            root: root.into(),
            artifact_dir: None,
        }
    }
}

pub fn run(config: &GuardConfig) -> io::Result<Report> {
    let root = config.root.canonicalize()?;
    let files = walk::collect_files(&root)?;
    let mut checks = Vec::new();
    checks.extend(manifest_rules::inspect(&root, &files));
    checks.extend(source_rules::inspect(&root, &files));
    checks.push(mermaid_rules::release_assets(
        &root,
        config.artifact_dir.as_deref(),
    ));
    checks.push(delivery_rules::release_assets(
        config.artifact_dir.as_deref(),
    ));
    checks.push(delivery_rules::schema_compatibility(&root));
    checks.sort_by(|left, right| left.id.cmp(&right.id));
    Ok(Report::new(walk::display_path(&root), checks))
}

pub fn resolve_from(root: &Path, value: &str) -> PathBuf {
    let path = PathBuf::from(value);
    if path.is_absolute() {
        path
    } else {
        root.join(path)
    }
}
