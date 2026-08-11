[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$Force,
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$driverRoot = Split-Path -Parent $scriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $driverRoot)
Import-Module (Join-Path $scriptRoot 'DriverInstallPlan.psm1') -Force

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

$existingDevices = @(Get-CimInstance Win32_PnPSignedDriver |
    Where-Object {
        [string]$_.DeviceID -like
            'ROOT\SOUNDSTAGEROUTERVIRTUALAUDIO\*'
    })
$existingPackages = @(Get-WindowsDriver -Online -All)
$installPlan = New-SoundStageDriverInstallPlan `
    -Devices $existingDevices -Packages $existingPackages

if ($PSCmdlet.ShouldProcess($certPath, 'Import test certificate into LocalMachine Root and TrustedPublisher')) {
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
}

foreach ($deviceInstanceId in $installPlan.DeviceInstanceIds) {
    if ($PSCmdlet.ShouldProcess(
        $deviceInstanceId,
        'Remove existing SoundStage Router root devnode')) {
        & $devcon remove "@$deviceInstanceId"
        if (-not (Test-SoundStageDevConSuccess $LASTEXITCODE)) {
            throw "devcon remove failed for $deviceInstanceId with exit code $LASTEXITCODE."
        }
    }
}

foreach ($publishedInfName in $installPlan.PublishedInfNames) {
    if ($PSCmdlet.ShouldProcess(
        $publishedInfName,
        'Remove existing SoundStage Router driver-store package')) {
        & pnputil /delete-driver $publishedInfName /uninstall /force
        if ($LASTEXITCODE -ne 0) {
            throw "pnputil delete-driver failed for $publishedInfName with exit code $LASTEXITCODE."
        }
    }
}

$installed = $false
if ($PSCmdlet.ShouldProcess('Root\SoundStageRouterVirtualAudio', 'Create exactly one root device and install driver with devcon')) {
    & $devcon install $infPath 'Root\SoundStageRouterVirtualAudio'
    if (-not (Test-SoundStageDevConSuccess $LASTEXITCODE)) {
        throw "devcon install failed with exit code $LASTEXITCODE."
    }
    $installed = $true
}

if ($installed) {
    $resultingDevices = @()
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        $resultingDevices = @(Get-CimInstance Win32_PnPSignedDriver |
            Where-Object {
                [string]$_.DeviceID -like
                    'ROOT\SOUNDSTAGEROUTERVIRTUALAUDIO\*'
            })
        if ($resultingDevices.Count -eq
            $installPlan.ExpectedFinalDeviceCount) {
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if ($resultingDevices.Count -ne
        $installPlan.ExpectedFinalDeviceCount) {
        throw "Expected exactly one SoundStage Router root devnode after installation; found $($resultingDevices.Count)."
    }
    if ($resultingDevices[0].PSObject.Properties.Name -contains
        'IsSigned' -and -not $resultingDevices[0].IsSigned) {
        throw 'The resulting SoundStage Router devnode is not associated with a signed driver package.'
    }
    Write-Host 'SoundStage Router Surround has exactly one root devnode. A reboot may be required before the endpoint appears.'
}
else {
    Write-Host 'No installation changes were applied.'
}

Write-Host "Diagnostics: `"$devcon`" status `"Root\SoundStageRouterVirtualAudio`""
