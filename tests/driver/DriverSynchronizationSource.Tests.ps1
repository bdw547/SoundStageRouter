$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$miniportPath = Join-Path $repoRoot `
    'driver\SoundStageRouterVirtualAudio\EndpointsCommon\minwavert.cpp'
$enginePath = Join-Path $repoRoot `
    'driver\SoundStageRouterVirtualAudio\EndpointsCommon\MiniportAudioEngineNode.cpp'
$miniport = Get-Content -Raw $miniportPath
$engine = Get-Content -Raw $enginePath

function Get-FunctionBody {
    param([string]$Source, [string]$Start, [string]$Next)
    $startIndex = $Source.IndexOf($Start, [StringComparison]::Ordinal)
    if ($startIndex -lt 0) { throw "Function marker not found: $Start" }
    if (-not $Next) { return $Source.Substring($startIndex) }
    $nextIndex = $Source.IndexOf(
        $Next, $startIndex + $Start.Length, [StringComparison]::Ordinal)
    if ($nextIndex -lt 0) { throw "Next marker not found: $Next" }
    return $Source.Substring($startIndex, $nextIndex - $startIndex)
}

function Assert-NotContains {
    param([string]$Text, [string]$Value, [string]$Message)
    if ($Text.IndexOf($Value, [StringComparison]::Ordinal) -ge 0) {
        throw $Message
    }
}

function Assert-Contains {
    param([string]$Text, [string]$Value, [string]$Message)
    if ($Text.IndexOf($Value, [StringComparison]::Ordinal) -lt 0) {
        throw $Message
    }
}

function Assert-SharedLockOnlyInResidentSections {
    param([string]$Source, [string]$Path)
    $pageable = $false
    foreach ($line in ($Source -split "`r?`n")) {
        if ($line.Trim() -eq '#pragma code_seg("PAGE")') {
            $pageable = $true
        }
        elseif ($line.Trim() -eq '#pragma code_seg()') {
            $pageable = $false
        }
        elseif ($pageable -and
                $line -match 'KeAcquireSpinLock\(&m_SharedFormatStateLock') {
            throw "Pageable shared-state spin-lock acquisition in $Path."
        }
    }
}

Assert-SharedLockOnlyInResidentSections $miniport $miniportPath
Assert-SharedLockOnlyInResidentSections $engine $enginePath

$getMix = Get-FunctionBody $engine 'CMiniportWaveRT::GetMixFormat' `
    'CMiniportWaveRT::GetDeviceFormat'
$getDevice = Get-FunctionBody $engine 'CMiniportWaveRT::GetDeviceFormat' `
    'CMiniportWaveRT::SetDeviceFormat'
$setDevice = Get-FunctionBody $engine 'CMiniportWaveRT::SetDeviceFormat' `
    'CMiniportWaveRT::GetSupportedDeviceFormats'
foreach ($body in @($getMix, $getDevice, $setDevice)) {
    Assert-NotContains $body 'KeAcquireSpinLock' `
        'Pageable audio-engine properties must delegate locking to resident helpers.'
}
Assert-Contains $setDevice 'TryBeginSharedFormatSwitch' `
    'The production setter must delegate the atomic transition decision.'
Assert-Contains ($miniport + $engine) 'TrySwitchSharedFormat(' `
    'The driver must use the transition contract exercised by tests.'

$newStream = Get-FunctionBody $miniport 'CMiniportWaveRT::NewStream' `
    'CMiniportWaveRT::NonDelegatingQueryInterface'
Assert-Contains $newStream 'BeginStreamCreation(Pin, Capture)' `
    'NewStream must atomically reserve capacity with the format-switch gate.'
Assert-NotContains $newStream 'ValidateStreamCreate(Pin, Capture)' `
    'NewStream must not separate capacity validation from reservation.'

$drm = Get-FunctionBody $miniport 'CMiniportWaveRT::UpdateDrmRights' `
    'CMiniportWaveRT::AllocStreamAudioModules'
Assert-NotContains $drm 'm_SystemStreams[i]->' `
    'DRM aggregation must not dereference weak system-stream pointers unlocked.'
Assert-NotContains $drm 'm_OffloadStreams[i]->' `
    'DRM aggregation must not dereference weak offload-stream pointers unlocked.'

$protection = Get-FunctionBody $engine `
    'CMiniportWaveRT::SetLoopbackProtection' ''
Assert-NotContains $protection 'm_LoopbackStreams[i]->' `
    'Loopback protection must call only strong referenced stream snapshots.'

Write-Host 'DriverSynchronizationSource.Tests.ps1: PASS'
