[CmdletBinding()]
param(
    [ValidateSet('All', 'Prepare', 'Build', 'Collect', 'Publish')] [string]$Stage = 'All',
    [string]$Variant,
    [switch]$ForceRebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$baselinePath = Join-Path $repoRoot '.github/ci/release-baseline.json'
. (Join-Path $PSScriptRoot 'ci-common.ps1')
. (Join-Path $PSScriptRoot 'ci-protocol.ps1')
. (Join-Path $PSScriptRoot 'ci-github.ps1')
$ciDirectory = Join-Path $repoRoot 'build/ci'
$attempt = if ($env:GITHUB_RUN_ATTEMPT) { [int]$env:GITHUB_RUN_ATTEMPT } else { 1 }
function Set-CiOutput {
    param([string]$Name, $Value)
    if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "$Name=$Value" -Encoding utf8 }
}
function Get-CheckedOutSha {
    $sha = & git -c "safe.directory=$($repoRoot.Replace('\', '/'))" -C $repoRoot rev-parse HEAD
    if ($LASTEXITCODE) { throw 'Cannot resolve checked-out commit.' }
    return [string]$sha
}

if ($Stage -in @('All', 'Prepare')) {
    $started = [DateTimeOffset]::UtcNow
    $version = Get-ProjectVersion
    Assert-WorkflowPolicy
    Assert-VendorSourcePolicy
    # Public checks run once, before matrix construction; local-only files are not required.
    $documentationTimer = [Diagnostics.Stopwatch]::StartNew()
    $syncMode = if ($env:GITHUB_RUN_ID) { 'Commit' } else { 'Worktree' }
    & (Join-Path $PSScriptRoot 'repository-sync.ps1') -Mode $syncMode -Root $repoRoot
    & (Join-Path $PSScriptRoot 'check-project-docs.ps1') -Scope Public -Root $repoRoot -Report (Join-Path $ciDirectory 'documentation.json')
    & (Join-Path $repoRoot 'tests/project_documentation_tests.ps1')
    $documentationSeconds = [math]::Round($documentationTimer.Elapsed.TotalSeconds, 3)
    Assert-TagVersion $version
    $sha = Get-CheckedOutSha
    if ($env:GITHUB_REF_TYPE -eq 'tag') {
        Assert-ReleaseNotes $version | Out-Null
        Assert-CiRemoteTag $env:GITHUB_REF_NAME $sha $env:GITHUB_REPOSITORY
        Assert-CiMainHistory $sha $env:GITHUB_REPOSITORY
    }
    $preflight = Join-Path $repoRoot 'build/ci-preflight'
    Invoke-NativeCommand 'cmake' @('--fresh', '-S', $repoRoot, '-B', $preflight,
        '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DBUILD_TESTING=ON', '-DCMAKE_CONFIGURATION_TYPES=Debug;Release')
    Assert-ReleaseConfiguration $preflight
    $options = @(Get-CiModuleOptions (Join-Path $preflight 'CMakeCache.txt'))
    $plan = [pscustomobject]@{
        protocol = $script:CiProtocol; fingerprint = Get-CiFingerprint
        repository = $(if ($env:GITHUB_REPOSITORY) { $env:GITHUB_REPOSITORY } else { 'local' })
        sha = $sha; version = $version; options = $options; variants = @(Get-CiVariants $options)
        runId = $(if ($env:GITHUB_RUN_ID) { [long]$env:GITHUB_RUN_ID } else { 0 })
        mode = 'build'; reuse = $null; startedAt = $started.ToString('o'); prepareSeconds = 0
        documentationSeconds = $documentationSeconds
        forceRebuild = [bool]($ForceRebuild -or $env:CI_FORCE_REBUILD -eq 'true')
    }
    $mayReuse = $env:GITHUB_REF_TYPE -eq 'tag' -or $env:GITHUB_EVENT_NAME -eq 'workflow_dispatch'
    if ($Stage -ne 'All' -and $mayReuse -and -not $ForceRebuild -and $env:CI_FORCE_REBUILD -ne 'true') {
        $plan.reuse = Resolve-CiReuse $plan
        if ($plan.reuse) { $plan.mode = 'reuse' }
    }
    $plan.prepareSeconds = [math]::Round(([DateTimeOffset]::UtcNow - $started).TotalSeconds, 3)
    Write-CiJson $plan (Join-Path $ciDirectory 'plan.json')
    Set-CiOutput 'plan' ($plan | ConvertTo-Json -Depth 20 -Compress)
    Set-CiOutput 'matrix' (@{ include = @($plan.variants | ForEach-Object { @{ id = $_.id } }) } | ConvertTo-Json -Depth 5 -Compress)
    Set-CiOutput 'sha' $sha
    Set-CiOutput 'mode' $plan.mode
    if ($plan.reuse) {
        Set-CiOutput 'source_run' $plan.reuse.runId
        Set-CiOutput 'source_artifact' $plan.reuse.artifactName
    }
    if ($Stage -eq 'Prepare') { return }
} else {
    if ($env:CI_PLAN) { $plan = $env:CI_PLAN | ConvertFrom-Json }
    else { $plan = Get-Content -LiteralPath (Join-Path $ciDirectory 'plan.json') -Raw | ConvertFrom-Json }
}
Assert-CiPlan $plan
Assert-Equal (Get-CheckedOutSha) $plan.sha 'Checked-out source'

if ($Stage -in @('All', 'Build')) {
    Assert-Equal $plan.mode 'build' 'Build mode'
    $variants = if ($Stage -eq 'All') { @($plan.variants.id) } else { @($Variant) }
    foreach ($id in $variants) {
        & (Join-Path $PSScriptRoot 'test-module-builds.ps1') -Plan $plan -Variant $id -OutputDirectory (Join-Path $ciDirectory 'results')
    }
    if ($Stage -eq 'Build') { return }
}

if ($Stage -in @('All', 'Collect')) {
    if ($env:GITHUB_RUN_ID) { Assert-Equal $env:CI_PREPARE_RESULT 'success' 'Preparation result' }
    if ($plan.mode -eq 'reuse') {
        Assert-Equal $env:CI_BUILD_RESULT 'skipped' 'Explicit reuse build skip'
        $sourceRun = Invoke-CiApi "/repos/$($plan.repository)/actions/runs/$($plan.reuse.runId)"
        Assert-CiTrustedRun $sourceRun $plan
        Assert-Equal $sourceRun.run_attempt $plan.reuse.attempt 'Source run attempt'
        $artifact = Get-CiReusableArtifact $sourceRun $plan
        if (-not $artifact) { throw 'Reusable artifact disappeared after preparation.' }
        Assert-Equal $artifact.id $plan.reuse.artifactId 'Source artifact identity'
        $directory = Join-Path $ciDirectory 'reused'
        $validation = Get-Content -LiteralPath (Join-Path $directory 'validation.json') -Raw | ConvertFrom-Json
        $records = @(Assert-CiValidation $validation $plan $directory $sourceRun.id $sourceRun.run_attempt)
        Assert-CiJobs $records @(Get-CiRunJobs $sourceRun.id)
        Set-CiOutput 'source_run' $sourceRun.id
        Set-CiOutput 'source_attempt' $sourceRun.run_attempt
        Set-CiOutput 'source_artifact' $artifact.name
        $summary = "Reused complete main validation $($sourceRun.id), attempt $($sourceRun.run_attempt), commit $($plan.sha)."
    } else {
        $incoming = if ($env:GITHUB_RUN_ID) { Join-Path $ciDirectory 'incoming' } else { Join-Path $ciDirectory 'results' }
        $paths = @(Get-ChildItem -LiteralPath $incoming -Filter result.json -File -Recurse)
        $reports = @($paths | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json })
        $matrixResult = if ($env:GITHUB_RUN_ID) { $env:CI_BUILD_RESULT } else { 'success' }
        $records = @(Assert-CiRecords $plan $reports $attempt $matrixResult)
        $jobs = @()
        $queue = 0
        $firstStart = [DateTimeOffset]$plan.startedAt
        if ($env:GITHUB_RUN_ID) {
            $jobs = @(Get-CiRunJobs $plan.runId)
            Assert-CiJobs $records $jobs
            $run = Invoke-CiApi "/repos/$($plan.repository)/actions/runs/$($plan.runId)"
            $firstStart = [DateTimeOffset]($jobs | Where-Object started_at | Sort-Object started_at | Select-Object -First 1).started_at
            $queue = [math]::Round(($firstStart - [DateTimeOffset]$run.created_at).TotalSeconds, 3)
        }
        $full = @($records | Where-Object variant -EQ 'full')[0]
        $fullPath = @($paths | Where-Object {
            $r = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
            $r.variant -eq 'full' -and $r.attempt -eq $full.attempt
        })[0].DirectoryName
        $package = Assert-CiPackage $fullPath $plan.version $full.package.sha256
        Assert-Equal $package.checksumSha256 $full.package.checksumSha256 'Candidate checksum digest'
        $verified = Join-Path $ciDirectory 'validated'
        New-Item -ItemType Directory -Path $verified -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $fullPath $package.name), (Join-Path $fullPath ($package.name + '.sha256')) -Destination $verified -Force
        $elapsed = [math]::Round(([DateTimeOffset]::UtcNow - $firstStart).TotalSeconds, 3)
        $validation = [ordered]@{
            protocol = $plan.protocol; fingerprint = $plan.fingerprint; repository = $plan.repository
            sha = $plan.sha; version = $plan.version; runId = $plan.runId; attempt = $attempt
            status = 'success'; options = $plan.options; records = $records; package = $package
            forceRebuild = $plan.forceRebuild
            verifiedAt = [DateTimeOffset]::UtcNow.ToString('o'); prepareSeconds = $plan.prepareSeconds
            documentationSeconds = $plan.documentationSeconds
            initialQueueSeconds = $queue; verificationSeconds = $elapsed; performanceTargetSeconds = 600
            # The final benchmark uses aggregate job completion (including upload).
            performanceTargetMetSoFar = ($elapsed -le 600)
        }
        Write-CiJson $validation (Join-Path $verified 'validation.json')
        Set-CiOutput 'source_run' $plan.runId
        Set-CiOutput 'source_attempt' $attempt
        Set-CiOutput 'source_artifact' "validated-$($plan.runId)-$attempt"
        $testCount = ($records | ForEach-Object { $_.passedTests.Count } | Measure-Object -Sum).Sum
        $summary = "Validated $($records.Count) configurations / $testCount tests in ${elapsed}s; initial queue ${queue}s. Commit $($plan.sha)."
        if ($elapsed -gt 600) { Write-Warning 'Performance target missed; successful verification remains valid. Further optimization is required.' }
    }
    Write-Host $summary
    if ($env:GITHUB_STEP_SUMMARY) { Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $summary }
    if ($Stage -eq 'Collect' -or $Stage -eq 'All') { return }
}

if ($Stage -eq 'Publish') {
    Assert-Equal $env:GITHUB_EVENT_NAME 'push' 'Publication event'
    Assert-Equal $env:GITHUB_REF_TYPE 'tag' 'Publication ref type'
    Assert-Equal $env:GITHUB_REF_NAME "v$($plan.version)" 'Publication tag'
    $sourceRun = Invoke-CiApi "/repos/$($plan.repository)/actions/runs/$($env:CI_SOURCE_RUN)"
    $sourceAttempt = [int]$env:CI_SOURCE_ATTEMPT
    $jobs = @(Get-CiRunJobs $sourceRun.id)
    Assert-CiPublicationSource $sourceRun $plan $sourceAttempt $jobs
    $directory = Join-Path $ciDirectory 'publish'
    $validation = Get-Content -LiteralPath (Join-Path $directory 'validation.json') -Raw | ConvertFrom-Json
    $records = @(Assert-CiValidation $validation $plan $directory $sourceRun.id $sourceAttempt)
    Assert-CiJobs $records $jobs
    Publish-CiRelease $plan $directory
}
