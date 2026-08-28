use crate::model::{CheckResult, Finding, Severity};
use crate::walk::{display_path, is_test_path};
use std::fs;
use std::path::{Path, PathBuf};

const SOURCE_EXTENSIONS: &[&str] = &[
    "rs", "c", "cc", "cpp", "h", "hpp", "m", "mm", "js", "jsx", "ts", "tsx", "ps1", "sh",
];

pub fn inspect(root: &Path, files: &[PathBuf]) -> Vec<CheckResult> {
    let mut isolation = Vec::new();
    let mut size = Vec::new();
    let mut hardcoding = Vec::new();
    let mut debug_entries = Vec::new();
    let mut unsafe_routes = Vec::new();
    let mut auto_ad = Vec::new();

    for relative in files {
        if !is_source(relative) {
            continue;
        }
        let Ok(text) = fs::read_to_string(root.join(relative)) else {
            continue;
        };
        let test_file = is_test_path(relative);
        inspect_file_size(relative, &text, test_file, &mut size);
        inspect_function_sizes(relative, &text, &mut size);
        if !test_file && is_product_source(relative) {
            inspect_production_isolation(relative, &text, &mut isolation);
            inspect_hardcoding(relative, &text, &mut hardcoding);
            inspect_debug_entries(relative, &text, &mut debug_entries);
            inspect_unsafe_routes(relative, &text, &mut unsafe_routes);
            inspect_auto_ad_behavior(relative, &text, &mut auto_ad);
        }
    }

    vec![
        CheckResult::applicable(
            "RG-002",
            "production sources contain no test bodies or test doubles",
            isolation,
        ),
        CheckResult::applicable(
            "RG-003",
            "file and function size thresholds were evaluated",
            size,
        ),
        CheckResult::applicable(
            "RG-004",
            "credentials and machine-specific paths fail; configurable literals warn",
            hardcoding,
        ),
        CheckResult::applicable(
            "RG-004A",
            "debug entry points are not present in production source",
            debug_entries,
        ),
        CheckResult::applicable(
            "RG-004B",
            "unsafe routes are not present in production source",
            unsafe_routes,
        ),
        CheckResult::applicable(
            "RG-004C",
            "auto ad behavior is not present in production source",
            auto_ad,
        ),
    ]
}

fn is_source(path: &Path) -> bool {
    path.extension()
        .and_then(|value| value.to_str())
        .map(|value| SOURCE_EXTENSIONS.contains(&value.to_ascii_lowercase().as_str()))
        .unwrap_or(false)
}

fn is_product_source(path: &Path) -> bool {
    let display = display_path(path);
    let mut parts = display.split('/');
    match parts.next() {
        Some("tools" | "scripts") => false,
        Some(_) if !display.contains('/') => false,
        _ => true,
    }
}

fn inspect_file_size(path: &Path, text: &str, test_file: bool, findings: &mut Vec<Finding>) {
    let lines = text.lines().count();
    let (warning, error) = if test_file {
        (2_000, 3_000)
    } else {
        (2_000, usize::MAX)
    };
    if lines >= error {
        findings.push(finding(
            Severity::Error,
            path,
            None,
            format!("test file has {lines} lines; files >= 3000 lines are blocked"),
        ));
    } else if lines >= warning {
        findings.push(finding(
            Severity::Warning,
            path,
            None,
            format!(
                "{} file has {lines} lines and requires a split review",
                if test_file { "test" } else { "production" }
            ),
        ));
    }
}

fn inspect_function_sizes(path: &Path, text: &str, findings: &mut Vec<Finding>) {
    if path.extension().and_then(|value| value.to_str()) != Some("rs") {
        return;
    }
    let lines: Vec<&str> = text.lines().collect();
    let mut index = 0;
    while index < lines.len() {
        if !looks_like_rust_function(lines[index]) {
            index += 1;
            continue;
        }
        let start = index;
        let mut depth = 0isize;
        let mut opened = false;
        while index < lines.len() {
            let code = strip_line_comment(lines[index]);
            for character in code.chars() {
                if character == '{' {
                    depth += 1;
                    opened = true;
                } else if character == '}' && opened {
                    depth -= 1;
                }
            }
            index += 1;
            if opened && depth <= 0 {
                break;
            }
        }
        let length = index.saturating_sub(start);
        if length >= 100 {
            findings.push(finding(
                Severity::Warning,
                path,
                Some(start + 1),
                format!(
                    "function is approximately {length} lines{}",
                    if length >= 200 {
                        "; strong split review required"
                    } else {
                        ""
                    }
                ),
            ));
        }
    }
}

fn looks_like_rust_function(line: &str) -> bool {
    let line = line.trim_start();
    !line.starts_with("//")
        && [
            "fn ",
            "pub fn ",
            "pub(crate) fn ",
            "async fn ",
            "pub async fn ",
            "pub(crate) async fn ",
        ]
        .iter()
        .any(|prefix| line.starts_with(prefix))
}

fn strip_line_comment(line: &str) -> &str {
    line.split_once("//").map(|(code, _)| code).unwrap_or(line)
}

fn inspect_production_isolation(path: &Path, text: &str, findings: &mut Vec<Finding>) {
    for (index, line) in text.lines().enumerate() {
        let trimmed = line.trim();
        if trimmed.starts_with("#[test]") || trimmed.starts_with("#[tokio::test") {
            findings.push(finding(
                Severity::Error,
                path,
                Some(index + 1),
                "test body attribute is not allowed in a production source file".to_owned(),
            ));
        }
        if ["Mock", "Fake", "testOnly"]
            .iter()
            .any(|marker| contains_identifier(line, marker))
        {
            findings.push(finding(
                Severity::Error,
                path,
                Some(index + 1),
                "test-double identifier is not allowed in production source".to_owned(),
            ));
        }
    }
}

fn inspect_hardcoding(path: &Path, text: &str, findings: &mut Vec<Finding>) {
    for (index, line) in text.lines().enumerate() {
        let trimmed = line.trim_start();
        if trimmed.starts_with("//") || trimmed.starts_with('#') {
            continue;
        }
        let lower = line.to_ascii_lowercase();
        if looks_like_secret_assignment(&lower) {
            findings.push(finding(
                Severity::Error,
                path,
                Some(index + 1),
                "possible hard-coded credential".to_owned(),
            ));
        }
        if line.contains("C:\\Users\\") || line.contains("/Users/") || line.contains("/home/") {
            findings.push(finding(
                Severity::Error,
                path,
                Some(index + 1),
                "machine-specific absolute user path".to_owned(),
            ));
        }
        if line.contains("127.0.0.1:")
            || line.contains("\"0.0.0.0\"")
            || line.contains("Duration::from_secs(")
            || line.contains("Duration::from_millis(")
        {
            findings.push(finding(
                Severity::Warning,
                path,
                Some(index + 1),
                "network address, port, or timeout literal should come from typed configuration"
                    .to_owned(),
            ));
        }
    }
}

/// Debug entry points must not appear in production source.
/// Covers debug assertions, devtools flags and remote-debugging hooks.
fn inspect_debug_entries(path: &Path, text: &str, findings: &mut Vec<Finding>) {
    for (index, line) in text.lines().enumerate() {
        let trimmed = line.trim();
        if trimmed.starts_with("//") || trimmed.starts_with('#') {
            continue;
        }
        let lower = line.to_ascii_lowercase();
        if lower.contains("debug_assert!(")
            || lower.contains("debug_assert! (")
            || lower.contains("debug_assert_eq!(")
            || lower.contains("debug_assert_eq! (")
            || lower.contains("debug_assert_ne!(")
            || lower.contains("debug_assert_ne! (")
            || lower.contains("devtools")
            || lower.contains("remote_debugging")
            || lower.contains("chrome_devtools")
        {
            findings.push(finding(
                Severity::Error,
                path,
                Some(index + 1),
                "debug entry point in production source".to_owned(),
            ));
        }
    }
}

/// Unsafe route patterns must not appear in production source.
/// Covers legacy extraction, open proxy and player/probe endpoints.
fn inspect_unsafe_routes(path: &Path, text: &str, findings: &mut Vec<Finding>) {
    // The legacy extraction and relay paths (src/main.rs, src/relay/)
    // are allowed under the `legacy-dev` feature; they are not
    // production surfaces.
    // Windows reports `\` separators; normalise before matching the
    // legacy-dev exemption paths.
    let display = path.to_string_lossy().replace('\\', "/");
    if display == "src/main.rs" || display.starts_with("src/relay/") {
        return;
    }
    let patterns = [
        "/api/extract",
        "/api/proxy",
        "/proxy?",
        "/player?",
        "/probe?",
        "/api/v1/extract",
    ];
    for (index, line) in text.lines().enumerate() {
        let trimmed = line.trim();
        if trimmed.starts_with("//") || trimmed.starts_with('#') {
            continue;
        }
        for pattern in &patterns {
            if line.contains(pattern) {
                findings.push(finding(
                    Severity::Error,
                    path,
                    Some(index + 1),
                    format!("unsafe route pattern `{pattern}` in production source"),
                ));
            }
        }
    }
}

/// Automatic ad behavior (auto-play, ad skip, ad block) must not
/// appear in production source.
fn inspect_auto_ad_behavior(path: &Path, text: &str, findings: &mut Vec<Finding>) {
    // Only match actual auto-ad behavior assignments/calls, not
    // protection-against-autoplay code (e.g. BR-005 gate).
    let patterns = [
        "autoplay = true",
        "set_autoplay(true)",
        "trigger_autoplay",
        "skip_ad()",
        "skip_ads()",
        "auto_skip_ad",
        "ad_skip()",
        "enable_ad_block",
        "enable_adblock",
    ];
    for (index, line) in text.lines().enumerate() {
        let trimmed = line.trim();
        if trimmed.starts_with("//") || trimmed.starts_with('#') {
            continue;
        }
        let lower = line.to_ascii_lowercase();
        for pattern in &patterns {
            if lower.contains(pattern) {
                findings.push(finding(
                    Severity::Error,
                    path,
                    Some(index + 1),
                    format!("auto ad behavior pattern `{pattern}` in production source"),
                ));
            }
        }
    }
}

fn looks_like_secret_assignment(line: &str) -> bool {
    let names = [
        "password",
        "passwd",
        "api_key",
        "apikey",
        "secret_key",
        "access_token",
        "private_key",
    ];
    if line.contains("env::") || line.contains("getenv") {
        return false;
    }
    // The sensitive name must be followed on the same line by an assignment
    // to a string literal: `password = "x"` or `const API_KEY: &str = "x"`.
    // A denylist array entry like `{"password", "token"}` has no `=` after
    // the name, and `password == "x"` compares rather than assigns.
    names.iter().any(|name| {
        line.match_indices(name).any(|(index, _)| {
            line[index + name.len()..]
                .split_once('=')
                .map(|(_, value)| value.trim_start().starts_with(['"', '\'']))
                .unwrap_or(false)
        })
    })
}

fn contains_identifier(line: &str, marker: &str) -> bool {
    line.match_indices(marker).any(|(index, _)| {
        let before = line[..index].chars().next_back();
        let after = line[index + marker.len()..].chars().next();
        !before.map(is_identifier_char).unwrap_or(false)
            && !after.map(is_identifier_char).unwrap_or(false)
    })
}

fn is_identifier_char(value: char) -> bool {
    value.is_ascii_alphanumeric() || value == '_'
}

fn finding(severity: Severity, path: &Path, line: Option<usize>, message: String) -> Finding {
    Finding {
        severity,
        path: display_path(path),
        line,
        message,
    }
}
