# Protocol changes invalidate reuse of older validation artifacts.
$script:CiProtocol = 'simpilot-ci-v1'

function Write-CiJson {
    param($Value, [string]$Path)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-CiFingerprint {
    $files = @('.github/workflows/ci.yml', 'CMakeLists.txt', 'CMakePresets.json') +
        @(Get-ChildItem (Join-Path $repoRoot 'tools/ci*.ps1') | ForEach-Object { 'tools/' + $_.Name }) +
        @('tools/test-module-builds.ps1', 'tests/ci_protocol_tests.ps1', 'tests/ci_github_tests.ps1',
          'tools/repository-sync-policy.json', 'tools/repository-sync.ps1', 'tools/repository_sync.py',
          'tools/check-project-docs.ps1', 'tools/project_docs.py', 'tools/publish-wiki.ps1',
          'tests/project_documentation_tests.ps1', 'tests/project_documentation_tests.py')
    # Hash normalized source text so Git's Windows CRLF conversion is immaterial.
    $text = (@($files | Sort-Object | ForEach-Object {
        $_ + "`n" + [IO.File]::ReadAllText((Join-Path $repoRoot $_)).Replace("`r`n", "`n")
    }) -join "`n")
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text))).ToLowerInvariant()
}

function Get-CiVariants {
    param([string[]]$Options)
    if ($Options.Count -eq 0 -or @($Options | Sort-Object -Unique).Count -ne $Options.Count) {
        throw 'Module options must be nonempty and unique.'
    }
    $variants = @([pscustomobject]@{ id = 'full'; disabled = @(); withoutSources = $false })
    foreach ($option in ($Options | Sort-Object)) {
        if ($option -notmatch '^SIMPILOT_MODULE_[A-Z_]+$') { throw "Invalid module option: $option" }
        $variants += [pscustomobject]@{
            id = 'without-' + $option.Substring(16).ToLowerInvariant().Replace('_', '-')
            disabled = @($option); withoutSources = $false
        }
    }
    $variants += [pscustomobject]@{ id = 'without-all-module-sources'; disabled = @($Options); withoutSources = $true }
    return $variants
}

function Get-CiModuleOptions {
    param([string]$Cache)
    $options = @(foreach ($line in Get-Content -LiteralPath $Cache) {
        if ($line -match '^(SIMPILOT_MODULE_[A-Z_]+):BOOL=(.+)$') {
            Assert-Equal $Matches[2] 'ON' 'Baseline module switch'
            $Matches[1]
        }
    })
    if (-not $options.Count) { throw 'CMake did not expose any module switches.' }
    return @($options | Sort-Object)
}

function Assert-CiPlan {
    param($Plan)
    Assert-Equal $Plan.protocol $script:CiProtocol 'Validation protocol'
    Assert-Equal $Plan.fingerprint (Get-CiFingerprint) 'Workflow implementation'
    Assert-Equal $Plan.version (Get-ProjectVersion) 'Source version'
    if ($Plan.sha -notmatch '^[0-9a-f]{40}$') { throw 'Invalid source SHA.' }
    if ($Plan.mode -notin @('build', 'reuse')) { throw 'Invalid validation mode.' }
    $expected = @(Get-CiVariants @($Plan.options)) | ConvertTo-Json -Depth 5 -Compress
    Assert-Equal ($Plan.variants | ConvertTo-Json -Depth 5 -Compress) $expected 'Complete variant matrix'
}

function Assert-CiRecords {
    param($Plan, [object[]]$Records, [int]$Attempt, [string]$MatrixResult = 'success')
    Assert-Equal $MatrixResult 'success' 'Build matrix conclusion'
    $selected = @()
    foreach ($record in $Records) {
        foreach ($field in @('protocol', 'fingerprint', 'sha', 'version', 'repository', 'runId')) {
            Assert-Equal $record.$field $Plan.$field "Result $field"
        }
        if ($record.variant -notin @($Plan.variants.id)) { throw "Unexpected variant: $($record.variant)" }
        if ($record.attempt -lt 1 -or $record.attempt -gt $Attempt) { throw 'Invalid result attempt.' }
    }
    foreach ($variant in $Plan.variants) {
        $matches = @($Records | Where-Object variant -EQ $variant.id | Sort-Object attempt -Descending)
        if (-not $matches.Count) { throw "Missing result: $($variant.id)" }
        if (@($matches | Group-Object attempt | Where-Object Count -GT 1).Count) { throw 'Duplicate result for an attempt.' }
        $record = $matches[0]
        Assert-Equal $record.status 'success' "Variant $($variant.id)"
        Assert-Equal $record.configuration 'Release' 'Configuration'
        Assert-Equal $record.ipo $true 'IPO'
        Assert-Equal $record.installVerified $true 'Installed file manifest'
        Assert-Equal ($record.disabled -join ',') ($variant.disabled -join ',') 'Disabled modules'
        Assert-Equal $record.withoutSources $variant.withoutSources 'Source removal'
        $expected = @($record.expectedTests | Sort-Object)
        $passed = @($record.passedTests | Sort-Object)
        if (-not $expected.Count -or @($expected | Sort-Object -Unique).Count -ne $expected.Count -or
            $expected.Count -ne $passed.Count -or @(Compare-Object $expected $passed).Count) {
            throw "Incomplete test evidence: $($variant.id)"
        }
        $selected += $record
    }
    return $selected
}

function Assert-CiJobEvidence {
    param([object[]]$Jobs, [string]$Name, [int]$Attempt)
    $history = @($Jobs | Where-Object name -EQ $Name | Sort-Object run_attempt)
    $original = @($history | Where-Object run_attempt -EQ $Attempt)
    if ($original.Count -ne 1 -or @($history | Group-Object run_attempt | Where-Object Count -GT 1).Count) {
        throw "Missing or ambiguous GitHub job evidence: $Name, attempt $Attempt"
    }
    Assert-Equal $original[0].conclusion 'success' "Job $Name"
    foreach ($job in @($history | Where-Object run_attempt -GT $Attempt)) {
        Assert-Equal $job.conclusion 'success' "Latest job $Name"
        # Failed-jobs reruns copy retained successes into the new attempt with
        # new job IDs. Only identical execution evidence may keep an old report;
        # a newly executed job still requires its own result, even if successful.
        foreach ($field in @('run_id', 'head_sha', 'runner_id', 'started_at', 'completed_at')) {
            if (-not $original[0].PSObject.Properties[$field] -or -not $original[0].$field -or
                -not $job.PSObject.Properties[$field] -or -not $job.$field) {
                throw "Missing retained-job identity: $Name/$field"
            }
            if ($field -in @('started_at', 'completed_at')) {
                Assert-Equal ([DateTimeOffset]$job.$field) ([DateTimeOffset]$original[0].$field) "Retained job $Name/$field"
            } else {
                Assert-Equal $job.$field $original[0].$field "Retained job $Name/$field"
            }
        }
        Assert-Equal $job.status 'completed' 'Retained job status'
        Assert-Equal $original[0].status 'completed' 'Original job status'
        if (-not $job.created_at -or [DateTimeOffset]$job.created_at -lt [DateTimeOffset]$original[0].completed_at) {
            throw "Job was executed again without a matching report: $Name"
        }
        if (-not @($original[0].steps).Count -or -not @($job.steps).Count) {
            throw "Missing retained-job steps: $Name"
        }
        $stepEvidence = @(foreach ($execution in @($original[0], $job)) {
            @(foreach ($step in $execution.steps) {
                [ordered]@{ name = $step.name; number = $step.number; status = $step.status; conclusion = $step.conclusion
                    started_at = if ($step.started_at) { ([DateTimeOffset]$step.started_at).ToUniversalTime().ToString('o') } else { $null }
                    completed_at = if ($step.completed_at) { ([DateTimeOffset]$step.completed_at).ToUniversalTime().ToString('o') } else { $null } }
            }) | ConvertTo-Json -Depth 5 -Compress
        })
        Assert-Equal $stepEvidence[1] $stepEvidence[0] "Retained job steps: $Name"
    }
}

function Assert-CiJobs {
    param([object[]]$Records, [object[]]$Jobs)
    foreach ($record in $Records) {
        Assert-CiJobEvidence $Jobs "Build / $($record.variant)" $record.attempt
    }
}

function Expand-CiArchive {
    param([string]$Archive, [string]$Destination)
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $root = [IO.Path]::GetFullPath($Destination).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    try {
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\', '/')
            if ($name -match '(^/|:|//|[. ](/|$))' -or -not $names.Add($name)) { throw 'Unsafe or duplicate archive path.' }
            $target = [IO.Path]::GetFullPath((Join-Path $root $name))
            if (-not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive escapes its destination.' }
            if ($name.EndsWith('/')) { continue }
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
    } finally { $zip.Dispose() }
}

function Assert-CiPackage {
    param([string]$Directory, [string]$Version, [string]$ExpectedHash)
    $zip = Join-Path $Directory "Simpilot-$Version-win-x64.zip"
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    Assert-Equal $hash $ExpectedHash 'Candidate digest'
    # Use the same original whitelist, PE version, checksum and size checks on
    # the extracted executable; never execute a downloaded package.
    $scratch = Join-Path $Directory ('inspection-' + [guid]::NewGuid().ToString('N'))
    Expand-CiArchive $zip $scratch
    $release = Join-Path $scratch 'Release'
    New-Item -ItemType Directory -Path $release | Out-Null
    Copy-Item -LiteralPath (Join-Path $scratch 'Simpilot.exe') -Destination $release
    Copy-Item -LiteralPath $zip, "$zip.sha256" -Destination $scratch
    try {
        $result = Assert-ReleaseArtifacts $Version $scratch
        # Check bytes as well as lengths for the bundled source assets.
        $paths = @{ 'LICENSE' = 'LICENSE'; 'THIRD-PARTY-NOTICES.txt' = 'THIRD-PARTY-NOTICES.txt'
            'Everything/Everything.exe' = 'third_party/Everything/Everything.exe'
            'Everything/Everything.ini' = 'third_party/Everything/Everything.ini'
            'Everything/Everything.lng' = 'third_party/Everything/Everything.lng'
            'Everything/Everything64.dll' = 'third_party/Everything/Everything64.dll' }
        foreach ($name in $paths.Keys) {
            Assert-Equal (Get-FileHash (Join-Path $scratch $name)).Hash `
                (Get-FileHash (Join-Path $repoRoot $paths[$name])).Hash "Package member $name"
        }
        return [pscustomobject]@{ name = (Split-Path -Leaf $zip); sha256 = $hash.ToLowerInvariant()
            bytes = (Get-Item -LiteralPath $zip).Length
            checksumSha256 = (Get-FileHash -LiteralPath "$zip.sha256").Hash.ToLowerInvariant() }
    } finally { Remove-Item -LiteralPath $scratch -Recurse -Force }
}

function Assert-CiValidation {
    param($Validation, $Plan, [string]$Directory, [long]$SourceRun, [int]$SourceAttempt)
    foreach ($field in @('protocol', 'fingerprint', 'sha', 'version', 'repository')) {
        Assert-Equal $Validation.$field $Plan.$field "Validation $field"
    }
    Assert-Equal $Validation.runId $SourceRun 'Validated run'
    Assert-Equal $Validation.attempt $SourceAttempt 'Validated attempt'
    Assert-Equal $Validation.status 'success' 'Final validation status'
    Assert-Equal ($Validation.options -join ',') ($Plan.options -join ',') 'Validated module options'
    $sourcePlan = $Plan | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $sourcePlan.runId = $SourceRun
    $records = @(Assert-CiRecords $sourcePlan @($Validation.records) $SourceAttempt)
    $full = @($records | Where-Object variant -EQ 'full')[0]
    Assert-Equal $full.package.sha256 $Validation.package.sha256 'Full build and aggregate digest'
    Assert-Equal $full.package.checksumSha256 $Validation.package.checksumSha256 'Full build and aggregate checksum'
    $package = Assert-CiPackage $Directory $Plan.version $Validation.package.sha256
    Assert-Equal $package.checksumSha256 $Validation.package.checksumSha256 'Checksum file digest'
    Assert-Equal $package.bytes $Validation.package.bytes 'Package size'
    return $records
}

function Assert-ReleaseNotes {
    param([string]$Version)
    $path = Join-Path $repoRoot "docs/release-notes/v$Version.md"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release notes: $path" }
    $text = [IO.File]::ReadAllText($path)
    $heading = ($text -split '\r?\n')[0]
    if ($heading -notmatch ('^#\s+.*\bv' + [regex]::Escape($Version) + '\s*$')) { throw 'Release notes heading does not match the tag version.' }
    if (($text -split '\r?\n', 2).Count -lt 2 -or [string]::IsNullOrWhiteSpace(($text -split '\r?\n', 2)[1])) { throw 'Empty release notes.' }
    return $text
}

function Assert-WorkflowPolicy {
    param([string]$Text = '')
    $directory = Join-Path $repoRoot '.github/workflows'
    $files = @(Get-ChildItem -LiteralPath $directory -File | Where-Object Extension -In @('.yml', '.yaml'))
    if ($files.Count -ne 1 -or $files[0].Name -ne 'ci.yml') { throw 'CI must have exactly one workflow: ci.yml.' }
    if (-not $Text) { $Text = [IO.File]::ReadAllText((Join-Path $directory 'ci.yml')) }
    $lines = $Text -split '\r?\n'
    $uses = @($lines | Where-Object { $_ -match '^\s*(?:-\s+)?uses:' })
    if (-not $uses.Count) { throw 'Missing pinned actions.' }
    foreach ($line in $uses) {
        if ($line -notmatch '^\s*(?:-\s+)?uses:\s+actions/(checkout|upload-artifact|download-artifact)@[a-f0-9]{40}\s+#\s+v\S+\s*$') { throw "Unpinned or unapproved action: $line" }
    }
    # Check concrete policy text as well as counts to prevent weakening gates.
    $required = @(
        '  pull_request:', '  push:', '      - main', '      - "v*"', '  workflow_dispatch:',
        '      force_rebuild:', '        type: boolean', '        default: false',
        'permissions:', '  contents: read', '  actions: read',
        '    name: Windows x64 Release', '      max-parallel: 9', '      fail-fast: false',
        '  prepare:', '  build:', '  windows-release:', '  publish:',
        '    needs: [prepare, build]', '    if: always()',
        '    if: needs.prepare.outputs.mode == ''build''',
        '    if: ${{ !cancelled() && needs.prepare.result == ''success'' && needs.windows-release.result == ''success'' && github.event_name == ''push'' && github.ref_type == ''tag'' }}',
        '    needs: [prepare, windows-release]', '      contents: write',
        '      group: simpilot-release-publication', '      cancel-in-progress: false',
        '  cancel-in-progress: ${{ github.event_name == ''pull_request'' }}',
        '  group: ci-${{ github.event_name == ''pull_request'' && github.ref || (github.ref_type == ''tag'' && github.ref || github.sha) }}'
    )
    foreach ($line in $required) { if ($line -notin $lines) { throw "Missing workflow policy: $line" } }
    if (@($lines | Where-Object { $_ -match '^\s*runs-on:' }).Count -ne 4 -or
        @($lines | Where-Object { $_ -match '^\s*runs-on:' -and $_ -notmatch 'windows-2022$' }).Count) { throw 'All four stages require Windows 2022.' }
    if (@($lines | Where-Object { $_ -match '^\s*contents: write$' }).Count -ne 1 -or
        ($Text -split '(?m)^  publish:', 2)[0] -match '(contents|actions): write') { throw 'Only the publishing job may write repository contents.' }
    if ($Text -match '(?m)^\s*(permissions: write-all|continue-on-error:|actions: write|timeout-minutes: 10)') { throw 'Unsafe workflow policy.' }
    foreach ($stage in @('Prepare', 'Build', 'Collect', 'Publish')) {
        if ($Text -notmatch ('run: ./tools/ci.ps1 -Stage ' + $stage + '\b')) { throw "Missing pipeline stage: $stage" }
    }
    if (@($lines | Where-Object { $_ -match '^\s*run:' }).Count -ne 4) { throw 'All pipeline commands must use the four stage entry points.' }
    if (@($lines | Where-Object { $_ -match '^\s*persist-credentials: false$' }).Count -ne 4) { throw 'Every checkout must disable persisted credentials.' }
}
