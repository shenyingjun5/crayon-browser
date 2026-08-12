param(
    [Parameter(Position = 0)]
    [string]$Mode = 'fast'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$results = [System.Collections.Generic.List[object]]::new()
$overallPassed = $true
$failure = $null

if ($Mode -notin @('fast', 'core', 'security', 'brand-assets', 'all')) {
    [pscustomobject]@{
        schema_version = 1
        mode = $Mode
        passed = $false
        failure = 'unsupported mode'
        steps = @()
    } | ConvertTo-Json -Depth 4 -Compress
    throw "unsupported mode '$Mode'; expected fast, core, security, brand-assets, or all"
}

function Invoke-CheckStep {
    param(
        [string]$Name,
        [scriptblock]$Action
    )
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    & $Action
    $exitCode = $LASTEXITCODE
    $watch.Stop()
    if ($null -eq $exitCode) { $exitCode = 0 }
    $script:results.Add([pscustomobject]@{
        name = $Name
        passed = ($exitCode -eq 0)
        duration_ms = $watch.ElapsedMilliseconds
    })
    if ($exitCode -ne 0) {
        throw "check step '$Name' failed with exit code $exitCode"
    }
}

Push-Location -LiteralPath $repoRoot
try {
    $steps = switch ($Mode) {
        'fast' { @('guard', 'format', 'brand-assets-unit', 'brand-assets', 'formal-workspace', 'legacy-unit') }
        'core' { @('formal-workspace', 'legacy-package') }
        'security' { @('guard', 'relay-unit', 'relay-security') }
        'brand-assets' { @('brand-assets-unit', 'brand-assets') }
        'all' { @('guard', 'format', 'brand-assets-unit', 'brand-assets', 'formal-workspace', 'legacy-package') }
    }
    foreach ($step in $steps) {
        switch ($step) {
            'guard' { Invoke-CheckStep $step { cargo run --quiet -p repo-guard -- scan --root $repoRoot } }
            'format' { Invoke-CheckStep $step { cargo fmt --all -- --check } }
            'brand-assets-unit' { Invoke-CheckStep $step { node --test tools/brand-assets/tests/managed-paths.test.mjs } }
            'brand-assets' { Invoke-CheckStep $step { node tools/brand-assets/verify.mjs } }
            'formal-workspace' { Invoke-CheckStep $step { cargo test --workspace } }
            'legacy-unit' { Invoke-CheckStep $step { cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib } }
            'legacy-package' { Invoke-CheckStep $step { cargo test -p crayon-browser-core --no-default-features --features legacy-dev } }
            'relay-unit' { Invoke-CheckStep $step { cargo test -p crayon-browser-core --no-default-features --features legacy-dev relay:: } }
            'relay-security' { Invoke-CheckStep $step { cargo test --no-default-features --features legacy-dev --test fixtures security:: } }
        }
    }
}
catch {
    $overallPassed = $false
    $failure = $_.Exception.Message
}
finally {
    Pop-Location
}

[pscustomobject]@{
    schema_version = 1
    mode = $Mode
    passed = $overallPassed
    failure = $failure
    steps = $results
} | ConvertTo-Json -Depth 4 -Compress

if (-not $overallPassed) {
    throw $failure
}
