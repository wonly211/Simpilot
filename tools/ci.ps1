[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDirectory = Join-Path $repoRoot "build\ci-vs2022-x64"
$baselinePath = Join-Path $repoRoot ".github\ci\release-baseline.json"

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$Arguments
    )

    Write-Host "> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Assert-Equal {
    param(
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] $Expected,
        [Parameter(Mandatory)] [string]$Name
    )

    if ($Actual -ne $Expected) {
        throw "$Name mismatch. Expected '$Expected', got '$Actual'."
    }
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory)] [string]$CachePath,
        [Parameter(Mandatory)] [string]$Name
    )

    $prefix = "${Name}:"
    $line = Get-Content -LiteralPath $CachePath -Encoding UTF8 |
        Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) } |
        Select-Object -First 1
    if (-not $line) {
        throw "CMake cache entry '$Name' is missing."
    }
    $separator = $line.IndexOf('=')
    if ($separator -lt 0) {
        throw "CMake cache entry '$Name' is malformed."
    }
    return $line.Substring($separator + 1)
}

function Get-ProjectVersion {
    $cmake = Get-Content -LiteralPath (Join-Path $repoRoot "CMakeLists.txt") -Raw -Encoding UTF8
    $match = [regex]::Match(
        $cmake,
        'project\s*\(\s*Simpilot\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
    if (-not $match.Success) {
        throw "Unable to read the Simpilot project version."
    }
    return $match.Groups[1].Value
}

function Assert-WorkflowPolicy {
    $workflowPath = Join-Path $repoRoot ".github\workflows\ci.yml"
    $workflowDirectory = Split-Path -Parent $workflowPath
    $workflowFiles = @(Get-ChildItem -LiteralPath $workflowDirectory -File |
        Where-Object { $_.Extension -in @(".yml", ".yaml") })
    $singleWorkflow = $workflowFiles.Count -eq 1 -and [string]::Equals(
        $workflowFiles[0].FullName, $workflowPath,
        [StringComparison]::OrdinalIgnoreCase)
    if (-not $singleWorkflow) {
        throw "CI must have exactly one workflow file: .github/workflows/ci.yml."
    }

    $lines = @(Get-Content -LiteralPath $workflowPath -Encoding UTF8)
    $usesLines = @($lines | Where-Object { $_ -match '^\s*uses:' })
    if ($usesLines.Count -eq 0) {
        throw "CI workflow does not contain any pinned actions."
    }
    foreach ($line in $usesLines) {
        if ($line -notmatch '^\s*uses:\s+[^@\s]+@[0-9a-fA-F]{40}\s+#\s+v\S+\s*$') {
            throw "GitHub Action must use a full commit SHA with a version comment: $($line.Trim())"
        }
    }

    $runnerLines = @($lines | Where-Object { $_ -match '^\s*runs-on:' })
    if ($runnerLines.Count -ne 1 -or
        $runnerLines[0] -notmatch '^\s*runs-on:\s+windows-2022\s*$') {
        throw "CI workflow must use the windows-2022 runner."
    }

    $runLines = @($lines | Where-Object { $_ -match '^\s*run:' })
    if ($runLines.Count -ne 1 -or
        $runLines[0] -notmatch '^\s*run:\s+\./tools/ci\.ps1\s*$') {
        throw "CI workflow must call tools/ci.ps1 as its single pipeline entry point."
    }

    $requiredLines = @(
        '^\s*pull_request:\s*$',
        '^\s*push:\s*$',
        '^\s*workflow_dispatch:\s*$',
        '^\s*-\s+main\s*$',
        '^\s*-\s+"v\*"\s*$',
        '^\s*permissions:\s*$',
        '^\s*contents:\s+read\s*$',
        '^\s*cancel-in-progress:\s+true\s*$',
        '^\s*timeout-minutes:\s+30\s*$'
    )
    foreach ($pattern in $requiredLines) {
        if (-not ($lines | Where-Object { $_ -match $pattern })) {
            throw "CI workflow is missing required policy line matching: $pattern"
        }
    }
}

function Assert-VendorSourcePolicy {
    $legacyVendorPath = Join-Path $repoRoot "third_party\PowerToys"
    if (Test-Path -LiteralPath $legacyVendorPath) {
        throw "Legacy PowerToys vendor path must be removed: $legacyVendorPath"
    }

    $scanRoots = @(
        "CMakeLists.txt",
        "CMakePresets.json",
        "src",
        "include",
        "tests",
        ".github"
    )
    $forbiddenTokens = @(
        "PowerToys",
        "third_party/PowerToys",
        "third_party\\PowerToys",
        "KeyboardManagerState",
        "KeyboardHookDecision",
        "LowlevelKeyboardEvent",
        "ModifierKey",
        "KeyboardManager/KeyboardManagerState",
        "KeyboardManager\\KeyboardManagerState",
        "Shortcut.h",
        "Shortcut.cpp"
    )

    foreach ($relativeRoot in $scanRoots) {
        $path = Join-Path $repoRoot $relativeRoot
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Vendor policy scan root is missing: $relativeRoot"
        }
        $item = Get-Item -LiteralPath $path
        $files = if ($item.PSIsContainer) {
            @(Get-ChildItem -LiteralPath $path -Recurse -File -Force)
        } else {
            @($item)
        }
        foreach ($file in $files) {
            $matches = @(Select-String -LiteralPath $file.FullName `
                -Pattern $forbiddenTokens -SimpleMatch)
            if ($matches.Count -ne 0) {
                $match = $matches[0]
                throw "Removed vendor source reference found at " +
                    "$($file.FullName):$($match.LineNumber): $($match.Line.Trim())"
            }
        }
    }
}

function Assert-ReleaseConfiguration {
    $cachePath = Join-Path $buildDirectory "CMakeCache.txt"
    $projectPath = Join-Path $buildDirectory "simpilot.vcxproj"
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        throw "CMakeCache.txt was not generated."
    }
    if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
        throw "simpilot.vcxproj was not generated."
    }

    Assert-Equal (Get-CMakeCacheValue $cachePath "CMAKE_GENERATOR") `
        "Visual Studio 17 2022" "CMake generator"
    Assert-Equal (Get-CMakeCacheValue $cachePath "CMAKE_GENERATOR_PLATFORM") `
        "x64" "CMake generator platform"
    Assert-Equal (Get-CMakeCacheValue $cachePath "CMAKE_CONFIGURATION_TYPES") `
        "Debug;Release" "CMake configurations"
    Assert-Equal (Get-CMakeCacheValue $cachePath "CMAKE_CXX_FLAGS_RELEASE") `
        "/O2 /Ob2 /DNDEBUG" "Release compiler flags"
    Assert-Equal (Get-CMakeCacheValue $cachePath "CMAKE_EXE_LINKER_FLAGS_RELEASE") `
        "/INCREMENTAL:NO" "Release linker flags"

    [xml]$project = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
    $namespace = New-Object System.Xml.XmlNamespaceManager($project.NameTable)
    $namespace.AddNamespace("msb", $project.Project.NamespaceURI)
    $optimization = $project.SelectSingleNode(
        '//msb:ItemDefinitionGroup[contains(@Condition, "Release|x64")]/msb:ClCompile/msb:Optimization',
        $namespace)
    $linkIncremental = $project.SelectSingleNode(
        '//msb:PropertyGroup/msb:LinkIncremental[contains(@Condition, "Release|x64")]',
        $namespace)
    if (-not $optimization) {
        throw "Release Optimization is missing from simpilot.vcxproj."
    }
    if (-not $linkIncremental) {
        throw "Release LinkIncremental is missing from simpilot.vcxproj."
    }
    Assert-Equal $optimization.InnerText "MaxSpeed" "Release optimization"
    Assert-Equal $linkIncremental.InnerText "false" "Release incremental linking"
}

function Assert-DocumentationVersion {
    param([Parameter(Mandatory)] [string]$Version)

    $manuals = @(
        @{ Path = "docs\zh-CN\用户手册.md"; Text = "适用版本：$Version" },
        @{ Path = "docs\en-US\User-Manual.md"; Text = "Applies to version: $Version" }
    )
    foreach ($manual in $manuals) {
        $path = Join-Path $repoRoot $manual.Path
        $content = Get-Content -LiteralPath $path -Raw -Encoding UTF8
        if (-not $content.Contains($manual.Text, [StringComparison]::Ordinal)) {
            throw "$($manual.Path) does not declare project version $Version."
        }
    }
}

function Assert-TagVersion {
    param([Parameter(Mandatory)] [string]$Version)

    if ($env:GITHUB_REF_TYPE -eq "tag") {
        Assert-Equal $env:GITHUB_REF_NAME "v$Version" "Git tag"
    }
}

function Get-PercentChange {
    param(
        [Parameter(Mandatory)] [long]$Current,
        [Parameter(Mandatory)] [long]$Previous
    )

    if ($Previous -le 0) {
        throw "Previous artifact size must be greater than zero."
    }
    return (($Current - $Previous) / [double]$Previous) * 100.0
}

function Assert-ArtifactSizes {
    param(
        [Parameter(Mandatory)] [string]$Version,
        [Parameter(Mandatory)] [long]$ExeBytes,
        [Parameter(Mandatory)] [long]$ZipBytes
    )

    if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
        throw "Release size baseline is missing."
    }
    $baseline = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([version]$Version -lt [version]$baseline.version) {
        throw "Project version $Version is older than release baseline $($baseline.version)."
    }

    $previousExeBytes = [long]$baseline.exeBytes
    $previousZipBytes = [long]$baseline.zipBytes
    $exeDelta = $ExeBytes - $previousExeBytes
    $zipDelta = $ZipBytes - $previousZipBytes
    $exePercent = Get-PercentChange $ExeBytes $previousExeBytes
    $zipPercent = Get-PercentChange $ZipBytes $previousZipBytes
    $threshold = [double]$baseline.maximumGrowthPercent
    Assert-Equal $threshold 5.0 "Maximum artifact growth percent"

    $baselineHash = [string]$baseline.zipSha256
    if ($baselineHash -notmatch '^[0-9a-fA-F]{64}$') {
        throw "release-baseline.json zipSha256 must contain 64 hexadecimal characters."
    }
    $recordedAt = [string]$baseline.recordedAt
    if ($recordedAt -notmatch '^\d{4}-\d{2}-\d{2}$') {
        throw "release-baseline.json recordedAt must use YYYY-MM-DD."
    }

    Write-Host "Release size comparison against v$($baseline.version):"
    Write-Host ("  EXE {0} -> {1}, delta {2}, {3:N4}%" -f `
        $previousExeBytes, $ExeBytes, $exeDelta, $exePercent)
    Write-Host ("  ZIP {0} -> {1}, delta {2}, {3:N4}%" -f `
        $previousZipBytes, $ZipBytes, $zipDelta, $zipPercent)

    $growthExceeded = $exePercent -gt $threshold -or $zipPercent -gt $threshold
    if (-not $growthExceeded) {
        return @{
            BaselineVersion = [string]$baseline.version
            ExeDelta = $exeDelta
            ExePercent = $exePercent
            ZipDelta = $zipDelta
            ZipPercent = $zipPercent
            Threshold = $threshold
        }
    }

    $approval = $baseline.approvedGrowth
    if ($null -eq $approval) {
        throw "Artifact growth exceeds $threshold% without an approvedGrowth record."
    }
    $reason = [string]$approval.reason
    if ([string]::IsNullOrWhiteSpace($reason)) {
        throw "approvedGrowth.reason must explain the verified functional or resource change."
    }
    if ($ExeBytes -gt [long]$approval.maxExeBytes) {
        throw "Simpilot.exe exceeds approvedGrowth.maxExeBytes."
    }
    if ($ZipBytes -gt [long]$approval.maxZipBytes) {
        throw "Release ZIP exceeds approvedGrowth.maxZipBytes."
    }
    Write-Warning "Artifact growth is covered by the reviewed approval: $reason"

    return @{
        BaselineVersion = [string]$baseline.version
        ExeDelta = $exeDelta
        ExePercent = $exePercent
        ZipDelta = $zipDelta
        ZipPercent = $zipPercent
        Threshold = $threshold
    }
}

function Assert-ReleaseArtifacts {
    param([Parameter(Mandatory)] [string]$Version)

    $releaseDirectory = Join-Path $buildDirectory "Release"
    $exePath = Join-Path $releaseDirectory "Simpilot.exe"
    $zipPath = Join-Path $buildDirectory "Simpilot-$Version-win-x64.zip"
    $checksumPath = "$zipPath.sha256"
    foreach ($path in @($exePath, $zipPath, $checksumPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Expected release artifact is missing: $path"
        }
    }

    $exe = Get-Item -LiteralPath $exePath
    $versionInfo = $exe.VersionInfo
    Assert-Equal $versionInfo.FileVersion "$Version.0" "Simpilot.exe FileVersion"
    Assert-Equal $versionInfo.ProductVersion "$Version.0" "Simpilot.exe ProductVersion"
    Assert-Equal $versionInfo.ProductName "简驭 | Simpilot" "Simpilot.exe ProductName"

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $expectedFiles = [ordered]@{
        "Everything/Everything.exe" = (Get-Item (Join-Path $repoRoot "third_party\Everything\Everything.exe")).Length
        "Everything/Everything.ini" = (Get-Item (Join-Path $repoRoot "third_party\Everything\Everything.ini")).Length
        "Everything/Everything.lng" = (Get-Item (Join-Path $repoRoot "third_party\Everything\Everything.lng")).Length
        "Everything/Everything64.dll" = (Get-Item (Join-Path $repoRoot "third_party\Everything\Everything64.dll")).Length
        "LICENSE" = (Get-Item (Join-Path $repoRoot "LICENSE")).Length
        "Simpilot.exe" = $exe.Length
        "THIRD-PARTY-NOTICES.txt" = (Get-Item (Join-Path $repoRoot "THIRD-PARTY-NOTICES.txt")).Length
    }

    $archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $entries = @($archive.Entries | Where-Object { -not $_.FullName.EndsWith('/') })
        $actualNames = @($entries.FullName | Sort-Object)
        $expectedNames = @($expectedFiles.Keys | Sort-Object)
        $difference = @(Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames)
        if ($difference.Count -ne 0) {
            throw "Release ZIP contents differ from the fixed whitelist: $($difference | Out-String)"
        }
        foreach ($entry in $entries) {
            Assert-Equal ([long]$entry.Length) ([long]$expectedFiles[$entry.FullName]) `
                "ZIP entry size for $($entry.FullName)"
        }
    } finally {
        $archive.Dispose()
    }

    $checksumText = (Get-Content -LiteralPath $checksumPath -Raw -Encoding UTF8).Trim()
    $checksumMatch = [regex]::Match(
        $checksumText,
        '^(?<hash>[0-9a-fA-F]{64})\s+\*?(?<name>\S+)$')
    if (-not $checksumMatch.Success) {
        throw "SHA-256 file has an invalid format."
    }
    Assert-Equal $checksumMatch.Groups["name"].Value `
        (Split-Path -Leaf $zipPath) "SHA-256 file name"
    $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
    Assert-Equal $checksumMatch.Groups["hash"].Value.ToUpperInvariant() `
        $actualHash.ToUpperInvariant() "Release ZIP SHA-256"

    $zip = Get-Item -LiteralPath $zipPath
    $sizeResult = Assert-ArtifactSizes $Version $exe.Length $zip.Length
    return @{
        ExePath = $exe.FullName
        ExeBytes = $exe.Length
        ZipPath = $zip.FullName
        ZipBytes = $zip.Length
        ChecksumPath = (Get-Item -LiteralPath $checksumPath).FullName
        ZipSha256 = $actualHash
        Size = $sizeResult
    }
}

function Write-CiSummary {
    param(
        [Parameter(Mandatory)] [string]$Version,
        [Parameter(Mandatory)] $Artifacts
    )

    $summary = @"
## Simpilot CI

- Version: ``$Version``
- Pipeline: ``Visual Studio 2022 / x64 / Release``
- Tests: ``All registered tests passed``
- Simpilot.exe: ``$($Artifacts.ExeBytes) bytes``
- ZIP: ``$($Artifacts.ZipBytes) bytes``
- ZIP SHA-256: ``$($Artifacts.ZipSha256)``
- Baseline: ``v$($Artifacts.Size.BaselineVersion)``
- EXE change: ``$($Artifacts.Size.ExeDelta) bytes ($("{0:N4}" -f $Artifacts.Size.ExePercent)%)``
- ZIP change: ``$($Artifacts.Size.ZipDelta) bytes ($("{0:N4}" -f $Artifacts.Size.ZipPercent)%)``
"@
    Write-Host $summary
    if ($env:GITHUB_STEP_SUMMARY) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $summary -Encoding UTF8
    }
}

$expectedBuildDirectory = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build\ci-vs2022-x64"))
if (-not [string]::Equals(
        [IO.Path]::GetFullPath($buildDirectory),
        $expectedBuildDirectory,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean an unexpected CI build directory."
}

$version = Get-ProjectVersion
Assert-WorkflowPolicy
Assert-VendorSourcePolicy
Assert-DocumentationVersion $version
Assert-TagVersion $version

if (Test-Path -LiteralPath $buildDirectory) {
    Remove-Item -LiteralPath $buildDirectory -Recurse -Force
}

Push-Location $repoRoot
try {
    Invoke-NativeCommand "cmake" @("--fresh", "--preset", "ci-vs2022-x64")
    Assert-ReleaseConfiguration
    Invoke-NativeCommand "cmake" @("--build", "--preset", "ci-release", "--parallel", "2")
    Invoke-NativeCommand "ctest" @("--preset", "ci-release")
    Invoke-NativeCommand "cmake" @(
        "--build", "--preset", "ci-package-release", "--parallel", "2")
    $artifacts = Assert-ReleaseArtifacts $version
    Write-CiSummary $version $artifacts
} finally {
    Pop-Location
}
