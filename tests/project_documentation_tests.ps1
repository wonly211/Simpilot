$ErrorActionPreference = 'Stop'
& python -B (Join-Path $PSScriptRoot 'project_documentation_tests.py')
if ($LASTEXITCODE -ne 0) { throw 'Offline documentation/synchronization regression tests failed.' }
