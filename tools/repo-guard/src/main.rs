use repo_guard::{resolve_from, run, GuardConfig};
use std::env;
use std::path::PathBuf;
use std::process::ExitCode;

fn main() -> ExitCode {
    match execute(env::args().skip(1)) {
        Ok(passed) if passed => ExitCode::SUCCESS,
        Ok(_) => ExitCode::from(1),
        Err(message) => {
            eprintln!("repo-guard: {message}");
            ExitCode::from(2)
        }
    }
}

fn execute(arguments: impl Iterator<Item = String>) -> Result<bool, String> {
    let mut root = PathBuf::from(".");
    let mut artifact = None;
    let mut arguments = arguments.peekable();
    if arguments.peek().map(String::as_str) == Some("scan") {
        arguments.next();
    }
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--root" => root = PathBuf::from(arguments.next().ok_or("--root requires a path")?),
            "--artifact-path" | "--artifact-dir" => {
                artifact = Some(arguments.next().ok_or("--artifact-path requires a path")?)
            }
            "--help" | "-h" => {
                println!("repo-guard scan [--root PATH] [--artifact-path FILE_OR_DIRECTORY]");
                return Ok(true);
            }
            other => return Err(format!("unknown argument `{other}`")),
        }
    }

    let root = root.canonicalize().map_err(|error| error.to_string())?;
    let mut config = GuardConfig::new(&root);
    config.artifact_dir = artifact.map(|value| resolve_from(&root, &value));
    let report = run(&config).map_err(|error| error.to_string())?;
    println!(
        "{}",
        serde_json::to_string_pretty(&report).map_err(|error| error.to_string())?
    );
    Ok(report.passed)
}
