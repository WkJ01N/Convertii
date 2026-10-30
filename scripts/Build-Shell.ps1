param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'Build.ps1') -Configuration $Configuration -Targets ConvertiiShell
Write-Output (Join-Path $repo "artifacts\native-build\shell\$Configuration\ConvertiiShell.dll")