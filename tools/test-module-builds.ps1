[CmdletBinding()]
param(
    [string]$BaselineBuildDirectory,
    [ValidateSet('Release')] [string]$Configuration = 'Release',
    [switch]$OnlySourceRemoval,
    [string]$Variant,
    $Plan,
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$baselinePath = Join-Path $repoRoot '.github/ci/release-baseline.json'
. (Join-Path $PSScriptRoot 'ci-common.ps1')
. (Join-Path $PSScriptRoot 'ci-protocol.ps1')

if (-not $Plan) {
    if (-not $BaselineBuildDirectory) { throw 'Provide a baseline CMake build or a CI plan.' }
    $options = @(Get-CiModuleOptions (Join-Path $BaselineBuildDirectory 'CMakeCache.txt'))
    $Plan = [pscustomobject]@{ protocol = $script:CiProtocol; fingerprint = Get-CiFingerprint
        sha = ('0' * 40); repository = 'local'; runId = 0; version = Get-ProjectVersion
        options = $options; variants = @(Get-CiVariants $options); mode = 'build' }
}
Assert-CiPlan $Plan
$variants = @($Plan.variants | Where-Object { $_.id -ne 'full' })
if ($OnlySourceRemoval) { $variants = @($variants | Where-Object withoutSources) }
if ($Variant) {
    $variants = @($Plan.variants | Where-Object id -EQ $Variant)
    if ($variants.Count -ne 1) { throw "Unknown variant: $Variant" }
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build/module-results' }

foreach ($item in $variants) {
    $destination = Join-Path $OutputDirectory $item.id
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $record = [ordered]@{
        protocol = $Plan.protocol; fingerprint = $Plan.fingerprint; repository = $Plan.repository
        sha = $Plan.sha; version = $Plan.version; runId = $Plan.runId
        attempt = $(if ($env:GITHUB_RUN_ATTEMPT) { [int]$env:GITHUB_RUN_ATTEMPT } else { 1 })
        variant = $item.id; disabled = @($item.disabled); withoutSources = $item.withoutSources
        status = 'failure'; configuration = 'Release'; ipo = $false; installVerified = $false
        expectedTests = @(); passedTests = @(); package = $null; error = $null
        startedAt = [DateTimeOffset]::UtcNow.ToString('o'); timings = [ordered]@{}
    }
    $total = [Diagnostics.Stopwatch]::StartNew()
    function Invoke-Phase {
        param([string]$Name, [scriptblock]$Action)
        $watch = [Diagnostics.Stopwatch]::StartNew()
        try { & $Action } finally { $record.timings[$Name] = [math]::Round($watch.Elapsed.TotalSeconds, 3) }
    }
    # Short unique paths avoid MSBuild tracker path limits and ensure no cache reuse.
    $variantRoot = Join-Path $repoRoot ('build/mx/' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $source = $repoRoot
    $build = Join-Path $variantRoot 'b'
    Start-Transcript -LiteralPath (Join-Path $destination 'pipeline.log') | Out-Null
    try {
        Invoke-Phase 'configure' {
            if ($item.withoutSources) {
                $source = Join-Path $variantRoot 's'
                New-Item -ItemType Directory -Path $source -Force | Out-Null
                foreach ($name in @('CMakeLists.txt', 'CMakePresets.json', 'LICENSE',
                    'THIRD-PARTY-NOTICES.txt', 'include', 'resources', 'assets', 'Languages',
                    'third_party', 'tools', 'tests', '.github')) {
                    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $source -Recurse
                }
                $sourceSrc = Join-Path $source 'src'
                New-Item -ItemType Directory -Path $sourceSrc | Out-Null
                foreach ($directory in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src')) {
                    if ($directory.Name -ne 'modules') { Copy-Item -LiteralPath $directory.FullName -Destination $sourceSrc -Recurse }
                }
            }
            $arguments = @('-S', $source, '-B', $build, '-G', 'Visual Studio 17 2022',
                '-A', 'x64', '-DBUILD_TESTING=ON', '-DCMAKE_CONFIGURATION_TYPES=Debug;Release')
            foreach ($option in $Plan.options) {
                $enabled = if ($item.disabled -contains $option) { 'OFF' } else { 'ON' }
                $arguments += "-D${option}=$enabled"
            }
            Invoke-NativeCommand 'cmake' $arguments
            Assert-ReleaseConfiguration $build
            $record.ipo = $true
            foreach ($option in $Plan.options) {
                $enabled = if ($item.disabled -contains $option) { 'OFF' } else { 'ON' }
                Assert-Equal (Get-CMakeCacheValue (Join-Path $build 'CMakeCache.txt') $option) $enabled $option
            }
        }
        Invoke-Phase 'build' { Invoke-NativeCommand 'cmake' @('--build', $build, '--config', 'Release', '--parallel', '4') }
        Invoke-Phase 'test' {
            $listing = & ctest --test-dir $build -C Release --show-only=json-v1
            if ($LASTEXITCODE) { throw 'Cannot enumerate CTest tests.' }
            $listing | Set-Content -LiteralPath (Join-Path $destination 'tests.json') -Encoding utf8
            $record.expectedTests = @(($listing | ConvertFrom-Json).tests.name | Sort-Object)
            $junit = Join-Path $destination 'tests.xml'
            Invoke-NativeCommand 'ctest' @('--test-dir', $build, '-C', 'Release', '--output-on-failure',
                '--parallel', '1', '--no-tests=error', '--output-junit', $junit)
            [xml]$xml = Get-Content -LiteralPath $junit -Raw
            $cases = @($xml.SelectNodes('//testcase'))
            if (-not $cases.Count -or @($xml.SelectNodes('//failure|//error|//skipped')).Count) { throw 'Missing, skipped or failed tests.' }
            $record.passedTests = @($cases | ForEach-Object { $_.GetAttribute('name') } | Sort-Object)
            if ($record.expectedTests.Count -ne $record.passedTests.Count -or
                @(Compare-Object $record.expectedTests $record.passedTests).Count) { throw 'CTest did not run the complete test list.' }
        }
        Invoke-Phase 'install' {
            $stage = Join-Path $variantRoot 'stage'
            Invoke-NativeCommand 'cmake' @('--install', $build, '--config', 'Release', '--prefix', $stage)
            $expected = @('Simpilot.exe', 'LICENSE', 'THIRD-PARTY-NOTICES.txt') +
                @(Get-Content -LiteralPath (Join-Path $build 'module-runtime-files.txt') | Where-Object { $_ })
            $actual = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
                ForEach-Object { $_.FullName.Substring($stage.Length + 1).Replace('\', '/') })
            if (@(Compare-Object $expected $actual).Count) { throw 'Installed files differ from the active module manifest.' }
            $record.installVerified = $true
        }
        if ($item.id -eq 'full') {
            Invoke-Phase 'package' {
                Invoke-NativeCommand 'cmake' @('--build', $build, '--config', 'Release', '--target', 'package', '--parallel', '4')
                $artifacts = Assert-ReleaseArtifacts $Plan.version $build
                Copy-Item -LiteralPath $artifacts.ZipPath, $artifacts.ChecksumPath -Destination $destination
                $record.package = Assert-CiPackage $destination $Plan.version $artifacts.ZipSha256
                Write-CiSummary $Plan.version $artifacts
            }
        }
        $record.status = 'success'
    } catch {
        $record.error = $_.ToString()
        throw
    } finally {
        $record.timings.total = [math]::Round($total.Elapsed.TotalSeconds, 3)
        $record.completedAt = [DateTimeOffset]::UtcNow.ToString('o')
        Write-CiJson $record (Join-Path $destination 'result.json')
        foreach ($relative in @('CMakeCache.txt', 'Testing/Temporary/LastTest.log', 'CMakeFiles/CMakeConfigureLog.yaml')) {
            $path = Join-Path $build $relative
            if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $destination }
        }
        Stop-Transcript | Out-Null
    }
}
