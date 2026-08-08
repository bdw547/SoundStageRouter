# SoundStage Router Virtual Audio

This directory vendors a trimmed SysVAD-derived kernel driver slice that exposes one render endpoint named **SoundStage Router 5.1** for shared-mode WASAPI playback plus loopback capture.

## Scope and upstream provenance

- Upstream project: `microsoft/Windows-driver-samples`
- Upstream URL: <https://github.com/microsoft/Windows-driver-samples>
- Exact upstream commit: `26a27df80772dbcfd69e6449b671d5c29eb5aedc`
- Vendored subtree: `audio/sysvad`
- Upstream license: Microsoft Public License (MS-PL), copied verbatim to `THIRD_PARTY_NOTICES\LICENSE-MS-PL.txt`

## What was kept vs. removed

Kept:
- Core SysVAD adapter/common code needed to register PortCls miniports.
- Shared WaveRT/topology code for the internal speaker endpoint.
- One render endpoint pair (`WaveSoundStageRouter51` + `TopologySoundStageRouter51`).
- Loopback support and the existing offload pin wiring.

Removed or made unreachable:
- HDMI, SPDIF, headphone, mic, mic array, Bluetooth HFP, USB sideband, A2DP sideband, keyword detector endpoint registration, APO packaging, and keyword-detector packaging.
- Capture endpoint registration and all non-speaker miniport arrays in `minipairs.h`.

Judgement call: the offload render pin was **kept**. Removing it cleanly from SysVAD's WaveRT render path required more invasive surgery than keeping the upstream render-engine topology intact. The driver still exposes only one shared-mode format on host/offload/loopback pins, so this does not change the user-facing prototype goal.

## Format contract

The only shared-mode wave format exposed by the speaker endpoint is:

- 48000 Hz
- 32-bit IEEE float
- 6 channels
- `WAVEFORMATEXTENSIBLE`
- `dwChannelMask = KSAUDIO_SPEAKER_5POINT1` (`FL|FR|FC|LFE|BL|BR`, mask `0x3F`)

This satisfies the system-wide routing prototype requirement because Windows can open the virtual device in shared mode and WASAPI loopback capture can read the post-mix render stream without any custom IOCTL path.

## Standard OS identity that intentionally stayed unchanged

The PortCls/KS proxy CLSID in the INF stays at the standard Windows value `{17CCA71B-ECD7-11D0-B908-00A0C9223196}`. That GUID is OS-defined infrastructure, not sample-specific product identity, so the driver-specific identity changes were applied to the hardware ID, endpoint names, INF/provider strings, product/property GUIDs, and Visual Studio project GUIDs instead.

## Build

Manual command used in this repo:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' .\driver\SoundStageRouterVirtualAudio\SoundStageRouterVirtualAudio.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m /nologo /verbosity:minimal
```

Automated build + local test-signing:

```powershell
.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1
```

Primary output package directory:

```text
build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\x64\Release\SoundStageRouterVirtualAudioPackage
```

That folder should contain:
- `SoundStageRouterVirtualAudio.inf`
- `SoundStageRouterVirtualAudio.sys`
- `soundstageroutervirtualaudio.cat`

The build script also exports the public test certificate to:

```text
driver\SoundStageRouterVirtualAudio\certs\SoundStageRouterVirtualAudio-Test.cer
```

## INF validation

Command used:

```powershell
& 'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\infverif.exe' /w /v .\build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\x64\Release\SoundStageRouterVirtualAudioPackage\SoundStageRouterVirtualAudio.inf
```

Observed result:

```text
INF is VALID
Checked 1 INF(s) in 0 m 0 s 1 ms
```

Note: `infverif /provider 'SoundStage Router Project'` crashed on this machine's WDK build, so the plain `/w /v` invocation above is the validated path used here.

## Install and uninstall

Do **not** run these from a normal shell; both require admin elevation.

Install (stages trust + `pnputil /add-driver /install` only when `-Force` is supplied):

```powershell
Start-Process PowerShell -Verb RunAs -ArgumentList '-ExecutionPolicy Bypass -File .\driver\SoundStageRouterVirtualAudio\scripts\Install-SoundStageRouterDriver.ps1 -Force'
```

Uninstall (finds the installed `oemNN.inf` entry, then calls `pnputil /delete-driver /uninstall /force` only when `-Force` is supplied):

```powershell
Start-Process PowerShell -Verb RunAs -ArgumentList '-ExecutionPolicy Bypass -File .\driver\SoundStageRouterVirtualAudio\scripts\Uninstall-SoundStageRouterDriver.ps1 -Force'
```

Both scripts intentionally stop early if:
- the shell is not elevated, or
- Windows TESTSIGNING boot mode is not enabled.

To enable TESTSIGNING for a development machine:

```powershell
bcdedit /set testsigning on
```

A reboot is required after changing that setting. This repo does **not** enable TESTSIGNING automatically.

`devcon` remains optional for diagnostics (`devcon status`, `devcon rescan`, `devcon remove`), but `pnputil` is the primary supported install/uninstall path in these scripts.

## Production-signing limitation

This package is for development and prototyping only. Real deployment requires a properly issued signing certificate plus Microsoft Hardware Dev Center attestation or WHQL signing. End-user systems generally should not run with TESTSIGNING enabled.
