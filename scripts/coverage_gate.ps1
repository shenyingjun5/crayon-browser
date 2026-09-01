# QAR-04W: auditable Rust line-coverage gate over the cargo-llvm-cov
# workspace run.  Thresholds only move up; every exemption carries a
# reason.  Coverage is a gate signal, not a correctness proof.
[CmdletBinding()]
param(
    [string]$Config = 'tools/coverage-gate.json',
    [string]$ReportDir = 'target/coverage-gate',
    # Fail when cargo-llvm-cov is missing (CI); without it the gate reports
    # NOT_RUN and exits 0 so local runs without the tool stay honest.
    [switch]$Require,
    # Reuse an existing coverage.json instead of re-running the instrumented
    # test suite (used by the negative self-check).
    [switch]$ReuseReport,
    # Override thresholds at runtime (negative self-check only).
    [double]$Multiplier = 1.0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
    $reportPath = Join-Path $ReportDir 'report.json'
    $coveragePath = Join-Path $ReportDir 'coverage.json'

    $startedAt = [DateTime]::UtcNow
    $toolVersion = $null
    try {
        $toolVersion = (& cargo llvm-cov --version 2>$null | Select-Object -First 1)
    } catch {
        $toolVersion = $null
    }
    if (-not $toolVersion) {
        $result = [ordered]@{
            schema_version = 1
            status         = 'NOT_RUN'
            reason         = 'cargo-llvm-cov is not installed'
            started_at     = $startedAt.ToString('o')
        }
        ($result | ConvertTo-Json) | Set-Content -Encoding utf8 $reportPath
        Write-Host 'coverage gate NOT_RUN: cargo-llvm-cov is not installed'
        if ($Require) { exit 2 }
        exit 0
    }

    if (-not $ReuseReport) {
        # --json writes a clean LLVM export to stdout; test-runner chatter
        # stays on stderr and is kept as the failure-localization log.
        # PS 5.1 would turn any stderr line into a NativeCommandError under
        # Stop, so run the native tool under Continue and rely on exit codes.
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & cargo llvm-cov --workspace --json `
            > $coveragePath 2> (Join-Path $ReportDir 'llvm-cov.log')
        $covExit = $LASTEXITCODE
        $ErrorActionPreference = $previousPreference
        if ($covExit -ne 0) {
            Write-Host "cargo llvm-cov failed with exit $covExit"
            exit $covExit
        }
    }
    if (-not (Test-Path $coveragePath)) {
        throw "coverage export missing: $coveragePath"
    }

    # This host's ConvertFrom-Json passes file content with a trailing
    # newline through unparsed (observed on both PS 5.1 and pwsh 7.6);
    # Trim() the payload before parsing.
    $configText = [System.IO.File]::ReadAllText((Join-Path $root $Config))
    $gateConfig = $configText.Trim() | ConvertFrom-Json
    $coverageText = [System.IO.File]::ReadAllText((Join-Path $root $coveragePath))
    $coverage = $coverageText.Trim() | ConvertFrom-Json
    $coverageText = $null

    $perCrate = @{}
    foreach ($dataset in $coverage.data) {
        foreach ($file in $dataset.files) {
            $name = ($file.filename -replace '\\', '/')
            $crate = $null
            if ($name -match '(?:^|/)(crates|tools)/([^/]+)/') {
                $crate = "$($Matches[1])/$($Matches[2])"
            } else {
                continue
            }
            $lines = $file.summary.lines
            if (-not $perCrate.ContainsKey($crate)) {
                $perCrate[$crate] = [ordered]@{ covered = 0.0; total = 0.0 }
            }
            $perCrate[$crate].covered += $lines.covered
            $perCrate[$crate].total += $lines.count
        }
    }

    $exempted = @{}
    foreach ($exemption in $gateConfig.exemptions) {
        $exempted[$exemption.crate] = $exemption.reason
    }

    $failures = @()
    $rows = @()
    foreach ($entry in $gateConfig.crates.PSObject.Properties) {
        $crate = $entry.Name
        $threshold = [double]$entry.Value * $Multiplier
        if (-not $perCrate.ContainsKey($crate)) {
            if ($exempted.ContainsKey($crate)) { continue }
            $failures += "${crate}: no instrumented lines found (crate missing from build?)"
            continue
        }
        $stats = $perCrate[$crate]
        $percent = if ($stats.total -gt 0) { 100.0 * $stats.covered / $stats.total } else { 100.0 }
        $rows += [ordered]@{
            crate     = $crate
            covered   = [int64]$stats.covered
            total     = [int64]$stats.total
            percent   = [math]::Round($percent, 2)
            threshold = $threshold
        }
        if ($percent + 0.001 -lt $threshold) {
            $failures += "${crate}: $([math]::Round($percent,2))% < threshold $threshold%"
        }
    }

    $result = [ordered]@{
        schema_version = 1
        status         = $(if ($failures.Count -eq 0) { 'PASS' } else { 'FAIL' })
        tool           = $toolVersion
        started_at     = $startedAt.ToString('o')
        multiplier     = $Multiplier
        crates         = $rows
        exemptions     = @($gateConfig.exemptions)
        failures       = $failures
    }
    ($result | ConvertTo-Json -Depth 6) | Set-Content -Encoding utf8 $reportPath

    foreach ($row in $rows) {
        Write-Host ("{0,-42} {1,7}%  (threshold {2}%)" -f $row.crate, $row.percent, $row.threshold)
    }
    if ($failures.Count -gt 0) {
        foreach ($failure in $failures) { Write-Host "FAIL: $failure" }
        exit 1
    }
    Write-Host 'coverage gate PASS'
    exit 0
} finally {
    Pop-Location
}
