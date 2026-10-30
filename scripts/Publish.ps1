param(
 [Parameter(Mandatory)][string]$EngineDirectory,
 [string]$DownloadBaseUri,
 [string]$CertificateThumbprint,
 [string]$Publisher = 'CN=Convertii',
 [string]$IdentityPackage
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = '0.2.0'
$out = Join-Path $repo 'artifacts\release'
$bundle = Join-Path $out "Convertii-$version-win-x64"
if (Test-Path -LiteralPath $bundle) { throw 'This release directory already exists. Move it aside before creating a fresh release.' }
$engineLock = Get-Content -LiteralPath (Join-Path $repo 'packaging\engine.lock.json') -Raw | ConvertFrom-Json
$engineBin = Join-Path $EngineDirectory 'bin'
if (-not (Test-Path -LiteralPath $engineBin)) { $engineBin = $EngineDirectory }
foreach ($name in @('ffmpeg','ffprobe')) {
 $file = Join-Path $engineBin "$name.exe"
 if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $engineLock."$($name)Sha256") { throw "Pinned $name checksum mismatch." }
}
$configuration = & (Join-Path $engineBin 'ffmpeg.exe') -buildconf 2>&1 | Out-String
if ($configuration.Contains('--enable-nonfree')) { throw 'Non-redistributable engine build refused.' }
foreach ($name in @('LICENSE','README.txt')) { if (-not (Test-Path -LiteralPath (Join-Path $EngineDirectory $name))) { throw "Engine $name is required." } }
if ($DownloadBaseUri -and -not $DownloadBaseUri.StartsWith('https://')) { throw 'Release downloads must use HTTPS.' }
& (Join-Path $PSScriptRoot 'New-Assets.ps1')
$baseBuild = Join-Path $repo 'artifacts\native-build'
& (Join-Path $PSScriptRoot 'Build.ps1') -ExtraArguments @("-DCONVERTII_PUBLISHER=$Publisher", "-DCONVERTII_RELEASE_CONFIG=$(Join-Path $repo 'packaging\release.dev.json')", '-DCONVERTII_PAYLOAD=')
$bin = Join-Path $baseBuild 'bin\Release'
New-Item -ItemType Directory -Path $bundle,(Join-Path $bundle 'engine'),(Join-Path $bundle 'licenses'),(Join-Path $bundle 'identity'),(Join-Path $bundle 'docs') -Force | Out-Null
Copy-Item -Path (Join-Path $repo 'docs\*.md') -Destination (Join-Path $bundle 'docs')
Copy-Item -LiteralPath (Join-Path $bin 'Convertii.exe'),(Join-Path $bin 'ConvertiiSetup.exe'),(Join-Path $baseBuild 'shell\Release\ConvertiiShell.dll') -Destination $bundle
Copy-Item -LiteralPath (Join-Path $engineBin 'ffmpeg.exe'),(Join-Path $engineBin 'ffprobe.exe') -Destination (Join-Path $bundle 'engine')
Copy-Item -LiteralPath (Join-Path $repo 'packaging\engine.lock.json') -Destination (Join-Path $bundle 'engine')
Copy-Item -LiteralPath (Join-Path $EngineDirectory 'LICENSE') -Destination (Join-Path $bundle 'licenses\FFmpeg-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $EngineDirectory 'README.txt') -Destination (Join-Path $bundle 'licenses\FFmpeg-build-and-source.txt')
[IO.File]::WriteAllText((Join-Path $bundle 'licenses\FFmpeg-configuration.txt'),$configuration)
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE'),(Join-Path $repo 'README.md') -Destination $bundle
Copy-Item -LiteralPath (Join-Path $repo 'docs\THIRD-PARTY.md') -Destination (Join-Path $bundle 'licenses')
Copy-Item -LiteralPath (Join-Path $repo 'third_party\nlohmann\LICENSE.MIT') -Destination (Join-Path $bundle 'licenses\nlohmann-json-MIT.txt')
Copy-Item -LiteralPath (Join-Path $repo 'third_party\miniz\LICENSE') -Destination (Join-Path $bundle 'licenses\miniz-LICENSE.txt')
if ($IdentityPackage) {
 Copy-Item -LiteralPath $IdentityPackage -Destination (Join-Path $bundle 'identity\Convertii.Identity.msix')
} else {
 & (Join-Path $PSScriptRoot 'Build-Identity.ps1') -AppDirectory $bundle -Publisher $Publisher -CertificateThumbprint $CertificateThumbprint
}
if ($CertificateThumbprint) {
 $sdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
 $signTool = Join-Path $sdkRoot 'bin\10.0.26100.0\x64\signtool.exe'
 foreach ($name in @('Convertii.exe','ConvertiiSetup.exe','ConvertiiShell.dll')) {
  & $signTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 (Join-Path $bundle $name)
  if ($LASTEXITCODE) { throw 'Binary signing failed.' }
 }
}
function Invoke-Native([string]$Exe,[string[]]$Arguments) {
 $quoted = $Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }
 $process = Start-Process -FilePath $Exe -ArgumentList $quoted -WindowStyle Hidden -PassThru
 $process.WaitForExit()
 if ($process.ExitCode) { throw "Native release helper failed ($($process.ExitCode))." }
}
$archiveName = "Convertii-$version-win-x64.zip"
$archive = Join-Path $out $archiveName
Invoke-Native (Join-Path $bin 'ConvertiiSetup.exe') @('--create-zip',$bundle,$archive)
$url = if ($DownloadBaseUri) { $DownloadBaseUri.TrimEnd('/') + '/' + $archiveName } else { $null }
$config = Join-Path $out 'release-native.json'
@{ Version=$version; FileName=$archiveName; Sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash; DownloadUrl=$url } | ConvertTo-Json | Set-Content -LiteralPath $config -Encoding utf8
foreach ($kind in @('Online','Offline')) {
 $payload = if ($kind -eq 'Offline') { $archive } else { '' }
 & (Join-Path $PSScriptRoot 'Build.ps1') -BuildDirectory 'artifacts\setup-build' -Targets ConvertiiSetup -ExtraArguments @("-DCONVERTII_RELEASE_CONFIG=$config", "-DCONVERTII_PAYLOAD=$payload")
 $setup = Join-Path $out "Convertii-$version-$kind-Setup.exe"
 Copy-Item -LiteralPath (Join-Path $repo 'artifacts\setup-build\bin\Release\ConvertiiSetup.exe') -Destination $setup
 if ($CertificateThumbprint) { & $signTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $setup; if ($LASTEXITCODE) { throw 'Installer signing failed.' } }
 Invoke-Native $setup @('--verify-payload',$archive)
}
Get-ChildItem -LiteralPath $out -File | Where-Object { $_.Name -like "Convertii-$version-*" -and $_.Extension -in '.zip','.exe' } | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $([IO.Path]::GetFileName($_.Path))" } | Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS-native.txt') -Encoding ascii
& (Join-Path $bin 'InstallTests.exe') --verify-release $archive (Join-Path $out "Convertii-$version-Online-Setup.exe") (Join-Path $out "Convertii-$version-Offline-Setup.exe")
if ($LASTEXITCODE) { throw 'Native release resource verification failed.' }
if (-not $url) { Write-Warning 'No hosted URL configured. The online installer accepts the matching ZIP beside it. Pass -DownloadBaseUri for a hosted release.' }
if (-not $CertificateThumbprint) { Write-Warning 'Unsigned developer build. Modern Explorer registration requires a trusted signature.' }
Get-ChildItem -LiteralPath $out -File | Where-Object Name -like "Convertii-$version-*" | Select-Object Name,Length
