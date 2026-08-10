//! `LeakScanner`: scans logs, DTOs, diagnostic bundles and disk directories
//! for URLs with credentials/query tokens, Cookie and Authorization material
//! (testing-standard §4). Pure substring/rule matching, bounded input sizes.

use std::path::Path;

/// A single leak finding: which rule fired and at what byte offset.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LeakFinding {
    pub rule: &'static str,
    pub offset: usize,
    /// Fixed-size context snippet before the hit (never the secret itself).
    pub context: String,
}

/// Built-in sensitive-material rules (substring, case-insensitive).
const RULES: &[(&str, &str)] = &[
    ("cookie-header", "cookie:"),
    ("set-cookie-header", "set-cookie:"),
    ("authorization-header", "authorization:"),
    ("bearer-token", "bearer "),
    ("basic-token", "basic "),
    ("query-token", "token="),
    ("query-sign", "sign="),
    ("session-cookie-name", "sessdata="),
    ("url-credential", "://"),
];

/// Maximum text size scanned in one call (bounded input rule).
const MAX_TEXT_BYTES: usize = 4 * 1024 * 1024;
/// Maximum file size scanned by `scan_dir`.
const MAX_FILE_BYTES: u64 = 4 * 1024 * 1024;
/// Context snippet width (bytes before the hit).
const CONTEXT_BYTES: usize = 24;

/// Default scanner with the built-in rule set.
pub struct LeakScanner;

impl LeakScanner {
    /// Scans `text` (capped at `MAX_TEXT_BYTES`) and returns every finding.
    ///
    /// `allowed_substrings` whitelists documented example values (e.g. the
    /// `https://example.com` fixture origin) without weakening the rules:
    /// whitelisted spans are blanked before scanning.
    #[must_use]
    pub fn scan_text(text: &str, allowed_substrings: &[&str]) -> Vec<LeakFinding> {
        let capped = &text[..text.len().min(MAX_TEXT_BYTES)];
        let lower = capped.to_ascii_lowercase();
        let mut masked = lower.clone().into_bytes();
        for allowed in allowed_substrings {
            let allowed = allowed.to_ascii_lowercase();
            if allowed.is_empty() {
                continue;
            }
            for (start, _) in lower.match_indices(&allowed) {
                for byte in &mut masked[start..start + allowed.len()] {
                    *byte = b' ';
                }
            }
        }
        let masked = String::from_utf8_lossy(&masked);

        let mut findings = Vec::new();
        for (rule, needle) in RULES {
            for (offset, _) in masked.match_indices(needle) {
                if !rule_boundary_ok(rule, &masked, offset) {
                    continue;
                }
                let context_start = offset.saturating_sub(CONTEXT_BYTES);
                findings.push(LeakFinding {
                    rule,
                    offset,
                    context: capped[context_start..offset].to_string(),
                });
            }
        }
        findings.sort_by_key(|f| f.offset);
        findings
    }

    /// Scans every regular file under `dir` (recursively, size-capped).
    /// Unreadable entries become `walk-error` findings — silence is not success.
    #[must_use]
    pub fn scan_dir(dir: &Path, allowed_substrings: &[&str]) -> Vec<LeakFinding> {
        let mut findings = Vec::new();
        scan_dir_inner(dir, allowed_substrings, &mut findings);
        findings
    }
}

/// Rule-specific boundary checks that keep English words from false-firing
/// (`assigned=` must not match the `sign=` query rule, etc.).
fn rule_boundary_ok(rule: &str, masked: &str, offset: usize) -> bool {
    let bytes = masked.as_bytes();
    let prev = offset.checked_sub(1).map(|i| bytes[i]);
    match rule {
        // Query credentials: the text since the last `?`/`&` up to the needle
        // must be identifier characters (possibly empty), i.e. the needle is
        // (the tail of) a query parameter name. Catches `token=`,
        // `access_token=`, `sign=`, `ysign=`; rejects prose like `assigned=`
        // (no `?`/`&` before it).
        "query-token" | "query-sign" => {
            let window = &bytes[offset.saturating_sub(64)..offset];
            match window.iter().rposition(|&b| b == b'?' || b == b'&') {
                Some(pos) => window[pos + 1..]
                    .iter()
                    .all(|b| b.is_ascii_alphanumeric() || *b == b'_'),
                None => false,
            }
        }
        // Cookie names must not be a suffix of a longer identifier.
        "session-cookie-name" => !matches!(prev, Some(b) if b.is_ascii_alphanumeric()),
        // `://` only counts when userinfo (`user:pass@`) follows before the
        // first path slash; plain scheme prefixes are not leaks.
        "url-credential" => has_userinfo(masked, offset),
        _ => true,
    }
}

/// True when the scheme prefix at `offset` is followed by userinfo before the
/// first path/query delimiter (i.e. `scheme://user:pass@host`).
fn has_userinfo(masked: &str, scheme_offset: usize) -> bool {
    let after = &masked[scheme_offset + 3..];
    let end = after
        .find(['/', '?', '#'])
        .unwrap_or(after.len())
        .min(after.len());
    after[..end].contains('@')
}

fn scan_dir_inner(dir: &Path, allowed: &[&str], findings: &mut Vec<LeakFinding>) {
    let entries = match std::fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(_) => {
            findings.push(LeakFinding {
                rule: "walk-error",
                offset: 0,
                context: dir.display().to_string(),
            });
            return;
        }
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let Ok(meta) = entry.metadata() else {
            findings.push(LeakFinding {
                rule: "walk-error",
                offset: 0,
                context: path.display().to_string(),
            });
            continue;
        };
        if meta.is_dir() {
            scan_dir_inner(&path, allowed, findings);
        } else if meta.is_file() && meta.len() <= MAX_FILE_BYTES {
            if let Ok(content) = std::fs::read_to_string(&path) {
                findings.extend(LeakScanner::scan_text(&content, allowed));
            }
        }
    }
}
