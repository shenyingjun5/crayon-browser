use repo_guard::{run, CheckStatus, GuardConfig};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

struct TestRepo {
    path: PathBuf,
}

impl TestRepo {
    fn new(name: &str) -> Self {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = std::env::temp_dir().join(format!(
            "crayon-repo-guard-{}-{name}-{nonce}",
            std::process::id()
        ));
        fs::create_dir_all(path.join("src")).unwrap();
        Self { path }
    }

    fn write(&self, relative: &str, text: &str) {
        let path = self.path.join(relative);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        fs::write(path, text).unwrap();
    }

    fn report(&self) -> repo_guard::Report {
        run(&GuardConfig::new(&self.path)).unwrap()
    }
}

impl Drop for TestRepo {
    fn drop(&mut self) {
        let temp = std::env::temp_dir().canonicalize().unwrap();
        let Ok(path) = self.path.canonicalize() else {
            return;
        };
        if path.starts_with(&temp)
            && path
                .file_name()
                .and_then(|name| name.to_str())
                .map(|name| name.starts_with("crayon-repo-guard-"))
                .unwrap_or(false)
        {
            let _ = fs::remove_dir_all(path);
        }
    }
}

fn basic_manifest(name: &str) -> String {
    format!("[package]\nname = \"{name}\"\nversion = \"0.1.0\"\nedition = \"2021\"\n")
}

fn status(repo: &TestRepo, id: &str) -> CheckStatus {
    repo.report().check(id).unwrap().status
}

#[test]
fn clean_repository_passes_and_absent_checks_are_explicit() {
    let repo = TestRepo::new("clean");
    repo.write("Cargo.toml", &basic_manifest("clean"));
    repo.write("src/lib.rs", "pub fn answer() -> u32 { 42 }\n");
    let report = repo.report();
    assert!(report.passed);
    assert_eq!(report.check("RG-002").unwrap().status, CheckStatus::Passed);
    assert_eq!(
        report.check("RG-006").unwrap().status,
        CheckStatus::NotApplicable
    );
    assert_eq!(
        report.check("RG-007").unwrap().status,
        CheckStatus::NotApplicable
    );
    assert_eq!(
        report.check("RG-008").unwrap().status,
        CheckStatus::NotApplicable
    );
}

#[test]
fn inline_test_body_fails_rg_002() {
    let repo = TestRepo::new("inline-test");
    repo.write("Cargo.toml", &basic_manifest("inline-test"));
    repo.write("src/lib.rs", "#[test]\nfn embedded() {}\n");
    assert_eq!(status(&repo, "RG-002"), CheckStatus::Failed);
}

#[test]
fn test_framework_in_production_dependencies_fails_rg_001() {
    let repo = TestRepo::new("test-dependency");
    repo.write(
        "Cargo.toml",
        "[package]\nname = \"bad\"\nversion = \"0.1.0\"\n[dependencies]\nmockall = \"0.13\"\n",
    );
    repo.write("src/lib.rs", "pub fn value() {}\n");
    assert_eq!(status(&repo, "RG-001"), CheckStatus::Failed);
}

#[test]
fn hardcoded_secret_and_user_path_fail_rg_004() {
    let repo = TestRepo::new("secret");
    repo.write("Cargo.toml", &basic_manifest("secret"));
    repo.write(
        "src/lib.rs",
        "const API_KEY: &str = \"real-secret\";\nconst PATH: &str = \"C:\\\\Users\\\\alice\\\\file\";\n",
    );
    assert_eq!(status(&repo, "RG-004"), CheckStatus::Failed);
}

#[test]
fn sensitive_key_denylist_is_not_a_hardcoded_secret() {
    let repo = TestRepo::new("denylist");
    repo.write("Cargo.toml", &basic_manifest("denylist"));
    repo.write(
        "src/lib.rs",
        "static const char* const kSensitive[] = {\"password\", \"passwd\", \"token\"};\n",
    );
    assert_eq!(status(&repo, "RG-004"), CheckStatus::Passed);
}

#[test]
fn three_thousand_line_test_file_fails_rg_003() {
    let repo = TestRepo::new("large-test");
    repo.write("Cargo.toml", &basic_manifest("large-test"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write("tests/large.rs", &"// fixture\n".repeat(3_000));
    assert_eq!(status(&repo, "RG-003"), CheckStatus::Failed);
}

#[test]
fn cast_sdk_requires_adapter_and_pinned_git_revision() {
    let repo = TestRepo::new("cast-pin");
    repo.write(
        "Cargo.toml",
        "[package]\nname = \"browser\"\nversion = \"0.1.0\"\n[dependencies]\ncast-sdk = { path = \"../cast-sdk\" }\n",
    );
    repo.write("src/lib.rs", "pub fn value() {}\n");
    let report = repo.report();
    assert_eq!(report.check("RG-005").unwrap().status, CheckStatus::Failed);
    assert_eq!(report.check("RG-008").unwrap().status, CheckStatus::Failed);
}

#[test]
fn valid_cast_source_lock_passes_rg_008_without_adapter_dependency() {
    let repo = TestRepo::new("cast-source-lock");
    repo.write("Cargo.toml", &basic_manifest("browser"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write(
        ".gitmodules",
        "[submodule \"third_party/cast-sdk\"]\n\tpath = third_party/cast-sdk\n\turl = https://example.invalid/cast-sdk.git\n",
    );
    repo.write(
        "config/cast-sdk-source.toml",
        "schema_version = 1\nrepository = \"https://example.invalid/cast-sdk.git\"\nrevision = \"0123456789abcdef0123456789abcdef01234567\"\nsubmodule_path = \"third_party/cast-sdk\"\n",
    );
    repo.write(
        "third_party/cast-sdk/.git",
        "gitdir: ../../.git/modules/third_party/cast-sdk\n",
    );
    repo.write(
        ".git/modules/third_party/cast-sdk/HEAD",
        "0123456789abcdef0123456789abcdef01234567\n",
    );

    assert_eq!(status(&repo, "RG-008"), CheckStatus::Passed);
}

#[test]
fn incomplete_cast_source_lock_fails_rg_008() {
    let repo = TestRepo::new("invalid-cast-source-lock");
    repo.write("Cargo.toml", &basic_manifest("browser"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write(
        ".gitmodules",
        "[submodule \"third_party/cast-sdk\"]\n\tpath = third_party/cast-sdk\n\turl = https://example.invalid/other.git\n",
    );
    repo.write(
        "config/cast-sdk-source.toml",
        "schema_version = 1\nrepository = \"https://example.invalid/cast-sdk.git\"\nsubmodule_path = \"third_party/cast-sdk\"\n",
    );
    repo.write(
        "third_party/cast-sdk/.git",
        "gitdir: ../../.git/modules/third_party/cast-sdk\n",
    );
    repo.write(
        ".git/modules/third_party/cast-sdk/HEAD",
        "0123456789abcdef0123456789abcdef01234567\n",
    );

    assert_eq!(status(&repo, "RG-008"), CheckStatus::Failed);
}

#[test]
fn cast_source_lock_rejects_mismatched_checkout_head() {
    let repo = TestRepo::new("cast-source-head-mismatch");
    repo.write("Cargo.toml", &basic_manifest("browser"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write(
        ".gitmodules",
        "[submodule \"third_party/cast-sdk\"]\n\tpath = third_party/cast-sdk\n\turl = https://example.invalid/cast-sdk.git\n",
    );
    repo.write(
        "config/cast-sdk-source.toml",
        "schema_version = 1\nrepository = \"https://example.invalid/cast-sdk.git\"\nrevision = \"0123456789abcdef0123456789abcdef01234567\"\nsubmodule_path = \"third_party/cast-sdk\"\n",
    );
    repo.write(
        "third_party/cast-sdk/.git",
        "gitdir: ../../.git/modules/third_party/cast-sdk\n",
    );
    repo.write(
        ".git/modules/third_party/cast-sdk/HEAD",
        "89abcdef0123456789abcdef0123456789abcdef\n",
    );

    assert_eq!(status(&repo, "RG-008"), CheckStatus::Failed);
}

#[test]
fn cast_adapter_accepts_dependency_below_locked_source_path() {
    let repo = TestRepo::new("cast-source-path-dependency");
    repo.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-cast-adapter\"\nversion = \"0.1.0\"\n[dependencies]\ncast-sender-service = { path = \"third_party/cast-sdk/sender/rust/crates/cast-sender-service\" }\n",
    );
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write(
        ".gitmodules",
        "[submodule \"third_party/cast-sdk\"]\n\tpath = third_party/cast-sdk\n\turl = https://example.invalid/cast-sdk.git\n",
    );
    repo.write(
        "config/cast-sdk-source.toml",
        "schema_version = 1\nrepository = \"https://example.invalid/cast-sdk.git\"\nrevision = \"0123456789abcdef0123456789abcdef01234567\"\nsubmodule_path = \"third_party/cast-sdk\"\n",
    );
    repo.write(
        "third_party/cast-sdk/.git",
        "gitdir: ../../.git/modules/third_party/cast-sdk\n",
    );
    repo.write(
        ".git/modules/third_party/cast-sdk/HEAD",
        "0123456789abcdef0123456789abcdef01234567\n",
    );

    let report = repo.report();
    assert_eq!(report.check("RG-005").unwrap().status, CheckStatus::Passed);
    assert_eq!(report.check("RG-008").unwrap().status, CheckStatus::Passed);
}

#[test]
fn nested_git_submodule_is_not_scanned_as_product_source() {
    let repo = TestRepo::new("git-submodule-boundary");
    repo.write("Cargo.toml", &basic_manifest("browser"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write(
        "third_party/cast-sdk/.git",
        "gitdir: ../../../.git/modules/third_party/cast-sdk\n",
    );
    repo.write(
        "third_party/cast-sdk/Cargo.toml",
        "[package]\nname = \"foreign-cast-sdk\"\nversion = \"0.1.0\"\n[dependencies]\nmockall = \"0.13\"\n",
    );
    repo.write(
        "third_party/cast-sdk/src/lib.rs",
        "#[test]\nfn foreign_test_body() {}\n",
    );

    let report = repo.report();
    assert!(report.passed);
    assert_eq!(report.check("RG-001").unwrap().status, CheckStatus::Passed);
    assert_eq!(report.check("RG-002").unwrap().status, CheckStatus::Passed);
}

#[test]
fn repository_cache_root_is_not_scanned_as_product_source() {
    let repo = TestRepo::new("cache-boundary");
    repo.write("Cargo.toml", &basic_manifest("browser"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write(
        ".cache/cef/vendor/tests/generated.rs",
        &"// generated vendor fixture\n".repeat(3_000),
    );
    repo.write(
        ".cache/build/generated/src/lib.rs",
        "#[test]\nfn generated_test_body() {}\n",
    );

    let report = repo.report();
    assert!(report.passed);
    assert_eq!(report.check("RG-002").unwrap().status, CheckStatus::Passed);
    assert_eq!(report.check("RG-003").unwrap().status, CheckStatus::Passed);
}

#[test]
fn nested_cache_name_does_not_create_a_source_scan_exemption() {
    let repo = TestRepo::new("nested-cache-boundary");
    repo.write("Cargo.toml", &basic_manifest("browser"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write("src/.cache/large.rs", &"// owned source\n".repeat(3_000));

    assert_eq!(status(&repo, "RG-003"), CheckStatus::Warning);
}

#[test]
fn domain_rejects_network_or_platform_dependencies() {
    let repo = TestRepo::new("domain-boundary");
    repo.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-domain\"\nversion = \"0.1.0\"\n[dependencies]\ntokio = \"1\"\n",
    );
    repo.write("src/lib.rs", "pub fn value() {}\n");

    assert_eq!(status(&repo, "RG-005"), CheckStatus::Failed);
}

#[test]
fn app_runtime_rejects_concrete_platform_dependencies() {
    let repo = TestRepo::new("runtime-boundary");
    repo.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-app-runtime\"\nversion = \"0.1.0\"\n[dependencies]\ntauri = \"2\"\n",
    );
    repo.write("src/lib.rs", "pub fn value() {}\n");

    assert_eq!(status(&repo, "RG-005"), CheckStatus::Failed);
}

#[test]
fn legacy_adapter_is_available_only_to_the_explicit_legacy_app() {
    let rejected = TestRepo::new("legacy-rejected");
    rejected.write(
        "Cargo.toml",
        "[package]\nname = \"formal-browser\"\nversion = \"0.1.0\"\n[dependencies]\ncrayon-legacy-adapter = { path = \"crates/legacy\" }\n",
    );
    rejected.write("src/lib.rs", "pub fn value() {}\n");
    assert_eq!(status(&rejected, "RG-005"), CheckStatus::Failed);

    let accepted = TestRepo::new("legacy-accepted");
    accepted.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-legacy-app\"\nversion = \"0.1.0\"\n[dependencies]\ncrayon-legacy-adapter = { path = \"crates/legacy\" }\n",
    );
    accepted.write("src/lib.rs", "pub fn value() {}\n");
    assert_eq!(status(&accepted, "RG-005"), CheckStatus::Passed);
}

#[test]
fn formal_root_requires_legacy_runtime_dependencies_to_be_optional() {
    let rejected = TestRepo::new("formal-root-legacy-runtime");
    rejected.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-browser-core\"\nversion = \"0.1.0\"\n[dependencies]\nreqwest = \"0.12\"\n",
    );
    rejected.write("src/lib.rs", "pub fn value() {}\n");
    assert_eq!(status(&rejected, "RG-005"), CheckStatus::Failed);

    let accepted = TestRepo::new("formal-root-optional-runtime");
    accepted.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-browser-core\"\nversion = \"0.1.0\"\n[features]\nlegacy-dev = [\"dep:reqwest\"]\n[dependencies]\nreqwest = { version = \"0.12\", optional = true }\n",
    );
    accepted.write("src/lib.rs", "pub fn value() {}\n");
    assert_eq!(status(&accepted, "RG-005"), CheckStatus::Passed);

    let leaked = TestRepo::new("formal-root-feature-leak");
    leaked.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-browser-core\"\nversion = \"0.1.0\"\n[features]\nformal-product = [\"dep:reqwest\"]\nlegacy-dev = [\"dep:reqwest\"]\n[dependencies]\nreqwest = { version = \"0.12\", optional = true }\n",
    );
    leaked.write("src/lib.rs", "pub fn value() {}\n");
    assert_eq!(status(&leaked, "RG-005"), CheckStatus::Failed);
}

#[test]
fn release_fixture_asset_fails_rg_006() {
    let repo = TestRepo::new("release-asset");
    repo.write("Cargo.toml", &basic_manifest("release-asset"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write("dist/fixtures/sample.json", "{}\n");
    let mut config = GuardConfig::new(&repo.path);
    config.artifact_dir = Some(repo.path.join("dist"));
    let report = run(&config).unwrap();
    assert_eq!(report.check("RG-006").unwrap().status, CheckStatus::Failed);
}

#[test]
fn ordinary_latest_release_name_does_not_match_test_token() {
    let repo = TestRepo::new("release-name");
    repo.write("Cargo.toml", &basic_manifest("release-name"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write("dist/latest/app.bin", "release\n");
    let mut config = GuardConfig::new(&repo.path);
    config.artifact_dir = Some(repo.path.join("dist"));
    let report = run(&config).unwrap();
    assert_eq!(report.check("RG-006").unwrap().status, CheckStatus::Passed);
}

#[test]
fn legacy_route_bytes_fail_release_scan() {
    let repo = TestRepo::new("release-route");
    repo.write("Cargo.toml", &basic_manifest("release-route"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write("dist/app.bin", "prefix /api/extract suffix\n");
    let mut config = GuardConfig::new(&repo.path);
    config.artifact_dir = Some(repo.path.join("dist"));
    let report = run(&config).unwrap();
    assert_eq!(report.check("RG-006").unwrap().status, CheckStatus::Failed);
}

#[test]
fn multiline_pinned_cast_dependency_passes_rg_008_for_adapter() {
    let repo = TestRepo::new("multiline-cast-pin");
    repo.write(
        "Cargo.toml",
        "[package]\nname = \"crayon-cast-adapter\"\nversion = \"0.1.0\"\n[dependencies]\ncast-sdk = {\n  git = \"https://example.invalid/cast-sdk\",\n  rev = \"0123456789abcdef0123456789abcdef01234567\"\n}\n",
    );
    repo.write("src/lib.rs", "pub fn value() {}\n");
    let report = repo.report();
    assert_eq!(report.check("RG-005").unwrap().status, CheckStatus::Passed);
    assert_eq!(report.check("RG-008").unwrap().status, CheckStatus::Passed);
}

#[test]
fn invalid_or_missing_schema_vectors_fail_rg_007() {
    let repo = TestRepo::new("schema");
    repo.write("Cargo.toml", &basic_manifest("schema"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write("schemas/previous/v1.json", "{}\n");
    repo.write("schemas/current/broken.json", "not-json\n");
    assert_eq!(status(&repo, "RG-007"), CheckStatus::Failed);
}

#[test]
fn temporary_repository_stays_under_system_temp() {
    let path = {
        let repo = TestRepo::new("path-safety");
        assert!(repo.path.starts_with(std::env::temp_dir()));
        assert!(Path::new(&repo.path).is_dir());
        repo.path.clone()
    };
    assert!(!path.exists(), "temporary fixture must be removed on drop");
}

#[test]
fn engineering_tools_are_not_misclassified_as_product_runtime() {
    let repo = TestRepo::new("tool-classification");
    repo.write("Cargo.toml", &basic_manifest("tool-classification"));
    repo.write("src/lib.rs", "pub fn value() {}\n");
    repo.write(
        "tools/policy.rs",
        "const FORBIDDEN: &str = \"Fake /Users/example 127.0.0.1:80\";\n",
    );
    let report = repo.report();
    assert_eq!(report.check("RG-002").unwrap().status, CheckStatus::Passed);
    assert_eq!(report.check("RG-004").unwrap().status, CheckStatus::Passed);
}
