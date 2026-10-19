Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $repoRoot 'tools/ci-common.ps1')
. (Join-Path $repoRoot 'tools/ci-protocol.ps1')
. (Join-Path $repoRoot 'tools/ci-github.ps1')
function Must-Reject {
    param([scriptblock]$Action, [string]$Reason)
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Expected rejection: $Reason" }
}
function Copy-Value { param($Value) return $Value | ConvertTo-Json -Depth 30 | ConvertFrom-Json }

$plan = [pscustomobject]@{ repository = 'fixture/Simpilot'; sha = ('a' * 40); runId = 100
    version = '2.0.0'; protocol = 'simpilot-ci-v1'; fingerprint = ('b' * 64) }
$run = [pscustomobject]@{ id = 90; run_attempt = 1; repository = @{ full_name = $plan.repository }
    head_repository = @{ full_name = $plan.repository }; head_sha = $plan.sha
    path = '.github/workflows/ci.yml'; head_branch = 'main'; event = 'push'; status = 'completed'; conclusion = 'success' }
$artifact = [pscustomobject]@{ id = 9; name = 'validated-90-1'; expired = $false }
$script:runs = @($run); $script:artifacts = @($artifact); $script:polls = 0
$script:polledRun = $run
$finalJobs = @([pscustomobject]@{ name = 'Windows x64 Release'; run_attempt = 1; conclusion = 'success' })
Assert-CiPublicationSource $run $plan 1 $finalJobs
$rerun = Copy-Value $run; $rerun.id = $plan.runId; $rerun.run_attempt = 2
$rerun.status = 'in_progress'; $rerun.conclusion = $null; $rerun.head_branch = 'v2.0.0'
Assert-CiPublicationSource $rerun $plan 1 $finalJobs
Must-Reject { Assert-CiPublicationSource $rerun $plan 3 $finalJobs } 'future artifact attempt'
Must-Reject { Assert-CiPublicationSource $rerun $plan 1 @() } 'missing aggregate on publication retry'
$newAggregate = Copy-Value $finalJobs[0]; $newAggregate.run_attempt = 2; $newAggregate.conclusion = 'failure'
Must-Reject { Assert-CiPublicationSource $rerun $plan 1 ($finalJobs + $newAggregate) } 'new failed aggregate blocks stale publication'
$newAggregate.conclusion = 'success'
Must-Reject { Assert-CiPublicationSource $rerun $plan 1 ($finalJobs + $newAggregate) } 'new aggregate requires its own artifact'
Assert-CiPublicationSource $rerun $plan 2 ($finalJobs + $newAggregate)
$externalRetry = Copy-Value $run; $externalRetry.run_attempt = 2
Must-Reject { Assert-CiPublicationSource $externalRetry $plan 1 $finalJobs } 'external main attempt changed'
$retainedFinal = [pscustomobject]@{
    name = 'Windows x64 Release'; run_attempt = 1; conclusion = 'success'; status = 'completed'
    run_id = $rerun.id; head_sha = $plan.sha; runner_id = 42
    created_at = '2026-10-19T00:00:00Z'; started_at = '2026-10-19T00:00:02Z'; completed_at = '2026-10-19T00:00:20Z'
    steps = @([pscustomobject]@{ name = 'Verify complete evidence and package'; number = 1; status = 'completed'
        conclusion = 'success'; started_at = '2026-10-19T00:00:03Z'; completed_at = '2026-10-19T00:00:19Z' })
}
$retainedCopy = Copy-Value $retainedFinal; $retainedCopy.run_attempt = 2; $retainedCopy.created_at = '2026-10-19T00:05:00Z'
Assert-CiPublicationSource $rerun $plan 1 @($retainedFinal, $retainedCopy)
$retainedCopy.steps[0].conclusion = 'failure'
Must-Reject { Assert-CiPublicationSource $rerun $plan 1 @($retainedFinal, $retainedCopy) } 'modified retained aggregate'
$script:validationChecks = 0; $script:digestBad = $false
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('simpilot-github-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
$originalRepository = $env:GITHUB_REPOSITORY
$originalEvent = $env:GITHUB_EVENT_NAME
$env:GITHUB_REPOSITORY = $plan.repository
$env:GITHUB_EVENT_NAME = 'push'
try {
    # Exercise the real transport wrapper while mocking HTTP itself. These two
    # GitHub download endpoints require different Accept media types.
    function Invoke-WebRequest {
        param($Uri, $Method, $Headers, $ErrorAction, $OutFile)
        $script:lastAccept = $Headers.Accept
    }
    $savedToken = $env:GH_TOKEN
    try {
        $env:GH_TOKEN = 'offline-fixture'
        Invoke-CiApi '/repos/fixture/Simpilot/actions/artifacts/9/zip' -OutFile (Join-Path $temporary 'transport.zip')
        Assert-Equal $script:lastAccept 'application/vnd.github+json' 'Artifact ZIP media type'
        Invoke-CiApi '/repos/fixture/Simpilot/releases/assets/9' -OutFile (Join-Path $temporary 'asset.zip')
        Assert-Equal $script:lastAccept 'application/octet-stream' 'Release asset media type'
    } finally { $env:GH_TOKEN = $savedToken }
    # API is mocked at its only transport boundary. No test can publish a real
    # release or even make a network call, including with an inherited token.
    function Invoke-CiApi {
        param($Path, $Method = 'GET', $Body, [switch]$AllowNotFound, $OutFile, $InFile, [switch]$Upload)
        if ($Path -like '*/actions/workflows/ci.yml/runs?*') { return @{ workflow_runs = $script:runs } }
        if ($Path -like '*/actions/runs/90/artifacts?*') { return @{ artifacts = $script:artifacts } }
        if ($Path -like '*/actions/runs/95/artifacts?*') { return @{ artifacts = @() } }
        if ($Path -eq '/repos/fixture/Simpilot/actions/runs/90') { return $script:polledRun }
        throw "Unexpected offline API call: $Method $Path"
    }
    function Wait-CiReusePoll { $script:polls++ }
    function Receive-CiArtifact { param($Artifact, $Destination) return $temporary }
    function Get-CiRunJobs { param($RunId) return @() }
    function Assert-CiJobs { param($Records, $Jobs) }
    function Assert-CiValidation {
        param($Validation, $Plan, $Directory, $SourceRun, $SourceAttempt)
        $script:validationChecks++
        if ($script:digestBad) { throw 'Digest mismatch.' }
        Assert-Equal $Validation.sha $Plan.sha 'Mock validation commit'
        return @()
    }
    $validation = @{ protocol = $plan.protocol; fingerprint = $plan.fingerprint; sha = $plan.sha }
    Write-CiJson $validation (Join-Path $temporary 'validation.json')
    Assert-Equal (Resolve-CiReuse $plan).runId 90 'Successful reuse'
    Assert-Equal $script:validationChecks 1 'Reuse validates contents'
    $reusedManual = Copy-Value $run; $reusedManual.id = 95; $reusedManual.event = 'workflow_dispatch'
    $script:runs = @($reusedManual, $run)
    Assert-Equal (Resolve-CiReuse $plan).runId 90 'Manual reuse does not hide original main artifact'
    $reusedManual.conclusion = 'failure'
    Must-Reject { Resolve-CiReuse $plan } 'new failed main run cannot fall back to older success'
    $reusedManual.conclusion = 'success'
    $oldFailure = Copy-Value $run; $oldFailure.conclusion = 'failure'
    $script:runs = @($reusedManual, $oldFailure)
    Assert-Equal (Resolve-CiReuse $plan) $null 'Historical failure does not block rebuilding missing artifacts'
    $script:runs = @(); Assert-Equal (Resolve-CiReuse $plan) $null 'No previous run'
    foreach ($field in @('head_sha', 'path', 'head_branch', 'event')) {
        $bad = Copy-Value $run; $bad.$field = 'wrong'; $script:runs = @($bad)
        Assert-Equal (Resolve-CiReuse $plan) $null "Wrong $field cannot be reused"
    }
    $script:runs = @($run); $artifact.expired = $true
    Assert-Equal (Resolve-CiReuse $plan) $null 'Expired artifact triggers rebuild'
    $artifact.expired = $false
    $validation.protocol = 'old'; Write-CiJson $validation (Join-Path $temporary 'validation.json')
    Assert-Equal (Resolve-CiReuse $plan) $null 'Old protocol triggers rebuild'
    $validation.protocol = $plan.protocol; $validation.fingerprint = 'old'
    Write-CiJson $validation (Join-Path $temporary 'validation.json')
    Assert-Equal (Resolve-CiReuse $plan) $null 'Old workflow triggers rebuild'
    $validation.fingerprint = $plan.fingerprint; Write-CiJson $validation (Join-Path $temporary 'validation.json')
    $script:digestBad = $true; Must-Reject { Resolve-CiReuse $plan } 'digest mismatch'; $script:digestBad = $false
    $failed = Copy-Value $run; $failed.conclusion = 'failure'; $script:runs = @($failed)
    Must-Reject { Resolve-CiReuse $plan } 'failed main validation blocks release'
    $active = Copy-Value $run; $active.status = 'in_progress'; $active.conclusion = $null
    $script:runs = @($active)
    Assert-Equal (Resolve-CiReuse $plan).runId 90 'Wait for active validation'
    Assert-Equal $script:polls 1 'Poll once then reuse'
    $script:polledRun = $active
    Must-Reject { Resolve-CiReuse $plan -MaximumPolls 2 } 'wait timeout'

    # Publication test state: drafts and public assets are represented by files.
    $savedRoot = $repoRoot; $repoRoot = $temporary
    New-Item -ItemType Directory -Path (Join-Path $temporary 'docs/release-notes') -Force | Out-Null
    $notesPath = Join-Path $temporary 'docs/release-notes/v2.0.0.md'
    Set-Content -LiteralPath $notesPath -Value "# Simpilot v2.0.0`nChanges"
    $zip = Join-Path $temporary 'Simpilot-2.0.0-win-x64.zip'
    Set-Content -LiteralPath $zip -Value 'fixture ZIP bytes'
    Set-Content -LiteralPath "$zip.sha256" -Value 'fixture checksum bytes'
    $script:release = $null; $script:assets = @(); $script:uploads = 0; $script:patches = 0
    $script:tagSha = $plan.sha; $script:latestTag = 'v1.9.0'; $script:latestSetting = ''; $script:downloads = 0
    $script:mainStatus = 'ahead'; $script:annotated = $false
    function Invoke-CiApi {
        param($Path, $Method = 'GET', $Body, [switch]$AllowNotFound, $OutFile, $InFile, [switch]$Upload)
        if ($Method -eq 'GET' -and $Path -eq '/repos/fixture/Simpilot/git/ref/tags/v2.0.0') {
            if ($script:annotated) { return @{ object = @{ type = 'tag'; sha = 'tag-object' } } }
            return @{ object = @{ type = 'commit'; sha = $script:tagSha } }
        }
        if ($Path -eq '/repos/fixture/Simpilot/git/tags/tag-object') { return @{ object = @{ type = 'commit'; sha = $script:tagSha } } }
        if ($Path -like '*/compare/*...main') { return @{ status = $script:mainStatus; merge_base_commit = @{ sha = $plan.sha } } }
        if ($Method -eq 'GET' -and $Path -like '*/releases?per_page*') { if ($script:release) { return @($script:release) }; return @() }
        if ($Method -eq 'GET' -and $Path -like '*/releases/7/assets?*') { return $script:assets }
        if ($Method -eq 'POST' -and $Path -eq '/repos/fixture/Simpilot/releases') {
            Assert-Equal $Body.draft $true 'Created as draft'
            Assert-Equal $Body.tag_name 'v2.0.0' 'Existing tag'
            $script:release = [pscustomobject]@{ id = 7; tag_name = 'v2.0.0'; draft = $true; prerelease = $false }
            return $script:release
        }
        if ($Method -eq 'POST' -and $Path -like '*/releases/7/assets?name=*') {
            Assert-Equal $Upload $true 'Upload host'
            $script:uploads++
            $asset = [pscustomobject]@{ id = $script:uploads; name = (Split-Path -Leaf $InFile); bytes = [IO.File]::ReadAllBytes($InFile) }
            $script:assets += $asset
            return $asset
        }
        if ($OutFile -and $Path -match '/releases/assets/(\d+)$') {
            $asset = @($script:assets | Where-Object id -EQ ([int]$Matches[1]))[0]
            [IO.File]::WriteAllBytes($OutFile, $asset.bytes); $script:downloads++; return
        }
        if ($Path -like '*/releases/latest') { return @{ tag_name = $script:latestTag } }
        if ($Method -eq 'PATCH' -and $Path -eq '/repos/fixture/Simpilot/releases/7') {
            Assert-Equal $script:downloads 2 'Both assets downloaded before publication'
            $script:patches++; $script:latestSetting = $Body.make_latest; $script:release.draft = $Body.draft
            return $script:release
        }
        throw "Unexpected offline publication API: $Method $Path"
    }
    $script:tagSha = 'wrong'; Must-Reject { Publish-CiRelease $plan $temporary } 'tag points elsewhere'
    Assert-Equal $script:uploads 0 'Tag rejection before mutation'; $script:tagSha = $plan.sha
    $script:mainStatus = 'diverged'; Must-Reject { Publish-CiRelease $plan $temporary } 'commit not in main history'
    Assert-Equal $script:uploads 0 'Main ancestry rejection before mutation'; $script:mainStatus = 'ahead'
    $script:annotated = $true
    Remove-Item -LiteralPath $notesPath
    Must-Reject { Publish-CiRelease $plan $temporary } 'missing notes before draft'
    Set-Content -LiteralPath $notesPath -Value "# Simpilot v2.0.0`nChanges"
    Publish-CiRelease $plan $temporary
    Assert-Equal $script:uploads 2 'Two uploaded assets'
    Assert-Equal $script:patches 1 'Published once'
    Assert-Equal $script:latestSetting 'true' 'New version becomes Latest'
    Publish-CiRelease $plan $temporary
    Assert-Equal $script:uploads 2 'Idempotent publication does not upload'
    Assert-Equal $script:patches 1 'Idempotent publication does not edit release'
    $script:assets[0].bytes = [Text.Encoding]::UTF8.GetBytes('conflicting remote asset')
    Must-Reject { Publish-CiRelease $plan $temporary } 'published attachment conflict'
    Assert-Equal $script:patches 1 'Conflict did not modify release'
    $script:release.draft = $true
    Must-Reject { Publish-CiRelease $plan $temporary } 'draft attachment conflict'
    Assert-Equal $script:patches 1 'Conflicting draft is not made public'
    $script:assets[0].bytes = [IO.File]::ReadAllBytes($zip)
    # Resume an interrupted draft with one attachment; only upload the missing file.
    $script:release.draft = $true; $script:assets = @($script:assets[0]); $script:downloads = 0
    $script:latestTag = 'v3.0.0'
    Publish-CiRelease $plan $temporary
    Assert-Equal $script:uploads 3 'Draft resumes missing upload'
    Assert-Equal $script:latestSetting 'false' 'Old draft does not change Latest'
} finally {
    $env:GITHUB_REPOSITORY = $originalRepository
    $env:GITHUB_EVENT_NAME = $originalEvent
    if (Get-Variable savedRoot -ErrorAction SilentlyContinue) { $repoRoot = $savedRoot }
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
Write-Host 'Offline GitHub tests: reuse, expiry, failure, timeout, draft resume, publication and conflicts passed.'
