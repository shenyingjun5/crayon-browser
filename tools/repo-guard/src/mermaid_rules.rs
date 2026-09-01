use crate::model::{CheckResult, Finding, Severity};
use crate::walk::{collect_files, display_path};
use serde_json::Value;
use std::collections::BTreeSet;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

const MANIFEST_PATH: &str = "third_party/mermaid/manifest.json";
const NOTICE_PATH: &str = "third_party/mermaid/VENDORED.md";
const LICENSE_PATH: &str = "third_party/mermaid/LICENSE";
const EXPECTED_SCHEMA: &str = "crayon-mermaid-assets/v1";
const EXPECTED_VERSION: &str = "11.17.2";
const EXPECTED_FILE_COUNT: usize = 104;
const EXPECTED_TOTAL_BYTES: u64 = 3_522_090;
const RELEASE_NOTICE_NAME: &str = "THIRD_PARTY_NOTICES.md";
const RELEASE_SBOM_NAME: &str = "mermaid.spdx.json";
const RELEASE_MANIFEST_NAME: &str = "mermaid-manifest.json";

pub fn write_release_metadata(root: &Path, output_dir: &Path) -> io::Result<()> {
    let manifest_text = fs::read_to_string(root.join(MANIFEST_PATH))?;
    let manifest: Value = serde_json::from_str(&manifest_text).map_err(io::Error::other)?;
    let package = manifest
        .get("package")
        .ok_or_else(|| io::Error::other("Mermaid package metadata is missing"))?;
    let version = package
        .get("version")
        .and_then(Value::as_str)
        .ok_or_else(|| io::Error::other("Mermaid version is missing"))?;
    let source = package
        .get("source")
        .and_then(Value::as_str)
        .ok_or_else(|| io::Error::other("Mermaid source is missing"))?;
    let checksum = package
        .get("tarballSha256")
        .and_then(Value::as_str)
        .ok_or_else(|| io::Error::other("Mermaid checksum is missing"))?;
    let notice = format!(
        "{}\n\n## Full license\n\n{}",
        fs::read_to_string(root.join(NOTICE_PATH))?,
        fs::read_to_string(root.join(LICENSE_PATH))?
    );
    let sbom = serde_json::json!({
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "crayon-browser-mermaid-runtime",
        "documentNamespace": format!(
            "https://crayon.invalid/spdx/mermaid/{version}/{checksum}"
        ),
        "creationInfo": {
            "created": "2026-08-29T00:00:00Z",
            "creators": ["Tool: crayon-repo-guard"]
        },
        "packages": [{
            "name": "mermaid",
            "SPDXID": "SPDXRef-Package-mermaid",
            "versionInfo": version,
            "downloadLocation": source,
            "filesAnalyzed": false,
            "licenseConcluded": "MIT",
            "licenseDeclared": "MIT",
            "copyrightText": "NOASSERTION",
            "checksums": [{"algorithm": "SHA256", "checksumValue": checksum}],
            "externalRefs": [{
                "referenceCategory": "PACKAGE-MANAGER",
                "referenceType": "purl",
                "referenceLocator": format!("pkg:npm/mermaid@{version}")
            }]
        }],
        "relationships": [{
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": "SPDXRef-Package-mermaid"
        }]
    });
    fs::create_dir_all(output_dir)?;
    fs::write(output_dir.join(RELEASE_NOTICE_NAME), notice)?;
    fs::write(
        output_dir.join(RELEASE_SBOM_NAME),
        format!(
            "{}\n",
            serde_json::to_string_pretty(&sbom).map_err(io::Error::other)?
        ),
    )?;
    fs::write(output_dir.join(RELEASE_MANIFEST_NAME), manifest_text)?;
    Ok(())
}

pub fn release_assets(root: &Path, artifact_path: Option<&Path>) -> CheckResult {
    let manifest_path = root.join(MANIFEST_PATH);
    if !manifest_path.is_file() {
        return CheckResult::not_applicable(
            "RG-009",
            "Mermaid release closure requires third_party/mermaid/manifest.json",
        );
    }

    let mut findings = Vec::new();
    let Some(manifest) = read_json(&manifest_path, &mut findings) else {
        return result(findings);
    };
    let resource_ids = validate_manifest(root, &manifest, &mut findings);
    validate_notice_and_license(root, &manifest, &mut findings);

    if let Some(artifact_path) = artifact_path {
        validate_artifact(artifact_path, &resource_ids, &mut findings);
        validate_release_metadata(root, artifact_path, &mut findings);
    }
    result(findings)
}

fn validate_release_metadata(root: &Path, artifact_path: &Path, findings: &mut Vec<Finding>) {
    let Some(output_dir) = artifact_path.is_dir().then_some(artifact_path) else {
        findings.push(error(
            artifact_path,
            "Mermaid release scan requires a distribution directory with NOTICE/SBOM sidecars",
        ));
        return;
    };
    let notice_path = output_dir.join(RELEASE_NOTICE_NAME);
    let sbom_path = output_dir.join(RELEASE_SBOM_NAME);
    let release_manifest_path = output_dir.join(RELEASE_MANIFEST_NAME);
    let notice = fs::read_to_string(&notice_path).unwrap_or_default();
    if !notice.contains("mermaid@11.17.2")
        || !notice.contains("MIT License")
        || !notice.contains("Permission is hereby granted")
    {
        findings.push(error(
            &notice_path,
            "release NOTICE is missing Mermaid provenance or full MIT license",
        ));
    }
    let source_manifest_text = fs::read_to_string(root.join(MANIFEST_PATH)).unwrap_or_default();
    let source_manifest = serde_json::from_str::<Value>(&source_manifest_text).ok();
    let expected_source = source_manifest
        .as_ref()
        .and_then(|value| value.pointer("/package/source"))
        .and_then(Value::as_str);
    let expected_checksum = source_manifest
        .as_ref()
        .and_then(|value| value.pointer("/package/tarballSha256"))
        .and_then(Value::as_str);
    let sbom = fs::read_to_string(&sbom_path)
        .ok()
        .and_then(|text| serde_json::from_str::<Value>(&text).ok());
    let sbom_valid = sbom.as_ref().is_some_and(|value| {
        value.pointer("/spdxVersion").and_then(Value::as_str) == Some("SPDX-2.3")
            && value.pointer("/packages/0/name").and_then(Value::as_str) == Some("mermaid")
            && value
                .pointer("/packages/0/versionInfo")
                .and_then(Value::as_str)
                == Some(EXPECTED_VERSION)
            && value
                .pointer("/packages/0/licenseDeclared")
                .and_then(Value::as_str)
                == Some("MIT")
            && value
                .pointer("/packages/0/downloadLocation")
                .and_then(Value::as_str)
                == expected_source
            && value
                .pointer("/packages/0/checksums/0/checksumValue")
                .and_then(Value::as_str)
                == expected_checksum
    });
    if !sbom_valid {
        findings.push(error(
            &sbom_path,
            "release SPDX SBOM is missing the locked Mermaid package",
        ));
    }
    let source_manifest = source_manifest_text.into_bytes();
    let release_manifest = fs::read(&release_manifest_path).unwrap_or_default();
    if source_manifest.is_empty() || source_manifest != release_manifest {
        findings.push(error(
            &release_manifest_path,
            "release Mermaid manifest is missing or differs from the source lock",
        ));
    }
}

fn result(findings: Vec<Finding>) -> CheckResult {
    CheckResult::applicable(
        "RG-009",
        "Mermaid release closure, NOTICE/SBOM inputs, and embedded resource IDs are locked",
        findings,
    )
}

fn read_json(path: &Path, findings: &mut Vec<Finding>) -> Option<Value> {
    match fs::read_to_string(path)
        .ok()
        .and_then(|text| serde_json::from_str(&text).ok())
    {
        Some(value) => Some(value),
        None => {
            findings.push(error(path, "Mermaid manifest is not valid JSON"));
            None
        }
    }
}

fn validate_manifest(root: &Path, manifest: &Value, findings: &mut Vec<Finding>) -> Vec<String> {
    let value = |pointer: &str| manifest.pointer(pointer);
    let locked = value("/schema").and_then(Value::as_str) == Some(EXPECTED_SCHEMA)
        && value("/package/name").and_then(Value::as_str) == Some("mermaid")
        && value("/package/version").and_then(Value::as_str) == Some(EXPECTED_VERSION)
        && value("/package/license").and_then(Value::as_str) == Some("MIT")
        && value("/policy/entry").and_then(Value::as_str) == Some("mermaid.esm.min.mjs")
        && value("/policy/externalImports").and_then(Value::as_u64) == Some(0)
        && value("/policy/networkImports").and_then(Value::as_u64) == Some(0)
        && value("/policy/totalBytes").and_then(Value::as_u64) == Some(EXPECTED_TOTAL_BYTES);
    if !locked {
        findings.push(error(
            &root.join(MANIFEST_PATH),
            "Mermaid package/schema/license/import/byte lock drifted",
        ));
    }

    let Some(files) = value("/files").and_then(Value::as_array) else {
        findings.push(error(
            &root.join(MANIFEST_PATH),
            "Mermaid manifest files array is missing",
        ));
        return Vec::new();
    };
    if files.len() != EXPECTED_FILE_COUNT {
        findings.push(error(
            &root.join(MANIFEST_PATH),
            "Mermaid manifest must contain exactly 104 runtime assets",
        ));
    }

    let mut unique = BTreeSet::new();
    let mut resource_ids = Vec::new();
    for file in files {
        let Some(path) = file.get("path").and_then(Value::as_str) else {
            findings.push(error(
                &root.join(MANIFEST_PATH),
                "Mermaid manifest resource path is missing",
            ));
            continue;
        };
        let lower = path.to_ascii_lowercase();
        if path.starts_with('/')
            || path.contains("..")
            || lower.contains("tiny")
            || lower.contains("node_modules")
            || !unique.insert(path.to_owned())
        {
            findings.push(error(
                &root.join(MANIFEST_PATH),
                "Mermaid resource path is unsafe, duplicate, or tiny-derived",
            ));
            continue;
        }
        let asset = root.join("third_party/mermaid/assets").join(path);
        if !asset.is_file() {
            findings.push(error(&asset, "manifest Mermaid asset is missing"));
        }
        resource_ids.push(path.to_owned());
    }
    resource_ids
}

fn validate_notice_and_license(root: &Path, manifest: &Value, findings: &mut Vec<Finding>) {
    let notice_path = root.join(NOTICE_PATH);
    let license_path = root.join(LICENSE_PATH);
    let notice = fs::read_to_string(&notice_path).unwrap_or_default();
    let license = fs::read_to_string(&license_path).unwrap_or_default();
    let expected = [
        "mermaid@11.17.2",
        "MIT",
        manifest
            .pointer("/package/source")
            .and_then(Value::as_str)
            .unwrap_or("<missing-source>"),
        manifest
            .pointer("/package/tarballSha256")
            .and_then(Value::as_str)
            .unwrap_or("<missing-sha256>"),
    ];
    if expected.iter().any(|marker| !notice.contains(marker)) {
        findings.push(error(
            &notice_path,
            "Mermaid NOTICE input is missing version/license/source/checksum",
        ));
    }
    if !license.contains("MIT License") || !license.contains("Permission is hereby granted") {
        findings.push(error(
            &license_path,
            "Mermaid MIT license text is missing or incomplete",
        ));
    }
}

fn validate_artifact(artifact_path: &Path, resource_ids: &[String], findings: &mut Vec<Finding>) {
    let executable = find_main_executable(artifact_path, findings);
    let files = if artifact_path.is_dir() {
        collect_files(artifact_path).unwrap_or_default()
    } else {
        Vec::new()
    };
    for file in files {
        let lower = display_path(&file).to_ascii_lowercase();
        if lower.contains("node_modules")
            || lower.contains("npm-cache")
            || lower.contains("mermaid-tiny")
            || lower.contains("@mermaid-js/tiny")
        {
            findings.push(error(
                &artifact_path.join(file),
                "development cache or Mermaid tiny asset found in release artifact",
            ));
        }
    }
    let Some(executable) = executable else {
        return;
    };
    let bytes = match fs::read(&executable) {
        Ok(bytes) => bytes,
        Err(_) => {
            findings.push(error(
                &executable,
                "cannot read CrayonBrowser release executable",
            ));
            return;
        }
    };
    // Windows ships a bootstrap exe plus the real payload in
    // CrayonBrowser.dll; embedded resource IDs live in the DLL there.
    let sibling_payload = executable_sibling_payload(&executable);
    for resource_id in resource_ids {
        if !resource_id_present(&bytes, sibling_payload.as_deref(), resource_id) {
            findings.push(error(
                &executable,
                &format!("embedded Mermaid resource ID is missing: {resource_id}"),
            ));
        }
    }
}

fn executable_sibling_payload(executable: &Path) -> Option<Vec<u8>> {
    if executable.extension().and_then(|ext| ext.to_str()) != Some("exe") {
        return None;
    }
    fs::read(executable.with_extension("dll")).ok()
}

fn resource_id_present(
    executable: &[u8],
    sibling_payload: Option<&[u8]>,
    resource_id: &str,
) -> bool {
    contains_bytes(executable, resource_id.as_bytes())
        || sibling_payload.is_some_and(|bytes| contains_bytes(bytes, resource_id.as_bytes()))
}

fn find_main_executable(artifact_path: &Path, findings: &mut Vec<Finding>) -> Option<PathBuf> {
    if artifact_path.is_file() {
        return Some(artifact_path.to_path_buf());
    }
    let candidates: Vec<PathBuf> = collect_files(artifact_path)
        .unwrap_or_default()
        .into_iter()
        .filter(|path| {
            matches!(
                path.file_name().and_then(|name| name.to_str()),
                Some("CrayonBrowser" | "CrayonBrowser.exe")
            )
        })
        .map(|path| artifact_path.join(path))
        .collect();
    if candidates.len() == 1 {
        return candidates.into_iter().next();
    }
    findings.push(error(
        artifact_path,
        "release artifact must contain exactly one CrayonBrowser executable",
    ));
    None
}

fn contains_bytes(haystack: &[u8], needle: &[u8]) -> bool {
    !needle.is_empty()
        && haystack
            .windows(needle.len())
            .any(|window| window == needle)
}

fn error(path: &Path, message: &str) -> Finding {
    Finding {
        severity: Severity::Error,
        path: display_path(path),
        line: None,
        message: message.to_owned(),
    }
}

#[cfg(test)]
mod tests {
    use super::resource_id_present;

    #[test]
    fn resource_id_in_windows_dll_payload_satisfies_scan() {
        // MRT-09W regression: the Windows bootstrap exe does not embed the
        // Mermaid resource IDs; they live in the sibling CrayonBrowser.dll.
        let exe = b"bootstrap-only";
        let dll = b"payload ... chunks/mermaid.esm.min/x.mjs ...";
        assert!(resource_id_present(
            exe,
            Some(dll),
            "chunks/mermaid.esm.min/x.mjs"
        ));
    }

    #[test]
    fn missing_resource_id_still_fails() {
        let exe = b"bootstrap-only";
        let dll = b"payload without the id";
        assert!(!resource_id_present(exe, Some(dll), "mermaid.esm.min.mjs"));
        assert!(!resource_id_present(exe, None, "mermaid.esm.min.mjs"));
    }

    #[test]
    fn resource_id_in_executable_itself_satisfies_scan() {
        let exe = b"single binary with mermaid.esm.min.mjs inside";
        assert!(resource_id_present(exe, None, "mermaid.esm.min.mjs"));
    }
}
