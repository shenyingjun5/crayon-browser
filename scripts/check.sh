#!/usr/bin/env sh
set -eu

mode="${1:-fast}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
cd "$repo_root"

summary=""
overall_passed=true
failure=""

emit_summary() {
    code=$?
    if [ "$code" -ne 0 ]; then overall_passed=false; fi
    printf '{"schema_version":1,"mode":"%s","passed":%s,"failure":"%s","steps":[%s]}\n' \
        "$mode" "$overall_passed" "$failure" "$summary"
}

trap emit_summary EXIT

run_step() {
    step_name=$1
    shift
    start=$(date +%s)
    set +e
    "$@"
    code=$?
    set -e
    end=$(date +%s)
    duration=$(( (end - start) * 1000 ))
    if [ -n "${summary:-}" ]; then summary="$summary,"; fi
    if [ "$code" -eq 0 ]; then
        summary="${summary}{\"name\":\"$step_name\",\"passed\":true,\"duration_ms\":$duration}"
    else
        summary="${summary}{\"name\":\"$step_name\",\"passed\":false,\"duration_ms\":$duration}"
        overall_passed=false
        failure="step $step_name failed with exit code $code"
        return "$code"
    fi
}

case "$mode" in
    fast)
        run_step guard cargo run --quiet -p repo-guard -- scan --root "$repo_root"
        run_step format cargo fmt --all -- --check
        run_step brand-assets-unit node --test tools/brand-assets/tests/managed-paths.test.mjs
        run_step brand-assets node tools/brand-assets/verify.mjs
        run_step formal-workspace cargo test --workspace
        run_step legacy-unit cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib
        ;;
    core)
        run_step formal-workspace cargo test --workspace
        run_step legacy-package cargo test -p crayon-browser-core --no-default-features --features legacy-dev
        ;;
    security)
        run_step guard cargo run --quiet -p repo-guard -- scan --root "$repo_root"
        run_step relay-unit cargo test -p crayon-browser-core --no-default-features --features legacy-dev relay::
        run_step relay-security cargo test --no-default-features --features legacy-dev --test fixtures security::
        ;;
    brand-assets)
        run_step brand-assets-unit node --test tools/brand-assets/tests/managed-paths.test.mjs
        run_step brand-assets node tools/brand-assets/verify.mjs
        ;;
    all)
        run_step guard cargo run --quiet -p repo-guard -- scan --root "$repo_root"
        run_step format cargo fmt --all -- --check
        run_step brand-assets-unit node --test tools/brand-assets/tests/managed-paths.test.mjs
        run_step brand-assets node tools/brand-assets/verify.mjs
        run_step formal-workspace cargo test --workspace
        run_step legacy-package cargo test -p crayon-browser-core --no-default-features --features legacy-dev
        ;;
    *)
        echo "usage: scripts/check.sh [fast|core|security|brand-assets|all]" >&2
        overall_passed=false
        failure="unsupported mode"
        exit 2
        ;;
esac
