[CmdletBinding()]
param([string]$Target = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Target) { $Target = Join-Path $repoRoot 'build/wiki-publish' }
& python -B (Join-Path $PSScriptRoot 'project_docs.py') --root $repoRoot --publish-wiki $Target
if ($LASTEXITCODE -ne 0) { throw 'Wiki validation/publication failed.' }
