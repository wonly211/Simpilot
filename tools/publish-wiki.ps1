[CmdletBinding()]
param(
    [string]$Target = ""
)

$ErrorActionPreference = "Stop"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceRoot = Join-Path $repoRoot "docs\wiki"
$targetRoot = if ($Target) {
    (Resolve-Path -LiteralPath $Target -ErrorAction Stop).Path
} else {
    Join-Path $repoRoot "build\wiki-publish"
}

$languages = @("zh-CN", "zh-TW", "en-US")

foreach ($language in $languages) {
    $languagePath = Join-Path $sourceRoot $language
    if (-not (Test-Path -LiteralPath $languagePath -PathType Container)) {
        throw "Missing Wiki source directory: $languagePath"
    }
}

if (-not (Test-Path -LiteralPath $targetRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $targetRoot | Out-Null
}

# The Wiki repository is generated output. Only Markdown files at its root are replaced.
Get-ChildItem -LiteralPath $targetRoot -File -Filter "*.md" | Remove-Item -Force

$publishedNames = @{}
foreach ($language in $languages) {
    $languagePath = Join-Path $sourceRoot $language
    foreach ($sourceFile in Get-ChildItem -LiteralPath $languagePath -File -Filter "*.md") {
        if ($publishedNames.ContainsKey($sourceFile.Name)) {
            throw "Wiki page name collision: $($sourceFile.Name)"
        }

        $publishedNames[$sourceFile.Name] = $language
        $content = Get-Content -LiteralPath $sourceFile.FullName -Raw -Encoding utf8

        # Source pages use language-relative links; the published Wiki is flat.
        $content = $content -replace '\.\./(?:zh-CN|zh-TW|en-US)/', ''
        $content = $content.TrimEnd([char[]]"`r`n") + [Environment]::NewLine

        $targetFile = Join-Path $targetRoot $sourceFile.Name
        [IO.File]::WriteAllText($targetFile, $content, $utf8NoBom)
    }
}

$sidebar = Join-Path $sourceRoot "_Sidebar.md"
if (-not (Test-Path -LiteralPath $sidebar -PathType Leaf)) {
    throw "Missing Wiki sidebar: $sidebar"
}
Copy-Item -LiteralPath $sidebar -Destination (Join-Path $targetRoot "_Sidebar.md") -Force

Write-Host "Published $($publishedNames.Count) Wiki pages to $targetRoot"
