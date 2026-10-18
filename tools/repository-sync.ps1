[CmdletBinding()]
param(
    [ValidateSet('Worktree','Staged','Commit','History','Hook','Export','GenerateIgnore','InstallHooks')]
    [string]$Mode = 'Worktree',
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Revision = 'HEAD',
    [string]$Base,
    [string]$Destination
)
$ErrorActionPreference = 'Stop'
$arguments = @('-B', (Join-Path $PSScriptRoot 'repository_sync.py'), '--mode', $Mode, '--root', $Root, '--revision', $Revision)
if ($Base) { $arguments += @('--base', $Base) }
if ($Destination) { $arguments += @('--destination', $Destination) }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "Repository synchronization check failed ($Mode)." }
