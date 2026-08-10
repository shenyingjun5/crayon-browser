use crate::model::{CheckResult, Finding, Severity};
use crate::walk::display_path;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug)]
struct Manifest {
    path: PathBuf,
    package: Option<String>,
    dependencies: Vec<Dependency>,
}

#[derive(Debug)]
struct Dependency {
    name: String,
    value: String,
    production: bool,
    line: usize,
}

pub fn inspect(root: &Path, files: &[PathBuf]) -> Vec<CheckResult> {
    let manifests: Vec<Manifest> = files
        .iter()
        .filter(|path| path.file_name().and_then(|name| name.to_str()) == Some("Cargo.toml"))
        .filter_map(|path| parse_manifest(root, path).ok())
        .collect();

    let dependency_isolation = dependency_isolation(&manifests);
    let (architecture, cast_pin) = dependency_architecture(&manifests);
    vec![dependency_isolation, architecture, cast_pin]
}

fn parse_manifest(root: &Path, path: &Path) -> Result<Manifest, std::io::Error> {
    let text = fs::read_to_string(root.join(path))?;
    let mut section = String::new();
    let mut package = None;
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

fn dependency_architecture(manifests: &[Manifest]) -> (CheckResult, CheckResult) {
    let mut architecture_findings = Vec::new();
    let mut pin_findings = Vec::new();
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
            if dependency_name.contains("cast-sdk") || dependency_name.contains("cast_sdk") {
                cast_count += 1;
                if !package.to_ascii_lowercase().contains("cast-adapter") {
                    architecture_findings.push(Finding {
                        severity: Severity::Error,
                        path: display_path(&manifest.path),
                        line: Some(dependency.line),
                        message: "only a cast-adapter package may depend on Cast-SDK".to_owned(),
                    });
                }
                let lower_value = dependency.value.to_ascii_lowercase();
                if lower_value.contains("path")
                    || !lower_value.contains("git")
                    || !lower_value.contains("rev")
                {
                    pin_findings.push(Finding {
                        severity: Severity::Error,
                        path: display_path(&manifest.path),
                        line: Some(dependency.line),
                        message:
                            "Cast-SDK must be a git dependency pinned with rev and without path"
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
        "workspace dependency graph is acyclic and Cast-SDK is adapter-only",
        architecture_findings,
    );
    let cast_pin = if cast_count == 0 {
        CheckResult::not_applicable(
            "RG-008",
            "Cast-SDK is not yet present in workspace manifests",
        )
    } else {
        CheckResult::applicable(
            "RG-008",
            "Cast-SDK dependencies are git revisions without local path fallback",
            pin_findings,
        )
    };
    (architecture, cast_pin)
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
