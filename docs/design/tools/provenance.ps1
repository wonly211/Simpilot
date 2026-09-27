param(
    [string]$Repository = (Resolve-Path "$PSScriptRoot/../../..").Path,
    [string]$Output = "$PSScriptRoot/../evidence/source-before.json"
)
$ErrorActionPreference = "Stop"
$roots = @("src", "include", "Languages", "resources", "tests", "cmake")
$files = foreach ($root in $roots) {
    $path = Join-Path $Repository $root
    if (Test-Path -LiteralPath $path) {
        Get-ChildItem -LiteralPath $path -Recurse -File
    }
}
$files += Get-Item -LiteralPath (Join-Path $Repository "CMakeLists.txt")
$hashes = @($files | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = [IO.Path]::GetRelativePath($Repository, $_.FullName).Replace("\", "/")
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
})
$record = [ordered]@{
    recordedAt = (Get-Date).ToString("o")
    head = (git -C $Repository rev-parse HEAD)
    branch = (git -C $Repository branch --show-current)
    status = @(git -C $Repository status --short)
    files = $hashes
}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Output))) | Out-Null
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Output "Recorded $($hashes.Count) production/build/test inputs."
