[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$BaselineBuildDirectory,
    [string]$Configuration = 'Release',
    [switch]$OnlySourceRemoval
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$cache = Join-Path $BaselineBuildDirectory 'CMakeCache.txt'
$options = @(foreach ($line in Get-Content -LiteralPath $cache) {
    if ($line -match '^(SIMPILOT_MODULE_[A-Z_]+):BOOL=') { $Matches[1] }
})
if (-not $options.Count) { throw 'No module build options found in the baseline CMake cache.' }
# Keep MSBuild's generated tracker paths below the Windows path-length limit.
$runRoot = Join-Path $root ("build/module-matrix/" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runRoot | Out-Null

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

$variants = @(foreach ($option in $options) {
    if (-not $OnlySourceRemoval) {
        [pscustomobject]@{ Name = $option.Substring('SIMPILOT_MODULE_'.Length).ToLowerInvariant(); Disabled = @($option); WithoutSources = $false }
    }
})
$variants += [pscustomobject]@{ Name = 'without-all-module-sources'; Disabled = $options; WithoutSources = $true }

foreach ($variant in $variants) {
    $variantRoot = Join-Path $runRoot $variant.Name
    $source = $root
    if ($variant.WithoutSources) {
        # This is an isolated source snapshot, not a move or deletion of the
        # working tree. Omit module directories entirely to detect hidden links.
        $source = Join-Path $variantRoot 'source'
        New-Item -ItemType Directory -Path $source -Force | Out-Null
        foreach ($name in @('CMakeLists.txt', 'CMakePresets.json', 'LICENSE',
                'THIRD-PARTY-NOTICES.txt', 'include', 'resources', 'assets', 'Languages',
                'third_party', 'tools', 'tests', '.github')) {
            Copy-Item -LiteralPath (Join-Path $root $name) -Destination $source -Recurse
        }
        $sourceSrc = Join-Path $source 'src'
        New-Item -ItemType Directory -Path $sourceSrc | Out-Null
        foreach ($directory in Get-ChildItem -LiteralPath (Join-Path $root 'src')) {
            if ($directory.Name -ne 'modules') {
                Copy-Item -LiteralPath $directory.FullName -Destination $sourceSrc -Recurse
            }
        }
    }
    $build = Join-Path $variantRoot 'build'
    $arguments = @('-S', $source, '-B', $build, '-G', 'Visual Studio 17 2022',
        '-A', 'x64', '-DBUILD_TESTING=ON')
    foreach ($option in $options) {
        $enabled = if ($variant.Disabled -contains $option) { 'OFF' } else { 'ON' }
        $arguments += "-D${option}=$enabled"
    }
    Write-Host "Verifying module variant: $($variant.Name)"
    Invoke-Checked 'cmake' $arguments
    Invoke-Checked 'cmake' @('--build', $build, '--config', $Configuration, '--parallel', '4')
    Invoke-Checked 'ctest' @('--test-dir', $build, '-C', $Configuration, '--output-on-failure')
    $stage = Join-Path $variantRoot 'stage'
    Invoke-Checked 'cmake' @('--install', $build, '--config', $Configuration, '--prefix', $stage)
    $expected = @('Simpilot.exe', 'LICENSE', 'THIRD-PARTY-NOTICES.txt')
    $expected += @(Get-Content -LiteralPath (Join-Path $build 'module-runtime-files.txt') |
        Where-Object { $_ })
    $actual = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
        ForEach-Object { $_.FullName.Substring($stage.Length + 1).Replace('\', '/') })
    $difference = @(Compare-Object $expected $actual)
    if ($difference.Count) { throw "Installed files do not match the active module manifest: $($difference | Out-String)" }
}
Write-Host "Module matrix and source-removal rehearsal passed: $runRoot"
