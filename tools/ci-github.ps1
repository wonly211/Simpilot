# All network operations cross these small boundaries, replaced by offline tests.
function Invoke-CiApi {
    param([string]$Path, [string]$Method = 'GET', $Body, [switch]$AllowNotFound,
        [string]$OutFile, [string]$InFile, [switch]$Upload)
    if (-not $env:GH_TOKEN) { throw 'GH_TOKEN is required for GitHub operations.' }
    if ($Path -notmatch '^/repos/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/') { throw 'Invalid repository API path.' }
    $hostName = if ($Upload) { 'uploads.github.com' } else { 'api.github.com' }
    $headers = @{ Authorization = "Bearer $env:GH_TOKEN"; Accept = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28' }
    $parameters = @{ Uri = "https://$hostName$Path"; Method = $Method; Headers = $headers; ErrorAction = 'Stop' }
    if ($PSBoundParameters.ContainsKey('Body')) {
        $parameters.Body = $Body | ConvertTo-Json -Depth 20 -Compress
        $parameters.ContentType = 'application/json'
    }
    if ($InFile) { $parameters.InFile = $InFile; $parameters.ContentType = 'application/octet-stream' }
    try {
        if ($OutFile) {
            # Release assets negotiate binary data. The Actions artifact ZIP
            # endpoint requires the JSON media type before redirecting to ZIP.
            if ($Path -match '/releases/assets/[0-9]+$') { $headers.Accept = 'application/octet-stream' }
            # PowerShell drops Authorization on cross-host redirects. Do not
            # enable PreserveAuthorizationOnRedirect for signed blob downloads.
            Invoke-WebRequest @parameters -OutFile $OutFile | Out-Null
            return
        }
        return Invoke-RestMethod @parameters
    } catch {
        if ($AllowNotFound -and $_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq 404) { return $null }
        throw
    }
}

function Get-CiApiItems {
    param([string]$Path, [string]$Property)
    $separator = if ($Path.Contains('?')) { '&' } else { '?' }
    $items = @()
    for ($page = 1; ; $page++) {
        $response = Invoke-CiApi "${Path}${separator}per_page=100&page=$page"
        $batch = @(if ($Property) { $response.$Property } else { $response })
        $items += $batch
        if ($batch.Count -lt 100) { break }
    }
    return $items
}

function Get-CiRunJobs {
    param([long]$RunId)
    return @(Get-CiApiItems "/repos/$($env:GITHUB_REPOSITORY)/actions/runs/$RunId/jobs?filter=all" 'jobs')
}

function Assert-CiTrustedRun {
    param($Run, $Plan, [switch]$Current)
    Assert-Equal $Run.repository.full_name $Plan.repository 'Source repository'
    Assert-Equal $Run.head_repository.full_name $Plan.repository 'Source head repository'
    Assert-Equal $Run.head_sha $Plan.sha 'Source commit'
    Assert-Equal $Run.path '.github/workflows/ci.yml' 'Source workflow'
    if ($Current) {
        Assert-Equal $Run.id $Plan.runId 'Current run ID'
    } else {
        Assert-Equal $Run.head_branch 'main' 'Source branch'
        if ($Run.event -notin @('push', 'workflow_dispatch')) { throw 'PR artifacts cannot be reused.' }
        Assert-Equal $Run.status 'completed' 'Source run status'
        Assert-Equal $Run.conclusion 'success' 'Source run conclusion'
    }
}

function Get-CiReusableArtifact {
    param($Run, $Plan)
    Assert-CiTrustedRun $Run $Plan
    $name = "validated-$($Run.id)-$($Run.run_attempt)"
    $artifacts = @(Get-CiApiItems "/repos/$($Plan.repository)/actions/runs/$($Run.id)/artifacts" 'artifacts' |
        Where-Object { $_.name -eq $name -and -not $_.expired })
    if (-not $artifacts.Count) { return $null }
    if ($artifacts.Count -ne 1) { throw 'Ambiguous validated artifacts.' }
    return $artifacts[0]
}

function Assert-CiPublicationSource {
    param($Run, $Plan, [int]$SourceAttempt, [object[]]$Jobs)
    if ($Run.id -eq $Plan.runId) {
        Assert-CiTrustedRun $Run $Plan -Current
        # Re-running only a failed publication increments the run attempt but
        # intentionally retains the successful aggregate and its old artifact.
        if ($SourceAttempt -lt 1 -or $SourceAttempt -gt $Run.run_attempt) { throw 'Invalid publication source attempt.' }
    } else {
        Assert-CiTrustedRun $Run $Plan
        Assert-Equal $Run.run_attempt $SourceAttempt 'External source attempt'
    }
    Assert-CiJobEvidence $Jobs 'Windows x64 Release' $SourceAttempt
}

function Receive-CiArtifact {
    param($Artifact, [string]$Destination)
    # Keep repeated verification and measurement invocations independent.
    $Destination = Join-Path $Destination ([guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $archive = Join-Path $Destination 'artifact.zip'
    Invoke-CiApi "/repos/$($env:GITHUB_REPOSITORY)/actions/artifacts/$($Artifact.id)/zip" -OutFile $archive
    $contents = Join-Path $Destination 'contents'
    Expand-CiArchive $archive $contents
    return $contents
}

function Wait-CiReusePoll { Start-Sleep -Seconds 15 }

function Resolve-CiReuse {
    param($Plan, [int]$MaximumPolls = 60)
    $runs = @(Get-CiApiItems "/repos/$($Plan.repository)/actions/workflows/ci.yml/runs?head_sha=$($Plan.sha)&branch=main" 'workflow_runs' |
        Where-Object { $_.id -ne $Plan.runId -and $_.head_sha -eq $Plan.sha -and
            $_.head_branch -eq 'main' -and $_.event -in @('push', 'workflow_dispatch') -and
            $_.path -eq '.github/workflows/ci.yml' -and $_.repository.full_name -eq $Plan.repository -and
            $_.head_repository.full_name -eq $Plan.repository -and
            ($env:GITHUB_EVENT_NAME -ne 'workflow_dispatch' -or $_.id -lt $Plan.runId) } |
        Sort-Object id -Descending)
    if (-not $runs.Count) { return $null }
    $deadline = [DateTimeOffset]::UtcNow.AddMinutes(15)
    $firstCandidate = $true
    foreach ($candidate in $runs) {
        # A newer successful validation supersedes historical failures. Only
        # the newest matching run controls waiting or failure blocking.
        if (-not $firstCandidate -and ($candidate.status -ne 'completed' -or $candidate.conclusion -ne 'success')) { continue }
        $firstCandidate = $false
        $run = $candidate
        for ($poll = 0; $run.status -ne 'completed'; $poll++) {
            if ($poll -ge $MaximumPolls -or [DateTimeOffset]::UtcNow.AddSeconds(15) -gt $deadline) { throw 'Matching main validation did not finish within 15 minutes.' }
            Write-Host "Waiting for main validation $($run.id) ($($run.status)); poll $($poll + 1)."
            Wait-CiReusePoll
            $run = Invoke-CiApi "/repos/$($Plan.repository)/actions/runs/$($run.id)"
        }
        Assert-CiTrustedRun $run $Plan
        $artifact = Get-CiReusableArtifact $run $Plan
        # A successful manual-main reuse has no new validated artifact. Keep
        # searching for the original full validation instead of rebuilding.
        if (-not $artifact) { continue }
        $directory = Receive-CiArtifact $artifact (Join-Path $repoRoot ('build/ci/reuse-' + [guid]::NewGuid().ToString('N')))
        $path = Join-Path $directory 'validation.json'
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $validation = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        if (-not $validation.PSObject.Properties['protocol'] -or $validation.protocol -ne $Plan.protocol -or
            -not $validation.PSObject.Properties['fingerprint'] -or $validation.fingerprint -ne $Plan.fingerprint) { continue }
        $records = @(Assert-CiValidation $validation $Plan $directory $run.id $run.run_attempt)
        Assert-CiJobs $records @(Get-CiRunJobs $run.id)
        return [pscustomobject]@{ runId = $run.id; attempt = $run.run_attempt; artifactId = $artifact.id; artifactName = $artifact.name }
    }
    return $null
}

function Assert-CiRemoteTag {
    param([string]$Tag, [string]$Sha, [string]$Repository)
    if ($Tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Tag must match vX.Y.Z.' }
    $ref = Invoke-CiApi "/repos/$Repository/git/ref/tags/$Tag"
    $object = $ref.object
    for ($depth = 0; $object.type -eq 'tag'; $depth++) {
        if ($depth -ge 8) { throw 'Too many nested annotated tags.' }
        $object = (Invoke-CiApi "/repos/$Repository/git/tags/$($object.sha)").object
    }
    Assert-Equal $object.type 'commit' 'Remote tag object'
    Assert-Equal $object.sha $Sha 'Remote tag commit'
}

function Assert-CiMainHistory {
    param([string]$Sha, [string]$Repository)
    $comparison = Invoke-CiApi "/repos/$Repository/compare/$Sha...main"
    if ($comparison.status -notin @('ahead', 'identical')) { throw 'Release commit is not in main history.' }
    Assert-Equal $comparison.merge_base_commit.sha $Sha 'Main ancestor'
}

function Assert-CiReleaseAssets {
    param($Release, [string[]]$Files, [string]$Repository, [switch]$AllowUpload)
    $assets = @(Get-CiApiItems "/repos/$Repository/releases/$($Release.id)/assets" '')
    $names = @($Files | ForEach-Object { Split-Path -Leaf $_ })
    if (@($assets | Where-Object name -NotIn $names).Count) { throw 'Release has unexpected attachments.' }
    foreach ($file in $Files) {
        $name = Split-Path -Leaf $file
        $matches = @($assets | Where-Object name -EQ $name)
        if ($matches.Count -gt 1) { throw 'Duplicate release attachment.' }
        if (-not $matches.Count) {
            if (-not $AllowUpload) { throw "Published release is missing $name; refusing to modify it." }
            $asset = Invoke-CiApi "/repos/$Repository/releases/$($Release.id)/assets?name=$([uri]::EscapeDataString($name))" -Method POST -InFile $file -Upload
        } else { $asset = $matches[0] }
        $download = Join-Path (Split-Path -Parent $file) ('asset-check-' + [guid]::NewGuid().ToString('N'))
        try {
            Invoke-CiApi "/repos/$Repository/releases/assets/$($asset.id)" -OutFile $download
            Assert-Equal (Get-FileHash -LiteralPath $download).Hash (Get-FileHash -LiteralPath $file).Hash "Release attachment $name"
        } finally {
            if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download -Force }
        }
    }
}

function Publish-CiRelease {
    param($Plan, [string]$Directory)
    $tag = 'v' + $Plan.version
    $notes = Assert-ReleaseNotes $Plan.version
    Assert-CiRemoteTag $tag $Plan.sha $Plan.repository
    Assert-CiMainHistory $Plan.sha $Plan.repository
    # Listing includes drafts accessible to the publishing token; a draft may
    # not be returned by the public get-by-tag endpoint.
    $releases = @(Get-CiApiItems "/repos/$($Plan.repository)/releases" '')
    $matches = @($releases | Where-Object tag_name -EQ $tag)
    if ($matches.Count -gt 1) { throw 'More than one release uses this tag.' }
    $files = @((Join-Path $Directory "Simpilot-$($Plan.version)-win-x64.zip"),
        (Join-Path $Directory "Simpilot-$($Plan.version)-win-x64.zip.sha256"))
    if ($matches.Count -and -not $matches[0].draft) {
        if ($matches[0].prerelease) { throw 'Existing release is a prerelease.' }
        Assert-CiReleaseAssets $matches[0] $files $Plan.repository
        Write-Host 'The public release already contains exactly the verified assets.'
        return
    }
    $release = if ($matches.Count) { $matches[0] } else {
        Invoke-CiApi "/repos/$($Plan.repository)/releases" -Method POST -Body @{
            tag_name = $tag; name = "Simpilot $tag"; body = $notes; draft = $true; prerelease = $false; make_latest = 'false'
        }
    }
    Assert-CiReleaseAssets $release $files $Plan.repository -AllowUpload
    # Do not promote an old version to Latest, including a resumed old draft.
    $latest = Invoke-CiApi "/repos/$($Plan.repository)/releases/latest" -AllowNotFound
    $makeLatest = 'false'
    if (-not $latest) { $makeLatest = 'true' }
    elseif ($latest.tag_name -match '^v(\d+\.\d+\.\d+)$' -and [version]$Plan.version -gt [version]$Matches[1]) { $makeLatest = 'true' }
    Assert-CiRemoteTag $tag $Plan.sha $Plan.repository
    $published = Invoke-CiApi "/repos/$($Plan.repository)/releases/$($release.id)" -Method PATCH -Body @{
        draft = $false; prerelease = $false; body = $notes; name = "Simpilot $tag"; make_latest = $makeLatest
    }
    Assert-Equal $published.draft $false 'Public release'
    Write-Host "Published $tag after downloading and verifying both attachments."
}
