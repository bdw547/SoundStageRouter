$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildScriptPath = Join-Path $repoRoot `
    'driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1'
$buildScript = Get-Content -Raw $buildScriptPath

if ($buildScript -match '&\s+\$signtool\s+sign\s+/as\b') {
    throw 'Build signing must replace the untrusted automatic WDK primary signature, not append behind it.'
}
if ($buildScript -notmatch
    '&\s+\$signtool\s+sign\s+/fd\s+SHA256\s+/sha1') {
    throw 'Build signing must select SHA-256 and the intended test certificate.'
}

Write-Host 'DriverSigningSource.Tests.ps1: PASS'
