//! LeakScanner self-tests: rule coverage, boundary false-positive guards,
//! allowlist masking and directory scanning with bounded input.

use test_support::leak_scanner::LeakScanner;

#[test]
fn detects_cookie_authorization_and_tokens() {
    let text = "log line\nCookie: SESSDATA=abc; other=1\nAuthorization: Bearer xyz\n\
                media=https://cdn.example.com/v.m3u8?token=deadbeef&x=1";
    let rules: Vec<&str> = LeakScanner::scan_text(text, &[])
        .iter()
        .map(|f| f.rule)
        .collect();
    assert!(rules.contains(&"cookie-header"));
    assert!(rules.contains(&"session-cookie-name"));
    assert!(rules.contains(&"authorization-header"));
    assert!(rules.contains(&"bearer-token"));
    assert!(rules.contains(&"query-token"));
}

#[test]
fn english_words_and_plain_urls_do_not_fire() {
    let text = "assigned=1\nredesign=2\nhttps://example.com/watch\nsigning complete";
    let findings = LeakScanner::scan_text(text, &[]);
    assert!(findings.is_empty(), "unexpected: {findings:?}");
}

#[test]
fn url_userinfo_is_a_credential_leak() {
    let findings = LeakScanner::scan_text("fetch https://user:pass@cdn.example.com/v.mp4", &[]);
    assert!(findings.iter().any(|f| f.rule == "url-credential"));
    // But the same host without userinfo is clean.
    assert!(LeakScanner::scan_text("fetch https://cdn.example.com/v.mp4", &[]).is_empty());
}

#[test]
fn query_sign_param_fires_but_assignment_words_do_not() {
    let findings = LeakScanner::scan_text("https://cdn.example.com/live.m3u8?ysign=abc", &[]);
    assert!(findings.iter().any(|f| f.rule == "query-sign"));
    assert!(LeakScanner::scan_text("assign=1", &[]).is_empty());
}

#[test]
fn allowlist_masks_documented_examples() {
    let text = "see https://user:pass@example.com for the fixture format";
    assert_eq!(LeakScanner::scan_text(text, &[]).len(), 1);
    assert!(LeakScanner::scan_text(text, &["https://user:pass@example.com"]).is_empty());
}

#[test]
fn scan_dir_reports_leaks_and_walk_errors() {
    let root = std::env::temp_dir().join(format!("leak-scan-{}", std::process::id()));
    std::fs::create_dir_all(root.join("sub")).unwrap();
    std::fs::write(root.join("clean.log"), "all good https://example.com").unwrap();
    std::fs::write(root.join("sub/diag.txt"), "Cookie: a=b").unwrap();

    let findings = LeakScanner::scan_dir(&root, &[]);
    assert_eq!(findings.len(), 1);
    assert_eq!(findings[0].rule, "cookie-header");

    let missing = root.join("does-not-exist");
    let findings = LeakScanner::scan_dir(&missing, &[]);
    assert!(findings.iter().any(|f| f.rule == "walk-error"));

    std::fs::remove_dir_all(&root).unwrap();
}
