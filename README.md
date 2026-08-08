# SoundStage Router

SoundStage Router is a native Windows prototype for synchronizing two physical
audio outputs as one front/rear listening layout.

## System-wide routing

The app loopback-captures the Windows mix from the installed **SoundStage
Router 5.1** render endpoint and sends one shared 48 kHz master timeline to two
physical shared-mode WASAPI outputs. It never captures another endpoint and
does not use custom driver IOCTLs.

After installing the driver using its separate instructions:

1. Open Windows sound settings and make **SoundStage Router 5.1** the default
   output.
2. Start SoundStage Router and select **System audio (virtual 5.1)**.
3. Select distinct physical Front and Rear outputs. The virtual endpoint is
   deliberately unavailable in these selectors to prevent feedback.
4. Choose rear fill if desired: **Off** (default), **Duplicate (-6 dB)**, or
   **Ambient difference**.
5. Click **Start Routing**, then play audio in any Windows application.

The app must stay running for routed audio to reach the physical devices.
Disconnecting the virtual input or either physical output stops the run with a
visible fault; restart is always manual.

The deterministic 5.1-to-stereo matrix is:

- Front L = clamp(FL + 0.707 FC + 0.5 LFE)
- Front R = clamp(FR + 0.707 FC + 0.5 LFE)
- Rear L/R = clamp(BL/BR)

Rear fill is applied only when both native rear channels are silent. Ambient
uses `0.5 * (FL - FR)` and its inverse. The test-signal mode remains available
for setup, click alignment, role tones, and live 0–2000 ms delay adjustment.

Settings remain in
`%LOCALAPPDATA%\SoundStageRouter\routing.ini`, including endpoint assignments,
delays, mode, rear-fill choice, and the last test pattern.

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

The native tests cover the channel matrix, dual-reader ring buffer, silent
loopback packets, strict virtual format validation, deterministic DSP, format
conversion, clock simulation, engine lifecycle/cancellation, telemetry, and
WASAPI fault injection without requiring audio hardware.

## Latency and limitations

End-to-end latency includes the virtual shared-mode mix buffer, the bounded
master buffer, each physical shared-mode buffer, configured delay, resampling,
and any Bluetooth buffering. Typical wired routing is tens of milliseconds;
Bluetooth can add 100–300 ms or more. Manual delay compensates relative arrival
time, not total latency. The app does not decode media, calibrate with a
microphone, support exclusive-mode applications, or keep routing after it
exits. Applications that bypass the Windows default endpoint are not captured.

## Design records

- [Approved synchronized playback design](docs/superpowers/specs/2026-08-08-synchronized-test-playback-design.md)
- [Synchronized playback implementation plan](docs/superpowers/plans/2026-08-08-synchronized-test-playback.md)
- [Separate acoustic alignment analyzer plan](docs/superpowers/plans/2026-08-08-acoustic-alignment-analyzer.md)

## Virtual 5.1 driver (kernel slice)

A separate SysVAD-derived kernel driver slice lives under
`driver\SoundStageRouterVirtualAudio`. It targets development-only, test-signed
installation of one root-enumerated virtual render endpoint named
**SoundStage Router 5.1**. Building the app does not install the driver.

See `driver\SoundStageRouterVirtualAudio\README.md` for exact build, `infverif`, install, uninstall, signing, and upstream-license details.
