[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$Force,
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$driverRoot = Split-Path -Parent $scriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $driverRoot)

if (-not $PackageRoot) {
    $PackageRoot = Join-Path $repoRoot 'build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\x64\Release\SoundStageRouterVirtualAudioPackage'
}

function Test-IsAdministrator {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdministrator)) {
    throw 'Administrator elevation is required. Re-run this script from an elevated PowerShell session.'
}

if (-not $Force) {
    throw 'Refusing to install without -Force. Re-run with -Force (and optionally -WhatIf or -Confirm) after reviewing the documentation.'
}

$bcdeditOutput = & bcdedit /enum '{current}'
if ($bcdeditOutput -notmatch 'testsigning\s+Yes') {
    Write-Warning 'Windows TESTSIGNING is not enabled.'
    Write-Host 'Enable it from an elevated prompt, then reboot before installing this dev-only driver package:'
    Write-Host '  bcdedit /set testsigning on'
    Write-Host 'A reboot is required after changing this setting.'
    exit 1
}

$infPath = Join-Path $PackageRoot 'SoundStageRouterVirtualAudio.inf'
$certPath = Join-Path $driverRoot 'certs\SoundStageRouterVirtualAudio-Test.cer'

if (-not (Test-Path -LiteralPath $infPath)) {
    throw "INF not found: $infPath"
}
if (-not (Test-Path -LiteralPath $certPath)) {
    throw "Certificate not found: $certPath. Run Build-Driver.ps1 first."
}

if ($PSCmdlet.ShouldProcess($certPath, 'Import test certificate into LocalMachine Root and TrustedPublisher')) {
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
}

if ($PSCmdlet.ShouldProcess($infPath, 'pnputil /add-driver /install')) {
    & pnputil /add-driver $infPath /install
}

Write-Host 'Optional diagnostics: use devcon status "Root\SoundStageRouterVirtualAudio" or devcon rescan.'
