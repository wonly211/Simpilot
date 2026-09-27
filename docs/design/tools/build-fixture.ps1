param(
    [ValidateSet("build/design-audit", "build/ci-vs2022-x64")]
    [string]$BuildDirectory = "build/design-audit"
)
$ErrorActionPreference = "Stop"
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = (Get-Command cmake).Source
$info.WorkingDirectory = (Resolve-Path "$PSScriptRoot/../../..").Path
$info.UseShellExecute = $false
# Pin the installed SDK; do not probe the protected per-user SDK directory.
foreach ($argument in @("--build", $BuildDirectory, "--config", "Release",
    "--parallel", "--", "/p:_LatestWindowsTargetPlatformVersion=10.0.26100.0",
    "/p:TargetPlatformSdkPath=C:\Program Files (x86)\Windows Kits\10\",
    "/p:TargetPlatformDisplayName=Windows")) {
    $info.ArgumentList.Add($argument)
}
# The runner can expose both PATH and Path; legacy MSBuild rejects that pair.
$environment = @{}
foreach ($item in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
    $environment[$item.Key] = $item.Value
}
$info.Environment.Clear()
foreach ($key in $environment.Keys) {
    $info.Environment[$key] = $environment[$key]
}
$process = [Diagnostics.Process]::Start($info)
$process.WaitForExit()
exit $process.ExitCode
