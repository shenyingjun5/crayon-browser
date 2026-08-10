use crate::model::{CheckResult, Finding, Severity};
use crate::walk::display_path;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Component, Path, PathBuf};

#[derive(Debug)]
struct Manifest {
    path: PathBuf,
    package: Option<String>,
    features: BTreeMap<String, BTreeSet<String>>,
    dependencies: Vec<Dependency>,
}

#[derive(Debug)]
struct Dependency {
    name: String,
    value: String,
    production: bool,
    line: usize,
}

#[derive(Debug)]
struct CastSourceLock {
    submodule_path: String,
}

const LEGACY_ROOT_DEPENDENCIES: &[&str] = &[
    "axum",
    "base64",
    "clap",
    "futures-util",
    "percent-encoding",
    "regex",
    "reqwest",
    "serde",
    "serde_json",
    "tokio",
    "tracing",
    "tracing-subscriber",
    "url",
];

pub fn inspect(root: &Path, files: &[PathBuf]) -> Vec<CheckResult> {
    let manifests: Vec<Manifest> = files
        .iter()
        .filter(|path| path.file_name().and_then(|name| name.to_str()) == Some("Cargo.toml"))
        .filter_map(|path| parse_manifest(root, path).ok())
        .collect();

    let dependency_isolation = dependency_isolation(&manifests);
    let (cast_source, cast_source_findings) = cast_source_lock(root);
    let (architecture, cast_pin) =
        dependency_architecture(&manifests, cast_source.as_ref(), cast_source_findings);
    vec![dependency_isolation, architecture, cast_pin]
}

fn parse_manifest(root: &Path, path: &Path) -> Result<Manifest, std::io::Error> {
    let text = fs::read_to_string(root.join(path))?;
    let mut section = String::new();
    let mut package = None;
    let mut features = BTreeMap::new();
    let mut dependencies = Vec::new();
    let lines: Vec<&str> = text.lines().collect();
    let mut index = 0;
    while index < lines.len() {
        let trimmed = lines[index].trim();
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            section = trimmed.to_ascii_lowercase();
            index += 1;
            continue;
        }
        let Some((key, value)) = trimmed.split_once('=') else {
            index += 1;
            continue;
        };
        let key = key.trim().trim_matches('\"').to_owned();
        if section == "[package]" && key == "name" {
            package = Some(value.trim().trim_matches('\"').to_owned());
        }
        if section == "[features]" {
            let mut feature_value = value.trim().to_owned();
            let mut bracket_depth = delimiter_depth(&feature_value, '[', ']');
            while bracket_depth > 0 && index + 1 < lines.len() {
                index += 1;
                let continuation = lines[index].trim();
                feature_value.push(' ');
                feature_value.push_str(continuation);
                bracket_depth += delimiter_depth(continuation, '[', ']');
            }
            let members = feature_value
                .trim_start_matches('[')
                .trim_end_matches(']')
                .split(',')
                .map(|member| member.trim().trim_matches('\"'))
                .filter(|member| !member.is_empty())
                .map(str::to_owned)
                .collect();
            features.insert(key.clone(), members);
        }
        if section.contains("dependencies") {
            let dependency_line = index + 1;
            let mut dependency_value = value.trim().to_owned();
            let mut brace_depth = delimiter_depth(&dependency_value, '{', '}');
            while brace_depth > 0 && index + 1 < lines.len() {
                index += 1;
                let continuation = lines[index].trim();
                dependency_value.push(' ');
                dependency_value.push_str(continuation);
                brace_depth += delimiter_depth(continuation, '{', '}');
            }
            dependencies.push(Dependency {
                name: key,
                value: dependency_value,
                production: !section.contains("dev-dependencies"),
                line: dependency_line,
            });
        }
        index += 1;
    }
    Ok(Manifest {
        path: path.to_path_buf(),
        package,
        features,
        dependencies,
    })
}

fn delimiter_depth(text: &str, open: char, close: char) -> isize {
    text.chars().fold(0, |depth, character| {
        depth + if character == open { 1 } else { 0 } - if character == close { 1 } else { 0 }
    })
}

fn dependency_isolation(manifests: &[Manifest]) -> CheckResult {
    let forbidden = [
        "mockall",
        "proptest",
        "quickcheck",
        "rstest",
        "test-support",
        "test_support",
    ];
    let mut findings = Vec::new();
    for manifest in manifests {
        for dependency in manifest.dependencies.iter().filter(|item| item.production) {
            let name = dependency.name.to_ascii_lowercase();
            if forbidden.iter().any(|item| name.contains(item)) {
                findings.push(Finding {
                    severity: Severity::Error,
                    path: display_path(&manifest.path),
                    line: Some(dependency.line),
                    message: format!("production dependency `{}` is test-only", dependency.name),
                });
            }
        }
    }
    CheckResult::applicable(
        "RG-001",
        "production dependency sections exclude test frameworks and test-support",
        findings,
    )
}

fn dependency_architecture(
    manifests: &[Manifest],
    cast_source: Option<&CastSourceLock>,
    mut pin_findings: Vec<Finding>,
) -> (CheckResult, CheckResult) {
    let mut architecture_findings = Vec::new();
    let mut cast_count = 0;
    let package_names: BTreeSet<String> = manifests
        .iter()
        .filter_map(|manifest| manifest.package.clone())
        .collect();
    let mut graph: BTreeMap<String, Vec<String>> = BTreeMap::new();

    for manifest in manifests {
        let Some(package) = &manifest.package else {
            continue;
        };
        let edges = graph.entry(package.clone()).or_default();
        for dependency in manifest.dependencies.iter().filter(|item| item.production) {
            enforce_product_boundaries(manifest, package, dependency, &mut architecture_findings);
            if package_names.contains(&dependency.name) {
                edges.push(dependency.name.clone());
            }
            let dependency_name = dependency.name.to_ascii_lowercase();
            if is_cast_sdk_dependency(&dependency_name) {
                cast_count += 1;
                if !package.to_ascii_lowercase().contains("cast-adapter") {
                    architecture_findings.push(Finding {
                        severity: Severity::Error,
                        path: display_path(&manifest.path),
                        line: Some(dependency.line),
                        message: "only a cast-adapter package may depend on Cast-SDK".to_owned(),
                    });
                }
                if !is_pinned_cast_dependency(dependency, cast_source) {
                    pin_findings.push(Finding {
                        severity: Severity::Error,
                        path: display_path(&manifest.path),
                        line: Some(dependency.line),
                        message: "Cast-SDK dependency must use the locked submodule path; without a source lock, git dependencies require a full rev"
                            .to_owned(),
                    });
                }
            }
        }
    }

    if let Some(cycle) = find_cycle(&graph) {
        architecture_findings.push(Finding {
            severity: Severity::Error,
            path: "Cargo.toml".to_owned(),
            line: None,
            message: format!("workspace dependency cycle: {}", cycle.join(" -> ")),
        });
    }

    let architecture = CheckResult::applicable(
        "RG-005",
        "workspace graph and formal/adapter dependency boundaries are enforced",
        architecture_findings,
    );
    let cast_pin = if cast_count == 0 && cast_source.is_none() && pin_findings.is_empty() {
        CheckResult::not_applicable(
            "RG-008",
            "Cast-SDK source lock and dependencies are not yet present",
        )
    } else {
        CheckResult::applicable(
            "RG-008",
            "Cast-SDK source lock and adapter dependencies are pinned",
            pin_findings,
        )
    };
    (architecture, cast_pin)
}

fn is_cast_sdk_dependency(name: &str) -> bool {
    name.contains("cast-sdk") || name.contains("cast_sdk") || name.starts_with("cast-sender-")
}

fn is_pinned_cast_dependency(
    dependency: &Dependency,
    cast_source: Option<&CastSourceLock>,
) -> bool {
    let lower_value = dependency.value.to_ascii_lowercase();
    if let Some(source) = cast_source {
        let normalized_path = source
            .submodule_path
            .replace('\\', "/")
            .to_ascii_lowercase();
        return lower_value.contains("path")
            && lower_value.replace('\\', "/").contains(&normalized_path);
    }
    if lower_value.contains("path") || lower_value.contains("branch") || lower_value.contains("tag")
    {
        return false;
    }
    let git = dependency_inline_string(&dependency.value, "git");
    let revision = dependency_inline_string(&dependency.value, "rev");
    git.is_some_and(|url| url.starts_with("https://"))
        && revision.is_some_and(|commit| is_git_commit(&commit))
}

fn dependency_inline_string(value: &str, key: &str) -> Option<String> {
    value
        .trim()
        .trim_start_matches('{')
        .trim_end_matches('}')
        .split(',')
        .filter_map(|field| field.split_once('='))
        .find_map(|(candidate, value)| {
            (candidate.trim() == key).then(|| value.trim().trim_matches('"').to_owned())
        })
}

fn cast_source_lock(root: &Path) -> (Option<CastSourceLock>, Vec<Finding>) {
    let lock_path = root.join("config/cast-sdk-source.toml");
    if !lock_path.is_file() {
        return (None, Vec::new());
    }

    let mut findings = Vec::new();
    let text = match fs::read_to_string(&lock_path) {
        Ok(text) => text,
        Err(error) => {
            findings.push(cast_source_finding(format!(
                "cannot read source lock: {error}"
            )));
            return (None, findings);
        }
    };
    let value = |key: &str| {
        text.lines().find_map(|line| {
            let (candidate, value) = line.split_once('=')?;
            (candidate.trim() == key).then(|| value.trim().trim_matches('"').to_owned())
        })
    };
    let schema_version = value("schema_version");
    let repository = value("repository");
    let revision = value("revision");
    let submodule_path = value("submodule_path");

    if schema_version.as_deref() != Some("1") {
        findings.push(cast_source_finding("schema_version must be 1".to_owned()));
    }
    let Some(repository) = repository else {
        findings.push(cast_source_finding("repository is required".to_owned()));
        return (None, findings);
    };
    let Some(revision) = revision else {
        findings.push(cast_source_finding("revision is required".to_owned()));
        return (None, findings);
    };
    let Some(submodule_path) = submodule_path else {
        findings.push(cast_source_finding("submodule_path is required".to_owned()));
        return (None, findings);
    };

    if !repository.starts_with("https://") {
        findings.push(cast_source_finding(
            "repository must use an HTTPS remote URL".to_owned(),
        ));
    }
    if !is_git_commit(&revision) {
        findings.push(cast_source_finding(
            "revision must be a 40-character lowercase git commit".to_owned(),
        ));
    }

    let source_path = Path::new(&submodule_path);
    if source_path.is_absolute()
        || source_path.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        })
    {
        findings.push(cast_source_finding(
            "submodule_path must be a repository-relative path without parent traversal".to_owned(),
        ));
    } else {
        let checkout = root.join(source_path);
        if !checkout.is_dir() || !checkout.join(".git").is_file() {
            findings.push(cast_source_finding(
                "submodule checkout is missing or not initialized".to_owned(),
            ));
        } else {
            match submodule_head(root, &checkout) {
                Ok(head) if head != revision => findings.push(cast_source_finding(format!(
                    "submodule HEAD {head} does not match locked revision {revision}"
                ))),
                Ok(_) => {}
                Err(message) => findings.push(cast_source_finding(message)),
            }
        }
    }

    let gitmodules = fs::read_to_string(root.join(".gitmodules")).unwrap_or_default();
    let expected_path = format!("path = {submodule_path}");
    let expected_url = format!("url = {repository}");
    if !gitmodules.lines().any(|line| line.trim() == expected_path) {
        findings.push(cast_source_finding(
            ".gitmodules path does not match source lock".to_owned(),
        ));
    }
    if !gitmodules.lines().any(|line| line.trim() == expected_url) {
        findings.push(cast_source_finding(
            ".gitmodules URL does not match source lock".to_owned(),
        ));
    }

    (Some(CastSourceLock { submodule_path }), findings)
}

fn submodule_head(root: &Path, checkout: &Path) -> Result<String, String> {
    let marker = fs::read_to_string(checkout.join(".git"))
        .map_err(|error| format!("cannot read submodule gitdir marker: {error}"))?;
    let git_dir_value = marker
        .trim()
        .strip_prefix("gitdir:")
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "submodule .git file does not contain a gitdir".to_owned())?;
    let git_dir_path = Path::new(git_dir_value);
    let unresolved_git_dir = if git_dir_path.is_absolute() {
        git_dir_path.to_path_buf()
    } else {
        checkout.join(git_dir_path)
    };
    let git_dir = fs::canonicalize(unresolved_git_dir)
        .map_err(|error| format!("cannot resolve submodule gitdir: {error}"))?;
    let canonical_root = fs::canonicalize(root)
        .map_err(|error| format!("cannot resolve repository root: {error}"))?;
    if !git_dir.starts_with(&canonical_root) {
        return Err("submodule gitdir resolves outside the repository".to_owned());
    }

    let head = fs::read_to_string(git_dir.join("HEAD"))
        .map_err(|error| format!("cannot read submodule HEAD: {error}"))?;
    let head = head.trim();
    if is_git_commit(head) {
        return Ok(head.to_owned());
    }
    let reference = head
        .strip_prefix("ref:")
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "submodule HEAD is neither a commit nor a symbolic ref".to_owned())?;
    let reference_path = Path::new(reference);
    if reference_path.is_absolute()
        || reference_path.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        })
    {
        return Err("submodule HEAD ref is not repository-relative".to_owned());
    }
    if let Ok(commit) = fs::read_to_string(git_dir.join(reference_path)) {
        let commit = commit.trim();
        if is_git_commit(commit) {
            return Ok(commit.to_owned());
        }
    }
    let packed_refs = fs::read_to_string(git_dir.join("packed-refs"))
        .map_err(|error| format!("cannot resolve submodule HEAD ref {reference}: {error}"))?;
    packed_refs
        .lines()
        .filter(|line| !line.starts_with('#') && !line.starts_with('^'))
        .filter_map(|line| line.split_once(' '))
        .find_map(|(commit, candidate)| {
            (candidate == reference && is_git_commit(commit)).then(|| commit.to_owned())
        })
        .ok_or_else(|| format!("cannot resolve submodule HEAD ref {reference}"))
}

fn is_git_commit(value: &str) -> bool {
    value.len() == 40
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn cast_source_finding(message: String) -> Finding {
    Finding {
        severity: Severity::Error,
        path: "config/cast-sdk-source.toml".to_owned(),
        line: None,
        message,
    }
}

fn enforce_product_boundaries(
    manifest: &Manifest,
    package: &str,
    dependency: &Dependency,
    findings: &mut Vec<Finding>,
) {
    let package = package.to_ascii_lowercase();
    let dependency_name = dependency.name.to_ascii_lowercase();
    let concrete_runtime_tokens = [
        "arkweb", "cast-sdk", "cast_sdk", "cef", "tauri", "windows", "winapi",
    ];
    let domain_forbidden_tokens = [
        "ark", "axum", "cast", "cef", "hyper", "reqwest", "tauri", "tokio", "tower", "windows",
    ];
    enforce_formal_root_boundary(manifest, &package, dependency, findings);

    if package == "crayon-domain"
        && domain_forbidden_tokens
            .iter()
            .any(|token| dependency_name.contains(token))
    {
        findings.push(Finding {
            severity: Severity::Error,
            path: display_path(&manifest.path),
            line: Some(dependency.line),
            message: format!(
                "crayon-domain cannot depend on network, platform, UI, or Cast package `{}`",
                dependency.name
            ),
        });
    }

    if package == "crayon-app-runtime"
        && concrete_runtime_tokens
            .iter()
            .any(|token| dependency_name.contains(token))
    {
        findings.push(Finding {
            severity: Severity::Error,
            path: display_path(&manifest.path),
            line: Some(dependency.line),
            message: format!(
                "crayon-app-runtime must use interfaces instead of concrete package `{}`",
                dependency.name
            ),
        });
    }

    if dependency_name == "crayon-legacy-adapter" && package != "get-video-app" {
        findings.push(Finding {
            severity: Severity::Error,
            path: display_path(&manifest.path),
            line: Some(dependency.line),
            message:
                "only the explicitly excluded get-video-app may depend on crayon-legacy-adapter"
                    .to_owned(),
        });
    }
}

fn enforce_formal_root_boundary(
    manifest: &Manifest,
    package: &str,
    dependency: &Dependency,
    findings: &mut Vec<Finding>,
) {
    let dependency_name = dependency.name.to_ascii_lowercase();
    if package != "get-video" || !LEGACY_ROOT_DEPENDENCIES.contains(&dependency_name.as_str()) {
        return;
    }

    let normalized_value = dependency
        .value
        .chars()
        .filter(|character| !character.is_ascii_whitespace())
        .collect::<String>()
        .to_ascii_lowercase();
    let feature_member = format!("dep:{dependency_name}");
    let enabled_by_legacy = manifest
        .features
        .get("legacy-dev")
        .is_some_and(|members| members.contains(&feature_member));
    let enabled_by_other_feature = manifest
        .features
        .iter()
        .any(|(feature, members)| feature != "legacy-dev" && members.contains(&feature_member));
    if normalized_value.contains("optional=true") && enabled_by_legacy && !enabled_by_other_feature
    {
        return;
    }

    findings.push(Finding {
        severity: Severity::Error,
        path: display_path(&manifest.path),
        line: Some(dependency.line),
        message: format!(
            "formal root legacy dependency `{}` must be optional and enabled exclusively by legacy-dev",
            dependency.name
        ),
    });
}

fn find_cycle(graph: &BTreeMap<String, Vec<String>>) -> Option<Vec<String>> {
    fn visit(
        node: &str,
        graph: &BTreeMap<String, Vec<String>>,
        active: &mut Vec<String>,
        complete: &mut BTreeSet<String>,
    ) -> Option<Vec<String>> {
        if let Some(index) = active.iter().position(|item| item == node) {
            let mut cycle = active[index..].to_vec();
            cycle.push(node.to_owned());
            return Some(cycle);
        }
        if complete.contains(node) {
            return None;
        }
        active.push(node.to_owned());
        for next in graph.get(node).into_iter().flatten() {
            if let Some(cycle) = visit(next, graph, active, complete) {
                return Some(cycle);
            }
        }
        active.pop();
        complete.insert(node.to_owned());
        None
    }

    let mut complete = BTreeSet::new();
    for node in graph.keys() {
        if let Some(cycle) = visit(node, graph, &mut Vec::new(), &mut complete) {
            return Some(cycle);
        }
    }
    None
}
