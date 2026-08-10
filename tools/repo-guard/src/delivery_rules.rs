use crate::model::{CheckResult, Finding, Severity};
use crate::walk::{collect_files, display_path};
use std::collections::BTreeSet;
use std::fs;
use std::io::Read;
use std::path::Path;

const FORBIDDEN_RELEASE_MARKERS: &[&[u8]] = &[
    b"/proxy/{*rest}",
    b"/api/extract",
    b"/probeplayer",
    b"v.currentTime = v.duration",
    b"adskip",
    b"skip-ad",
    b"remote-debugging-port",
];

pub fn release_assets(artifact_path: Option<&Path>) -> CheckResult {
    let Some(artifact_path) = artifact_path else {
        return CheckResult::not_applicable(
            "RG-006",
            "release artifact scan requires an explicit --artifact-path",
        );
    };
    if !artifact_path.exists() {
        return CheckResult::applicable(
            "RG-006",
            "release artifact path must exist",
            vec![Finding {
                severity: Severity::Error,
                path: display_path(artifact_path),
                line: None,
                message: "release artifact path does not exist".to_owned(),
            }],
        );
    }

    let mut findings = Vec::new();
    let (base, files) = if artifact_path.is_file() {
        (
            artifact_path.parent().unwrap_or_else(|| Path::new(".")),
            vec![artifact_path
                .file_name()
                .map(std::path::PathBuf::from)
                .unwrap_or_default()],
        )
    } else {
        (
            artifact_path,
            collect_files(artifact_path).unwrap_or_default(),
        )
    };
    {
        for file in files {
            let lower = display_path(&file).to_ascii_lowercase();
            if contains_test_asset_marker(&lower) {
                findings.push(Finding {
                    severity: Severity::Error,
                    path: display_path(&file),
                    line: None,
                    message: "test/debug asset found in release artifact".to_owned(),
                });
            }
            if let Ok(Some(marker)) = find_forbidden_content(&base.join(&file)) {
                findings.push(Finding {
                    severity: Severity::Error,
                    path: display_path(&file),
                    line: None,
                    message: format!(
                        "forbidden legacy/test marker found in release artifact: {marker}"
                    ),
                });
            }
        }
    }
    CheckResult::applicable(
        "RG-006",
        "release artifacts exclude test, fixture, mock, and remote-debug assets",
        findings,
    )
}

fn find_forbidden_content(path: &Path) -> std::io::Result<Option<&'static str>> {
    let mut file = fs::File::open(path)?;
    let overlap = FORBIDDEN_RELEASE_MARKERS
        .iter()
        .map(|marker| marker.len())
        .max()
        .unwrap_or(1)
        .saturating_sub(1);
    let mut carry = Vec::new();
    let mut chunk = [0u8; 64 * 1024];
    loop {
        let read = file.read(&mut chunk)?;
        if read == 0 {
            return Ok(None);
        }
        carry.extend_from_slice(&chunk[..read]);
        for marker in FORBIDDEN_RELEASE_MARKERS {
            if carry.windows(marker.len()).any(|window| window == *marker) {
                return Ok(Some(std::str::from_utf8(marker).unwrap_or("<binary>")));
            }
        }
        if carry.len() > overlap {
            carry.drain(..carry.len() - overlap);
        }
    }
}

fn contains_test_asset_marker(path: &str) -> bool {
    if path.contains("remote-debug") {
        return true;
    }
    path.split(|character: char| !character.is_ascii_alphanumeric())
        .any(|part| {
            matches!(
                part,
                "test" | "tests" | "fixture" | "fixtures" | "mock" | "mocks"
            )
        })
}

pub fn schema_compatibility(root: &Path) -> CheckResult {
    let current = root.join("schemas/current");
    let previous = root.join("schemas/previous");
    if !current.is_dir() || !previous.is_dir() {
        return CheckResult::not_applicable(
            "RG-007",
            "schema compatibility requires schemas/current and schemas/previous",
        );
    }

    let current_files = json_files(&current);
    let previous_files = json_files(&previous);
    let mut findings = Vec::new();
    for missing in previous_files.difference(&current_files) {
        findings.push(Finding {
            severity: Severity::Error,
            path: format!("schemas/current/{missing}"),
            line: None,
            message: "previous schema golden vector is missing from current set".to_owned(),
        });
    }
    for (base, files) in [
        ("schemas/current", &current_files),
        ("schemas/previous", &previous_files),
    ] {
        for file in files {
            let path = root.join(base).join(file);
            match fs::read_to_string(&path)
                .ok()
                .and_then(|text| serde_json::from_str::<serde_json::Value>(&text).ok())
            {
                Some(_) => {}
                None => findings.push(Finding {
                    severity: Severity::Error,
                    path: format!("{base}/{file}"),
                    line: None,
                    message: "schema golden vector is not valid JSON".to_owned(),
                }),
            }
        }
    }
    CheckResult::applicable(
        "RG-007",
        "current and previous schema golden vectors are present and valid JSON",
        findings,
    )
}

fn json_files(directory: &Path) -> BTreeSet<String> {
    collect_files(directory)
        .unwrap_or_default()
        .into_iter()
        .filter(|path| path.extension().and_then(|value| value.to_str()) == Some("json"))
        .map(|path| display_path(&path))
        .collect()
}
