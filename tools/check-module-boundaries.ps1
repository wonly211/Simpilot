[CmdletBinding()]
param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path
$modulesRoot = Join-Path $Root 'src/modules'
$headers = @{}
$appHeaders = @{}
foreach ($header in Get-ChildItem -LiteralPath (Join-Path $Root 'src/app') -File -Filter '*.hpp') {
    $appHeaders[$header.Name] = $true
}
if (Test-Path -LiteralPath $modulesRoot) {
    foreach ($header in Get-ChildItem -LiteralPath $modulesRoot -Recurse -File -Filter '*.hpp') {
        $relative = $header.FullName.Substring($modulesRoot.Length + 1).Replace('\', '/')
        $owner = $relative.Split('/')[0]
        if (-not $headers.ContainsKey($header.Name)) { $headers[$header.Name] = @() }
        $headers[$header.Name] += $owner
    }
}
$errors = @()
$files = @(Get-ChildItem -LiteralPath (Join-Path $Root 'src'), (Join-Path $Root 'include') -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.hpp') })
foreach ($file in $files) {
    $relative = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
    if ($relative -match '/tests/') { continue }
    $owner = if ($relative -match '^src/modules/([^/]+)/') { $Matches[1] } else { '' }
    $line = 0
    foreach ($text in Get-Content -LiteralPath $file.FullName) {
        ++$line
        if ($relative -ne 'src/app/register_builtin_modules.cpp' -and
            $text -match '^\s*#\s*(if|ifdef|ifndef).*SIMPILOT_MODULE_') {
            $errors += "${relative}:${line}: module build condition outside composition root"
        }
        if ($text -notmatch '^\s*#\s*include\s*"([^"]+)"') { continue }
        $include = $Matches[1].Replace('\', '/')
        $name = [IO.Path]::GetFileName($include)
        if ($owner -and ($include -match '(^|/)app/' -or $appHeaders.ContainsKey($name))) {
            $errors += "${relative}:${line}: module depends on app implementation"
        }
        if (($relative -match '^src/core/' -or $relative -match '^include/') -and
            $appHeaders.ContainsKey($name)) {
            $errors += "${relative}:${line}: shared code depends on app implementation"
        }
        if (-not $headers.ContainsKey($name)) { continue }
        if ($owner -and $headers[$name] -contains $owner) {
            if ($include -match 'modules/([^/]+)/' -and $Matches[1] -ne $owner) {
                $errors += "${relative}:${line}: cross-module implementation include"
            }
            continue
        }
        if ($relative -eq 'src/app/register_builtin_modules.cpp' -and $name -like '*_module.hpp') {
            continue
        }
        $errors += "${relative}:${line}: module implementation include $include"
    }
}
if ($errors.Count) { throw ($errors -join "`n") }
Write-Host 'Module include boundaries verified.'
