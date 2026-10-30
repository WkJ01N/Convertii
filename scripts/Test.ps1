param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location $repo
& (Join-Path $PSScriptRoot 'Build.ps1') -Configuration $Configuration -ExtraArguments @("-DCONVERTII_RELEASE_CONFIG=$(Join-Path $repo 'packaging\release.dev.json')", '-DCONVERTII_PAYLOAD=')
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$ctest = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir (Join-Path $repo 'artifacts\native-build') -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Native test suite failed.' }
