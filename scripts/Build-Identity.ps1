param(
    [Parameter(Mandatory)][string]$AppDirectory,
    [string]$Publisher = 'CN=Convertii',
    [string]$CertificateThumbprint,
    [string]$SdkBin,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $SdkBin) {
    $sdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
    $SdkBin = Join-Path $sdkRoot 'bin\10.0.26100.0\x64'
}
$stage = Join-Path $repo 'artifacts\identity-stage'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
& (Join-Path $PSScriptRoot 'New-Assets.ps1') -OutputDirectory (Join-Path $stage 'Assets')
[xml]$manifest = Get-Content -LiteralPath (Join-Path $repo 'packaging\identity\AppxManifest.xml') -Raw
$manifest.Package.Identity.Publisher = $Publisher
$manifest.Save((Join-Path $stage 'AppxManifest.xml'))
$out = Join-Path $AppDirectory 'identity'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$package = Join-Path $out 'Convertii.Identity.msix'
& (Join-Path $SdkBin 'makeappx.exe') pack /d $stage /p $package /nv /o
if ($LASTEXITCODE) { throw 'Identity packaging failed.' }
if ($CertificateThumbprint) {
    & (Join-Path $SdkBin 'signtool.exe') sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $package
    if ($LASTEXITCODE) { throw 'Identity signing failed.' }
} else {
    Write-Warning 'Unsigned identity created for inspection only. Registration needs a trusted signature; no certificate has been installed.'
}
