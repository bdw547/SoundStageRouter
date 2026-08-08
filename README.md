# SoundStage Router

SoundStage Router is a native Windows prototype for synchronizing two physical
audio outputs as one front/rear listening layout.

## Synchronized test playback milestone

The application generates deterministic test audio and renders it to two
distinct shared-mode WASAPI endpoints. It supports Front and Rear roles,
paired and alternating clicks, role-specific tones, live manual delay from
0â€“2000 ms, endpoint-clock telemetry, and bounded drift correction. Rear is the
default clock reference; manual delay remains the authority for acoustic
alignment.

This milestone does **not** capture or reroute system audio, open microphone or
loopback capture, decode media, or install a virtual audio device. A virtual
5.1/7.1 endpoint, system capture, and automatic microphone calibration remain
future work.

Settings remain in
`%LOCALAPPDATA%\SoundStageRouter\routing.ini`, including endpoint assignments,
delays, and the last test pattern.

## Build and test

Use Visual Studio 2026 with PlatformToolset `v145`, Windows SDK
`10.0.28000.0`, and x64 only:

```powershell
msbuild SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
msbuild SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
```

The application executables are written to
`build\Debug\SoundStageRouter.exe` and
`build\Release\SoundStageRouter.exe`.

## Offline acoustic analyzer

`SoundStageAlignmentAnalyzer.exe` evaluates an externally recorded 48 kHz
PCM16 or float32 WAV containing paired clicks:

```powershell
.\build\Release\SoundStageAlignmentAnalyzer.exe .\recording.wav
```

It returns `0` for PASS, `2` for FAIL, and `1` for invalid input or analysis
errors. PASS requires at least 20 detected pairs and a 95th-percentile absolute
front/rear onset difference no greater than 10.00 ms. The tool only reads an
existing WAV; it never opens a microphone or render endpoint. Follow the
[two-device hardware acceptance protocol](docs/testing/hardware-acceptance.md)
for the required start and 30-minute recordings.

The native tests cover deterministic DSP, format conversion, long-running
clock simulation, engine lifecycle, cancellation, lock-free telemetry, and
WASAPI fault injection without requiring audio hardware. Final acceptance
still requires the documented Realtek/Bluetooth two-device smoke test.

## Design records

- [Approved synchronized playback design](docs/superpowers/specs/2026-08-08-synchronized-test-playback-design.md)
- [Synchronized playback implementation plan](docs/superpowers/plans/2026-08-08-synchronized-test-playback.md)
- [Separate acoustic alignment analyzer plan](docs/superpowers/plans/2026-08-08-acoustic-alignment-analyzer.md)

## Virtual 5.1 driver (kernel slice)

A separate SysVAD-derived kernel driver slice now lives under `driver\SoundStageRouterVirtualAudio`. It is intentionally isolated from the existing user-mode app and currently targets development-only, test-signed installation of a single root-enumerated virtual render endpoint named **SoundStage Router 5.1**.

See `driver\SoundStageRouterVirtualAudio\README.md` for exact build, `infverif`, install, uninstall, signing, and upstream-license details.
