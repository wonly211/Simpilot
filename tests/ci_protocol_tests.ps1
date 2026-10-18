Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$baselinePath = Join-Path $repoRoot '.github/ci/release-baseline.json'
. (Join-Path $repoRoot 'tools/ci-common.ps1')
. (Join-Path $repoRoot 'tools/ci-protocol.ps1')
function Must-Reject {
    param([scriptblock]$Action, [string]$Reason)
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Accepted invalid evidence: $Reason" }
}
function Copy-Value { param($Value) return $Value | ConvertTo-Json -Depth 30 | ConvertFrom-Json }

$cmake = [IO.File]::ReadAllText((Join-Path $repoRoot 'CMakeLists.txt'))
$options = @([regex]::Matches($cmake, 'option\((SIMPILOT_MODULE_[A-Z_]+)\s') | ForEach-Object { $_.Groups[1].Value } | Sort-Object)
$variants = @(Get-CiVariants $options)
Assert-Equal $variants.Count 9 'Current nine configurations'
Assert-Equal @($variants | Where-Object { $_.disabled.Count -eq 1 }).Count 7 'Single-module omissions'
Assert-Equal $variants[-1].disabled.Count 7 'Source removal options'
Assert-Equal $variants[-1].withoutSources $true 'Source removal snapshot'
$plan = [pscustomobject]@{ protocol = $script:CiProtocol; fingerprint = Get-CiFingerprint
    sha = ('a' * 40); repository = 'fixture/Simpilot'; runId = 123; version = Get-ProjectVersion
    options = $options; variants = $variants; mode = 'build' }
Assert-CiPlan $plan
$records = @(foreach ($variant in $variants) {
    [pscustomobject]@{ protocol = $plan.protocol; fingerprint = $plan.fingerprint; sha = $plan.sha
        version = $plan.version; repository = $plan.repository; runId = 123; attempt = 1
        variant = $variant.id; disabled = $variant.disabled; withoutSources = $variant.withoutSources
        status = 'success'; configuration = 'Release'; ipo = $true; installVerified = $true
        expectedTests = @('native-ui', 'keyboard', 'policy'); passedTests = @('native-ui', 'keyboard', 'policy') }
})
Assert-Equal @(Assert-CiRecords $plan $records 1).Count 9 'Complete success'
foreach ($status in @('failure', 'cancelled', 'skipped')) {
    $bad = Copy-Value $records
    $bad[3].status = $status
    Must-Reject { Assert-CiRecords $plan $bad 1 } "variant $status"
    Must-Reject { Assert-CiRecords $plan $records 1 $status } "matrix $status"
}
Must-Reject { Assert-CiRecords $plan $records[0..7] 1 } 'missing variant'
Must-Reject { Assert-CiRecords $plan ($records + $records[0]) 1 } 'duplicate record'
$bad = Copy-Value $records; $bad[0].passedTests = @('native-ui', 'policy')
Must-Reject { Assert-CiRecords $plan $bad 1 } 'missing keyboard test'
$bad = Copy-Value $records; $bad[0].expectedTests = @(); $bad[0].passedTests = @()
Must-Reject { Assert-CiRecords $plan $bad 1 } 'empty test suite'
foreach ($field in @('sha', 'fingerprint', 'repository', 'configuration', 'variant')) {
    $bad = Copy-Value $records; $bad[0].$field = 'wrong'
    Must-Reject { Assert-CiRecords $plan $bad 1 } "wrong $field"
}
foreach ($field in @('ipo', 'installVerified')) {
    $bad = Copy-Value $records; $bad[0].$field = $false
    Must-Reject { Assert-CiRecords $plan $bad 1 } "disabled $field"
}
$badPlan = Copy-Value $plan; $badPlan.variants = $badPlan.variants[0..7]
Must-Reject { Assert-CiPlan $badPlan } 'shortened plan'

# Failed-jobs reruns keep successful records from attempt 1 and replace only
# the failed variant. A later failure must never fall back to older success.
$history = Copy-Value $records
$history[4].status = 'failure'
$retry = Copy-Value $records[4]; $retry.attempt = 2
$selected = @(Assert-CiRecords $plan ($history + $retry) 2)
Assert-Equal ($selected | Where-Object variant -EQ $retry.variant).attempt 2 'Retried variant'
$jobs = @($selected | ForEach-Object { [pscustomobject]@{ name = "Build / $($_.variant)"; run_attempt = $_.attempt; conclusion = 'success' } })
Assert-CiJobs $selected $jobs
$jobs[0].conclusion = 'cancelled'
Must-Reject { Assert-CiJobs $selected $jobs } 'cancelled job without updated report'
$jobs[0].conclusion = 'success'; $jobs[0].run_attempt = 2
Must-Reject { Assert-CiJobs $selected $jobs } 'new job without report'
$retry.status = 'failure'
Must-Reject { Assert-CiRecords $plan ($records + $retry) 2 } 'new failure hiding behind old success'

$workflow = [IO.File]::ReadAllText((Join-Path $repoRoot '.github/workflows/ci.yml'))
Assert-WorkflowPolicy $workflow
foreach ($mutation in @(
    @('name: Windows x64 Release', 'name: Different check'),
    @('max-parallel: 9', 'max-parallel: 4'),
    @('fail-fast: false', 'fail-fast: true'),
    @('contents: read', 'contents: write'),
    @('    if: always()', '    if: success()'),
    @('persist-credentials: false', 'persist-credentials: true'),
    @('3d3c42e5aac5ba805825da76410c181273ba90b1', 'main'),
    @('timeout-minutes: 30', 'timeout-minutes: 10')
)) { Must-Reject { Assert-WorkflowPolicy $workflow.Replace($mutation[0], $mutation[1]) } $mutation[0] }
Must-Reject { Assert-WorkflowPolicy ($workflow + "`n      - uses: actions/cache@main`n") } 'unpinned shorthand action'

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('simpilot-protocol-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $package = Join-Path $temporary 'Simpilot-1.2.3-win-x64.zip'
    [IO.File]::WriteAllText($package, 'tampered candidate')
    Must-Reject { Assert-CiPackage $temporary '1.2.3' ('0' * 64) } 'actual candidate SHA-256 mismatch'
    foreach ($name in @('../escaped.txt', '.. /escaped.txt', '/absolute.txt', 'C:/drive.txt')) {
        $archive = Join-Path $temporary ([guid]::NewGuid().ToString('N') + '.zip')
        $zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
        $zip.CreateEntry($name) | Out-Null; $zip.Dispose()
        Must-Reject { Expand-CiArchive $archive (Join-Path $temporary 'extract') } 'archive traversal'
    }
    $archive = Join-Path $temporary 'duplicate.zip'
    $zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
    $zip.CreateEntry('duplicate.txt') | Out-Null
    $zip.CreateEntry('DUPLICATE.txt') | Out-Null
    $zip.Dispose()
    Must-Reject { Expand-CiArchive $archive (Join-Path $temporary 'duplicate') } 'case-insensitive duplicate archive paths'
    $savedRoot = $repoRoot; $repoRoot = $temporary
    Must-Reject { Assert-ReleaseNotes '1.2.3' } 'missing release notes'
    New-Item -ItemType Directory -Path (Join-Path $temporary 'docs/release-notes') -Force | Out-Null
    $notes = Join-Path $temporary 'docs/release-notes/v1.2.3.md'
    Set-Content -LiteralPath $notes -Value "# Simpilot v1.2.4`nWrong version"
    Must-Reject { Assert-ReleaseNotes '1.2.3' } 'wrong release notes version'
    Set-Content -LiteralPath $notes -Value "# Simpilot v1.2.3`nChanges"
    Assert-ReleaseNotes '1.2.3' | Out-Null
} finally {
    if (Get-Variable savedRoot -ErrorAction SilentlyContinue) { $repoRoot = $savedRoot }
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
Write-Host 'CI protocol: complete matrix, failures, reruns, workflow policy and archive checks passed.'
