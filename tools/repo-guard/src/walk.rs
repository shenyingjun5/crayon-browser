use std::fs;
use std::io;
use std::path::{Path, PathBuf};

const SKIPPED_DIRECTORIES: &[&str] = &[
    ".git",
    ".idea",
    ".vscode",
    "target",
    "node_modules",
    "gen",
    "vendor",
];
const ROOT_SKIPPED_DIRECTORIES: &[&str] = &[".cache"];

pub fn collect_files(root: &Path) -> io::Result<Vec<PathBuf>> {
    let mut files = Vec::new();
    visit(root, root, &mut files)?;
    files.sort();
    Ok(files)
}

fn visit(root: &Path, directory: &Path, files: &mut Vec<PathBuf>) -> io::Result<()> {
    for entry in fs::read_dir(directory)? {
        let entry = entry?;
        let path = entry.path();
        let file_type = entry.file_type()?;
        if file_type.is_symlink() {
            continue;
        }
        if file_type.is_dir() {
            let name = entry.file_name();
            let name_is_skipped = SKIPPED_DIRECTORIES
                .iter()
                .any(|item| name.to_string_lossy().eq_ignore_ascii_case(item));
            let root_cache_is_skipped = directory == root
                && ROOT_SKIPPED_DIRECTORIES
                    .iter()
                    .any(|item| name.to_string_lossy().eq_ignore_ascii_case(item));
            if name_is_skipped || root_cache_is_skipped {
                continue;
            }
            // Git submodules are separately versioned source inputs. Their production
            // sources and manifests must not be treated as files owned by this repo.
            if path.join(".git").is_file() {
                continue;
            }
            visit(root, &path, files)?;
        } else if file_type.is_file() {
            files.push(path.strip_prefix(root).unwrap_or(&path).to_path_buf());
        }
    }
    Ok(())
}

pub fn display_path(path: &Path) -> String {
    path.to_string_lossy().replace('\\', "/")
}

pub fn is_test_path(path: &Path) -> bool {
    let display = display_path(path).to_ascii_lowercase();
    let file_name = path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    display.split('/').any(|part| {
        matches!(
            part,
            "test" | "tests" | "testing" | "fixtures" | "test-support" | "test_support"
        )
    }) || file_name.ends_with("_tests.rs")
        || file_name.contains(".test.")
        || file_name.contains(".spec.")
}
