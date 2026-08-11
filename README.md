# SoundStage Router

SoundStage Router is a native Windows prototype for synchronizing two physical
audio outputs as one front/rear listening layout.

## System-wide routing

The app loopback-captures the Windows mix from the installed **SoundStage
Router Surround** render endpoint and sends one shared 48 kHz master timeline to two
physical shared-mode WASAPI outputs. It never captures another endpoint and
does not use custom driver IOCTLs.

After installing the driver using its separate instructions:

1. Stop routing before changing the endpoint's speaker format. In Windows sound
   settings, choose 5.1 or 7.1 at 48 kHz for **SoundStage Router Surround**;
   Windows owns this choice, and the app detects it automatically.
2. Make **SoundStage Router Surround** the Windows default output.
3. Start SoundStage Router and select **System audio (virtual surround)**. Check
   for **5.1 detected** or **7.1 detected** in the app.
4. Select distinct physical **Front** and **Chair** outputs. The virtual
   endpoint is deliberately unavailable in these selectors to prevent feedback.
5. Choose rear fill if desired: **Off** (default), **Duplicate (-6 dB)**, or
   **Ambient difference**.
6. Set the linked-stereo **Back Level** and **Side Level** controls from 0–100%,
   then click **Start Routing** and play audio in any Windows application.

The app must stay running for routed audio to reach the physical devices.
Disconnecting the virtual input or either physical output stops the run with a
visible fault; restart is always manual. Changing the Windows speaker format
during a run also stops routing safely. After Windows finishes the change,
start routing again and confirm the newly detected format. There is no
automatic restart.

## Command Deck dashboard

The window is a single dark dashboard. The header shows the detected Windows
format (**5.1 detected** or **7.1 detected**) and the route state (**Ready**,
**Routing**, **Setup required**, or a fault). The synchronization line reads
**Aligned** once the outputs are locked together and **Synchronizing
outputs...** while alignment settles.

- The **Front** (soundbar / monitor) and **Chair** (Bluetooth headrest) cards
  each hold that output's device selector plus its **Delay (ms)** and
  **Level (%)** fields. Delays stay adjustable live while routing; Level is
  set while stopped.
- The **Chair Mix** card holds the linked-stereo **Back Level** and
  **Side Level** controls (0–100%, default 100%). Both apply live during
  routing. In 5.1, **Side Level** stays visible but disabled with a short
  explanation, because 5.1 carries no side channels.
- One primary action starts or stops routing. If Windows changes the speaker
  format, routing stops safely, an amber message explains what happened, and
  the action relabels to **Restart in 5.1** or **Restart in 7.1**. If a
  physical output disconnects, the message names the affected card.
- **Technical details** expands the collapsed diagnostics: per-output sample
  rate, channel count, buffer, delay, underruns, and reference/follower role,
  plus clock correction in ppm, capture overflow/underrun counts, and the last
  fault code. It starts collapsed on every launch.

The virtual endpoint supports these exact shared-mode loopback layouts:

| Windows format | Channels | Mask | Channel order |
|---|---:|---:|---|
| 5.1 | 6 | `0x003F` (`KSAUDIO_SPEAKER_5POINT1`) | FL, FR, FC, LFE, BL, BR |
| 7.1 | 8 | `0x063F` (`KSAUDIO_SPEAKER_7POINT1_SURROUND`) | FL, FR, FC, LFE, BL, BR, SL, SR |

Both are 48 kHz with 32-bit containers; the Windows shared audio engine
provides float32 loopback samples. The driver initially defaults to 7.1 while
5.1 remains selectable.

The deterministic surround-to-physical matrix is:

- Front L = clamp(FL + 0.707 FC + 0.5 LFE)
- Front R = clamp(FR + 0.707 FC + 0.5 LFE)
- Chair L = clamp((Back Level / 100) × BL + (Side Level / 100) × SL)
- Chair R = clamp((Back Level / 100) × BR + (Side Level / 100) × SR)

Back and Side are independently adjustable linked-stereo contributions. They
default to 100%, have no fixed attenuation, and are limited only if their sum
exceeds the valid sample range. In 5.1, Side samples are zero and Side Level is
disabled because it has no signal effect. The physical Front and Chair
**Level (%)** fields remain separate output master gains, set before a run.

Rear fill is applied only when all native BL, BR, SL, and SR samples are
silent, as determined before the Back/Side controls are applied. Setting a
native contribution to 0% therefore does not make fill appear. Ambient uses
`0.5 * (FL - FR)` and its inverse. The test-signal mode remains available for
setup, click alignment, role tones, and live 0–2000 ms delay adjustment.

Settings remain in
`%LOCALAPPDATA%\SoundStageRouter\routing.ini`, including endpoint assignments,
delays, mode, rear-fill choice, physical output levels, Back/Side levels, and
the last test pattern. Older settings files default the new levels to 100%.

If an older development driver still exposes **SoundStage Router 5.1**, stop
the app and remove that instance before installing the rebuilt package; the
new endpoint name and format list do not take effect in-place. Use the elevated
uninstall and install commands in the
[driver instructions](driver/SoundStageRouterVirtualAudio/README.md#install-and-uninstall).

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

## Virtual surround driver (kernel slice)

A separate SysVAD-derived kernel driver slice lives under
`driver\SoundStageRouterVirtualAudio`. It targets development-only, test-signed
installation of one root-enumerated virtual render endpoint named
**SoundStage Router Surround**. It offers Windows-selectable 5.1 and 7.1
formats and initially defaults to 7.1. Building the app does not install the
driver.

See `driver\SoundStageRouterVirtualAudio\README.md` for exact build, `infverif`, install, uninstall, signing, and upstream-license details.
