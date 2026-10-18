[CmdletBinding()]
param(
    [Parameter(Mandatory)] [long[]]$RunId,
    [string]$Repository = $env:GITHUB_REPOSITORY,
    [string]$OutputPath = 'build/ci/performance.json'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$baselinePath = Join-Path $repoRoot '.github/ci/release-baseline.json'
. (Join-Path $PSScriptRoot 'ci-common.ps1')
. (Join-Path $PSScriptRoot 'ci-protocol.ps1')
. (Join-Path $PSScriptRoot 'ci-github.ps1')
if (-not $Repository) { throw 'Specify -Repository owner/name.' }
if ($RunId.Count -ne 3 -or @($RunId | Sort-Object -Unique).Count -ne 3) { throw 'Acceptance requires three distinct runs.' }
$previousRepository = $env:GITHUB_REPOSITORY
$env:GITHUB_REPOSITORY = $Repository
try {
    $measurements = @()
    $sha = $null
    $previousFinish = $null
    foreach ($id in ($RunId | Sort-Object)) {
        $run = Invoke-CiApi "/repos/$Repository/actions/runs/$id"
        Assert-Equal $run.conclusion 'success' 'Benchmark run'
        Assert-Equal $run.run_attempt 1 'Benchmark must be a fresh run'
        Assert-Equal $run.event 'workflow_dispatch' 'Benchmark trigger'
        Assert-Equal $run.path '.github/workflows/ci.yml' 'Benchmark workflow'
        if ($sha) { Assert-Equal $run.head_sha $sha 'Same benchmark commit' } else { $sha = $run.head_sha }
        $jobs = @(Get-CiRunJobs $id)
        $builds = @($jobs | Where-Object { $_.name.StartsWith('Build / ') })
        Assert-Equal $builds.Count 9 'Nine uncached build jobs'
        foreach ($job in $builds) { Assert-Equal $job.conclusion 'success' 'Benchmark build' }
        $aggregate = @($jobs | Where-Object name -EQ 'Windows x64 Release')
        Assert-Equal $aggregate.Count 1 'One final verification'
        Assert-Equal $aggregate[0].conclusion 'success' 'Final check'
        # Invoke-RestMethod can deserialize UTC strings as DateTime. Casting
        # preserves Kind; Parse(DateTime) first stringifies and loses the zone.
        $first = [DateTimeOffset]($jobs | Where-Object started_at | Sort-Object started_at | Select-Object -First 1).started_at
        $finish = [DateTimeOffset]$aggregate[0].completed_at
        if ($previousFinish -and [DateTimeOffset]$run.created_at -lt $previousFinish) { throw 'Benchmarks must run consecutively, not concurrently.' }
        $previousFinish = $finish
        $seconds = ($finish - $first).TotalSeconds
        $artifacts = @(Get-CiApiItems "/repos/$Repository/actions/runs/$id/artifacts" 'artifacts' |
            Where-Object { $_.name -eq "validated-$id-1" -and -not $_.expired })
        Assert-Equal $artifacts.Count 1 'Full validation artifact'
        $directory = Receive-CiArtifact $artifacts[0] (Join-Path $repoRoot "build/ci/benchmark-$id")
        $validation = Get-Content -LiteralPath (Join-Path $directory 'validation.json') -Raw | ConvertFrom-Json
        Assert-Equal $validation.runId $id 'Original full run'
        Assert-Equal $validation.sha $sha 'Artifact commit'
        Assert-Equal $validation.fingerprint (Get-CiFingerprint) 'Benchmarked implementation'
        Assert-Equal $validation.forceRebuild $true 'Forced full rebuild'
        $measurements += [pscustomobject]@{
            runId = $id; sha = $sha; startedAt = $first.ToString('o'); completedAt = $finish.ToString('o')
            initialQueueSeconds = ($first - [DateTimeOffset]$run.created_at).TotalSeconds
            completeCiSeconds = $seconds; targetMet = ($seconds -le 600)
            prepareSeconds = $validation.prepareSeconds
            variants = @($validation.records | Select-Object variant, timings, @{ n = 'tests'; e = { $_.passedTests.Count } })
            jobs = @($jobs | Select-Object name, started_at, completed_at, conclusion)
        }
    }
    $between = @(Get-CiApiItems "/repos/$Repository/actions/workflows/ci.yml/runs?head_sha=$sha&event=workflow_dispatch" 'workflow_runs' |
        Where-Object { $_.id -ge ($RunId | Measure-Object -Minimum).Minimum -and $_.id -le ($RunId | Measure-Object -Maximum).Maximum })
    Assert-Equal $between.Count 3 'Consecutive runs without cherry-picking failures'
    Write-CiJson $measurements ([IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath)))
    $measurements | Select-Object runId, sha, initialQueueSeconds, completeCiSeconds, targetMet | Format-Table
    if (@($measurements | Where-Object { -not $_.targetMet }).Count) { throw 'Performance acceptance failed: at least one complete run exceeded 600 seconds.' }
} finally { $env:GITHUB_REPOSITORY = $previousRepository }
