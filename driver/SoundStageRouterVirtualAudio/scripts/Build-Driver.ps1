[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$driverRoot = Split-Path -Parent $scriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $driverRoot)
$solutionPath = Join-Path $driverRoot 'SoundStageRouterVirtualAudio.sln'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
$signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'
$certOutput = Join-Path $driverRoot 'certs\SoundStageRouterVirtualAudio-Test.cer'
$packageDir = Join-Path $repoRoot ('build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\{0}\{1}\SoundStageRouterVirtualAudioPackage' -f $Platform, $Configuration)
$driverDir = Join-Path $repoRoot ('build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudio\{0}\{1}' -f $Platform, $Configuration)
$subject = 'CN=SoundStageRouterVirtualAudio Test'

Write-Host "Building $solutionPath ($Configuration|$Platform)..."
& $msbuild $solutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /m /nologo /verbosity:minimal
if (-not $?) { throw 'MSBuild failed.' }

$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    Write-Host 'Creating local CurrentUser test-signing certificate...'
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation 'Cert:\CurrentUser\My' -FriendlyName 'SoundStage Router Virtual Audio Test Signing' -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(2)
}

Export-Certificate -Cert $cert -FilePath $certOutput -Force | Out-Null

$filesToSign = @(
    (Join-Path $driverDir 'SoundStageRouterVirtualAudio.sys'),
    (Join-Path $packageDir 'SoundStageRouterVirtualAudio.sys'),
    (Join-Path $packageDir 'soundstageroutervirtualaudio.cat')
) | Get-Unique

foreach ($file in $filesToSign) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Expected build output not found: $file"
    }

    Write-Host "Signing $file"
    & $signtool sign /as /fd SHA256 /sha1 $cert.Thumbprint /s My $file | Out-Host
    if (-not $?) { throw "signtool failed for $file" }
}

Write-Host ''
Write-Host "Package output: $packageDir"
Write-Host "Certificate:   $certOutput"
Write-Host "Thumbprint:    $($cert.Thumbprint)"
