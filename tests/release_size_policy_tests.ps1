Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$repoRoot = $root
$baselinePath = Join-Path $root '.github/ci/release-baseline.json'
$baseline = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
$tokens = $null
$parseErrors = $null
$pipelineAst = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'tools/ci.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw "CI script has PowerShell syntax errors: $parseErrors" }

# Load release checks without running the build pipeline.
foreach ($name in @('Assert-WorkflowPolicy', 'Get-PercentChange', 'Get-ArtifactSizeComparison')) {
    $function = $pipelineAst.Find({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $name
    }, $false)
    if (-not $function) { throw "Missing CI function: $name" }
    . ([scriptblock]::Create($function.Extent.Text))
}

Assert-WorkflowPolicy

foreach ($factor in @(1, 2, 10)) {
    $exeBytes = [long]$baseline.exeBytes * $factor
    $zipBytes = [long]$baseline.zipBytes * $factor
    $comparison = Get-ArtifactSizeComparison $baseline.version $exeBytes $zipBytes
    $expectedPercent = ($factor - 1) * 100.0
    if ($comparison.ExePercent -ne $expectedPercent -or
        $comparison.ZipPercent -ne $expectedPercent -or
        $comparison.ExeDelta -ne ($exeBytes - [long]$baseline.exeBytes) -or
        $comparison.ZipDelta -ne ($zipBytes - [long]$baseline.zipBytes) -or
        $comparison.BaselineVersion -ne $baseline.version) {
        throw "Incorrect size statistics for factor $factor."
    }
}

$shrunk = Get-ArtifactSizeComparison $baseline.version `
    ([long]$baseline.exeBytes / 2) ([long]$baseline.zipBytes / 2)
if ($shrunk.ExePercent -ge 0 -or $shrunk.ZipPercent -ge 0) {
    throw 'Artifact reductions must remain visible in size statistics.'
}

$rejected = $false
try { Get-PercentChange 1 0 | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'An invalid zero-size baseline must still be rejected.' }

$rejected = $false
try {
    Get-ArtifactSizeComparison '0.0.0' $baseline.exeBytes $baseline.zipBytes | Out-Null
} catch { $rejected = $true }
if (-not $rejected) { throw 'A version older than the baseline must still be rejected.' }

Write-Host 'Release size statistics passed; growth above 5% needs no approval.'
