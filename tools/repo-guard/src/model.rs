use serde::Serialize;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Severity {
    Warning,
    Error,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CheckStatus {
    Passed,
    Warning,
    Failed,
    NotApplicable,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct Finding {
    pub severity: Severity,
    pub path: String,
    pub line: Option<usize>,
    pub message: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct CheckResult {
    pub id: String,
    pub status: CheckStatus,
    pub summary: String,
    pub findings: Vec<Finding>,
}

impl CheckResult {
    pub fn applicable(id: &str, summary: impl Into<String>, findings: Vec<Finding>) -> Self {
        let status = if findings.iter().any(|item| item.severity == Severity::Error) {
            CheckStatus::Failed
        } else if findings.is_empty() {
            CheckStatus::Passed
        } else {
            CheckStatus::Warning
        };
        Self {
            id: id.to_owned(),
            status,
            summary: summary.into(),
            findings,
        }
    }

    pub fn not_applicable(id: &str, summary: impl Into<String>) -> Self {
        Self {
            id: id.to_owned(),
            status: CheckStatus::NotApplicable,
            summary: summary.into(),
            findings: Vec::new(),
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct Report {
    pub schema_version: u32,
    pub root: String,
    pub passed: bool,
    pub checks: Vec<CheckResult>,
}

impl Report {
    pub fn new(root: String, checks: Vec<CheckResult>) -> Self {
        let passed = checks
            .iter()
            .all(|check| check.status != CheckStatus::Failed);
        Self {
            schema_version: 1,
            root,
            passed,
            checks,
        }
    }

    pub fn check(&self, id: &str) -> Option<&CheckResult> {
        self.checks.iter().find(|check| check.id == id)
    }
}
