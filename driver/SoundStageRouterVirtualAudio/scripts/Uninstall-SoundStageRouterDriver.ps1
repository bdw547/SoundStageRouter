[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$Force,
    [string]$PublishedInfName
)

$ErrorActionPreference = 'Stop'

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
        throw 'devcon.exe was not found in the Windows Driver Kit.'
    }
    return $candidate
}

if (-not (Test-IsAdministrator)) {
    throw 'Administrator elevation is required. Re-run this script from an elevated PowerShell session.'
}

if (-not $Force) {
    throw 'Refusing to uninstall without -Force. Re-run with -Force (and optionally -WhatIf or -Confirm) after reviewing the documentation.'
}

$device = Get-CimInstance Win32_PnPSignedDriver |
    Where-Object { $_.DeviceID -like 'ROOT\SOUNDSTAGEROUTERVIRTUALAUDIO*' } |
    Select-Object -First 1
if (-not $PublishedInfName -and $device) {
    $PublishedInfName = $device.InfName
}

if (-not $PublishedInfName -or $PublishedInfName -notmatch '^oem\d+\.inf$') {
    throw 'Could not determine the published oemNN.inf name. If the device node is already absent, pass -PublishedInfName explicitly.'
}

$devcon = Find-DevCon
if ($device -and $PSCmdlet.ShouldProcess('Root\SoundStageRouterVirtualAudio', 'Remove root-enumerated device')) {
    & $devcon remove 'Root\SoundStageRouterVirtualAudio'
    if ($LASTEXITCODE -ne 0) {
        throw "devcon remove failed with exit code $LASTEXITCODE."
    }
}

if ($PSCmdlet.ShouldProcess($PublishedInfName, 'pnputil /delete-driver /uninstall /force')) {
    & pnputil /delete-driver $PublishedInfName /uninstall /force
    if ($LASTEXITCODE -ne 0) {
        throw "pnputil delete-driver failed with exit code $LASTEXITCODE."
    }
}
