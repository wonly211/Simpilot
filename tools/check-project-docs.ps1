[CmdletBinding()]
param(
    [ValidateSet('Public','All')] [string]$Scope = 'Public',
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Report
)
$ErrorActionPreference = 'Stop'
$arguments = @('-B', (Join-Path $PSScriptRoot 'project_docs.py'), '--root', $Root, '--scope', $Scope)
if ($Report) { $arguments += @('--report', $Report) }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "Project documentation check failed ($Scope)." }
