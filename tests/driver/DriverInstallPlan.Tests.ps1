$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$modulePath = Join-Path $repoRoot `
    'driver\SoundStageRouterVirtualAudio\scripts\DriverInstallPlan.psm1'
Import-Module $modulePath -Force

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    if ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', observed '$Actual'."
    }
}

$devices = @(
    [pscustomobject]@{
        DeviceID = 'ROOT\SOUNDSTAGEROUTERVIRTUALAUDIO\0000'
        InfName = 'oem41.inf'
    },
    [pscustomobject]@{
        DeviceID = 'ROOT\SOUNDSTAGEROUTERVIRTUALAUDIO\0001'
        InfName = 'oem42.inf'
    },
    [pscustomobject]@{
        DeviceID = 'ROOT\UNRELATED\0000'
        InfName = 'oem99.inf'
    },
    [pscustomobject]@{
        DeviceID = 'ROOT\SOUNDSTAGEROUTERVIRTUALAUDIOLEGACY\0000'
        InfName = 'oem98.inf'
    }
)
$packages = @(
    [pscustomobject]@{
        OriginalFileName = 'C:\staged\SoundStageRouterVirtualAudio.inf'
        Driver = 'oem42.inf'
    },
    [pscustomobject]@{
        OriginalFileName = 'SoundStageRouterVirtualAudio.inf'
        Driver = 'oem43.inf'
    },
    [pscustomobject]@{
        OriginalFileName = 'OtherAudio.inf'
        Driver = 'oem44.inf'
    }
)

$plan = New-SoundStageDriverInstallPlan `
    -Devices $devices -Packages $packages

Assert-Equal 2 $plan.DeviceInstanceIds.Count `
    'Every matching duplicate root devnode must be removed.'
Assert-Equal 3 $plan.PublishedInfNames.Count `
    'Attached and stale matching packages must be removed once each.'
Assert-Equal 'oem41.inf,oem42.inf,oem43.inf' `
    ($plan.PublishedInfNames -join ',') `
    'Published package names must be deterministic and unique.'
Assert-Equal 1 $plan.ExpectedFinalDeviceCount `
    'Installation must create exactly one intended instance.'

Assert-Equal $true (Test-SoundStageDevConSuccess 0) `
    'devcon exit code 0 must be accepted as success.'
Assert-Equal $true (Test-SoundStageDevConSuccess 1) `
    'devcon exit code 1 must be accepted as success with reboot required.'
Assert-Equal $false (Test-SoundStageDevConSuccess 2) `
    'devcon exit code 2 must be rejected as failure.'

$emptyPlan = New-SoundStageDriverInstallPlan -Devices @() -Packages @()
Assert-Equal 0 $emptyPlan.DeviceInstanceIds.Count `
    'A clean machine must require no removals.'
Assert-Equal 0 $emptyPlan.PublishedInfNames.Count `
    'A clean machine must require no package removals.'

Write-Host 'DriverInstallPlan.Tests.ps1: PASS'
