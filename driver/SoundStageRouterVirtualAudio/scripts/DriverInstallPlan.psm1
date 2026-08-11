Set-StrictMode -Version Latest

function New-SoundStageDriverInstallPlan {
    [CmdletBinding()]
    param(
        [AllowEmptyCollection()]
        [object[]]$Devices = @(),

        [AllowEmptyCollection()]
        [object[]]$Packages = @()
    )

    $hardwareIdPrefix = 'ROOT\SOUNDSTAGEROUTERVIRTUALAUDIO'
    $originalInfName = 'SoundStageRouterVirtualAudio.inf'

    $matchingDevices = @($Devices | Where-Object {
        $_.PSObject.Properties.Name -contains 'DeviceID' -and
        [string]$_.DeviceID -like "$hardwareIdPrefix\*"
    })

    $deviceInstanceIds = @($matchingDevices |
        ForEach-Object { [string]$_.DeviceID } |
        Where-Object { $_ } |
        Sort-Object -Unique)

    $attachedPackages = @($matchingDevices |
        ForEach-Object {
            if ($_.PSObject.Properties.Name -contains 'InfName') {
                [string]$_.InfName
            }
        })

    $stalePackages = @($Packages | Where-Object {
        if (-not ($_.PSObject.Properties.Name -contains
                  'OriginalFileName')) {
            return $false
        }
        [IO.Path]::GetFileName([string]$_.OriginalFileName) -ieq
            $originalInfName
    } | ForEach-Object {
        if ($_.PSObject.Properties.Name -contains 'Driver') {
            [string]$_.Driver
        }
        elseif ($_.PSObject.Properties.Name -contains 'PublishedName') {
            [string]$_.PublishedName
        }
    })

    $publishedInfNames = @(($attachedPackages + $stalePackages) |
        Where-Object { $_ -match '^oem\d+\.inf$' } |
        Sort-Object -Unique)

    [pscustomobject]@{
        DeviceInstanceIds = $deviceInstanceIds
        PublishedInfNames = $publishedInfNames
        ExpectedFinalDeviceCount = 1
    }
}

function Test-SoundStageDevConSuccess {
    [CmdletBinding()]
    param([int]$ExitCode)

    # DevCon uses 1 for success when a reboot is required.
    return $ExitCode -eq 0 -or $ExitCode -eq 1
}

Export-ModuleMember -Function @(
    'New-SoundStageDriverInstallPlan',
    'Test-SoundStageDevConSuccess'
)
