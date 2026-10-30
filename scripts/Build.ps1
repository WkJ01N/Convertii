param([string]$Configuration = 'Release', [string]$BuildDirectory = 'artifacts\native-build', [string[]]$ExtraArguments = @(), [string[]]$Targets = @())
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Install Visual Studio 2022 Desktop development with C++ and Windows SDK 10.0.26100.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repo $BuildDirectory }
# A build tree that travelled with the repository keeps absolute paths from its old location and
# then fails to configure or links stale objects. Recreate such a tree instead of trusting it.
$cacheFile = Join-Path $BuildDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $cacheFile) {
 $recorded = (Select-String -LiteralPath $cacheFile -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1).Matches[0].Groups[1].Value
 if ($recorded -and ($recorded -replace '\\','/').TrimEnd('/') -ne ($repo -replace '\\','/').TrimEnd('/')) {
  Write-Warning "Build tree '$BuildDirectory' was configured for '$recorded'. Recreating it for '$repo'."
  Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
 }
}
& $cmake -S $repo -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 @ExtraArguments
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
$buildArguments = @('--build',$BuildDirectory,'--config',$Configuration,'--parallel','1')
if ($Targets.Count) { $buildArguments += '--target'; $buildArguments += $Targets }
& $cmake @buildArguments
if ($LASTEXITCODE) { throw 'Native build failed.' }
