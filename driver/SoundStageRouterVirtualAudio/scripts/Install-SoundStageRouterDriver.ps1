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

function Find-DevCon {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools\10.0.28000.0\x64\devcon.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools\x64\devcon.exe')
    )
    $candidate = $candidates | Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if (-not $candidate) {
        throw 'devcon.exe was not found in the Windows Driver Kit. Install the WDK tools before installing this root-enumerated device.'
    }
    return $candidate
}

if (-not (Test-IsAdministrator)) {
    throw 'Administrator elevation is required. Re-run this script from an elevated PowerShell session.'
}

if (-not $Force) {
    throw 'Refusing to install without -Force. Re-run with -Force (and optionally -WhatIf or -Confirm) after reviewing the documentation.'
}

$bcdeditOutput = & bcdedit /enum all
if (($bcdeditOutput -join "`n") -notmatch 'testsigning\s+Yes') {
    throw @'
Windows TESTSIGNING is not enabled.
Enable it from an elevated prompt with "bcdedit /set testsigning on", then reboot
before installing this development-only driver package.
'@
}

$infPath = Join-Path $PackageRoot 'SoundStageRouterVirtualAudio.inf'
$certPath = Join-Path $driverRoot 'certs\SoundStageRouterVirtualAudio-Test.cer'

if (-not (Test-Path -LiteralPath $infPath)) {
    throw "INF not found: $infPath"
}
if (-not (Test-Path -LiteralPath $certPath)) {
    throw "Certificate not found: $certPath. Run Build-Driver.ps1 first."
}
$devcon = Find-DevCon

if ($PSCmdlet.ShouldProcess($certPath, 'Import test certificate into LocalMachine Root and TrustedPublisher')) {
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
}

if ($PSCmdlet.ShouldProcess('Root\SoundStageRouterVirtualAudio', 'Create root device and install driver with devcon')) {
    & $devcon install $infPath 'Root\SoundStageRouterVirtualAudio'
    if ($LASTEXITCODE -ne 0) {
        throw "devcon install failed with exit code $LASTEXITCODE."
    }
}

Write-Host 'SoundStage Router Surround device created. A reboot may be required before the endpoint appears.'
Write-Host "Diagnostics: `"$devcon`" status `"Root\SoundStageRouterVirtualAudio`""
