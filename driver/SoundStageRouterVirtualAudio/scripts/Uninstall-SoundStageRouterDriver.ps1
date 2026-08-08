[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$Force,
    [string]$OriginalInfName = 'SoundStageRouterVirtualAudio.inf'
)

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdministrator)) {
    throw 'Administrator elevation is required. Re-run this script from an elevated PowerShell session.'
}

if (-not $Force) {
    throw 'Refusing to uninstall without -Force. Re-run with -Force (and optionally -WhatIf or -Confirm) after reviewing the documentation.'
}

$blocks = ((& pnputil /enum-drivers) -join "`n") -split "(`r?`n){2,}"
$publishedName = $null

foreach ($block in $blocks) {
    if ($block -match [regex]::Escape($OriginalInfName) -or $block -match 'SoundStage Router Project') {
        if ($block -match 'Published Name\s*:\s*(oem\d+\.inf)') {
            $publishedName = $Matches[1]
            break
        }
    }
}

if (-not $publishedName) {
    throw "Could not find an installed driver matching $OriginalInfName. Inspect 'pnputil /enum-drivers' manually."
}

if ($PSCmdlet.ShouldProcess($publishedName, 'pnputil /delete-driver /uninstall /force')) {
    & pnputil /delete-driver $publishedName /uninstall /force
}

Write-Host 'Optional cleanup: devcon remove "Root\SoundStageRouterVirtualAudio" if the root-enumerated device node remains.'
