# Dual-Format Surround Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one SoundStage Router virtual endpoint support selectable Windows 5.1 and 7.1 formats, then route Back and Side channels into the Bluetooth chair with independent saved 0–100% controls.

**Architecture:** A shared pure format contract identifies the exact six- and eight-channel layouts. Loopback capture decodes either layout into one eight-field `SurroundFrame`, and `ChannelRouter` applies live Back/Side gains before the existing two-output synchronized pipeline. The SysVAD-derived endpoint advertises both device formats with 7.1 as its default.

**Tech Stack:** C++20, Win32, shared-mode WASAPI loopback/render, WAVEFORMATEXTENSIBLE, SysVAD/WDK, MSBuild v145, Windows SDK 10.0.28000.0, custom native test harness.

## Global Constraints

- Keep one virtual endpoint named `SoundStage Router Surround`.
- Windows selects 5.1 or 7.1; the application detects it automatically.
- Support exactly 48 kHz float32 loopback with mask `0x003F`/6 channels or `0x063F`/8 channels.
- Keep 7.1 as the driver default and retain 5.1 as a selectable supported format.
- Map Side Left plus Back Left to the left chair speaker and Side Right plus Back Right to the right chair speaker.
- Back and Side levels are linked stereo, 0–100%, default 100%, with no fixed −3 dB attenuation.
- Detect native surround before applying user gains; only the existing output limiter may reduce overloads.
- Preserve delay, drift correction, rear fill, test signals, cancellation, and existing physical Front/Rear master levels.
- Do not allocate, lock, log, access files, or call UI/COM APIs in per-frame routing code.
- Build x64 only with PlatformToolset `v145` and Windows SDK `10.0.28000.0`.

---

### Task 0: Checkpoint the preserved system-routing work

**Files:**
- Existing modified files reported by `git status --short` before this plan starts

**Interfaces:**
- Consumes: the imported `feature/system-wide-routing` branch plus its preserved uncommitted driver event-mode and physical-output level changes
- Produces: a clean, tested baseline commit before dual-format edits begin

- [ ] **Step 1: Confirm the preserved patch is exactly the imported work**

Run:

```powershell
git status --short
git diff --check
git diff --stat
```

Expected: 19 modified files, no untracked production files, and no whitespace errors.

- [ ] **Step 2: Build and run the existing Debug tests**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Expected: build succeeds and the test executable reports zero failures.

- [ ] **Step 3: Build and run the existing Release tests**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Expected: build succeeds and the test executable reports zero failures.

- [ ] **Step 4: Commit only the preserved baseline files**

```powershell
git add driver src tests
git commit -m "feat: preserve system routing reliability and level controls"
```

### Task 1: Shared surround format contract and eight-channel matrix

**Files:**
- Create: `src/audio/VirtualSurroundContract.h`
- Modify: `src/audio/AudioTypes.h`
- Modify: `src/audio/ChannelRouter.h`
- Modify: `src/audio/ChannelRouter.cpp`
- Modify: `tests/audio/AudioTypesTests.cpp`
- Modify: `tests/audio/ChannelRouterTests.cpp`

**Interfaces:**
- Consumes: `MasterSampleRate`, `RearFillMode`, and the existing `RoleFrame`
- Produces: `VirtualSurroundFormat`, `DetectVirtualSurroundFormat(...)`, `SurroundMixLevels`, the eight-field `SurroundFrame`, and `RouteSurroundFrame(const SurroundFrame&, RearFillMode, SurroundMixLevels)`

- [ ] **Step 1: Write failing contract and matrix tests**

Add expectations equivalent to:

```cpp
EXPECT_EQ(DetectVirtualSurroundFormat(
    {48000, 6, 32, 24, 0x003Fu, true}),
    VirtualSurroundFormat::FivePointOne);
EXPECT_EQ(DetectVirtualSurroundFormat(
    {48000, 8, 32, 32, 0x063Fu, true}),
    VirtualSurroundFormat::SevenPointOne);
EXPECT_EQ(DetectVirtualSurroundFormat(
    {48000, 8, 32, 32, 0x003Fu, true}),
    VirtualSurroundFormat::Unsupported);

const SurroundFrame input{
    0, 0, 0, 0, 0.8f, -0.6f, 0.4f, 0.2f};
const RoleFrame full = RouteSurroundFrame(
    input, RearFillMode::Off, {1.0f, 1.0f});
EXPECT_NEAR(full.rear.left, 1.0, 1e-7);
EXPECT_NEAR(full.rear.right, -0.4, 1e-7);

const RoleFrame balanced = RouteSurroundFrame(
    input, RearFillMode::Off, {0.5f, 0.25f});
EXPECT_NEAR(balanced.rear.left, 0.5, 1e-7);
EXPECT_NEAR(balanced.rear.right, -0.25, 1e-7);
```

Also prove that Side-only input suppresses rear fill even when `sideGain` is zero, that both gains clamp to `[0,1]`, and that the original Front matrix expectations remain unchanged.

- [ ] **Step 2: Run the focused tests and verify RED**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64
```

Expected: compilation fails because the new contract, fields, and signature do not exist.

- [ ] **Step 3: Implement the pure format contract**

Create this public shape in `VirtualSurroundContract.h`:

```cpp
enum class VirtualSurroundFormat : std::uint8_t
{
    Unsupported,
    FivePointOne,
    SevenPointOne
};

struct VirtualFormatDescription
{
    std::uint32_t sampleRate;
    std::uint16_t channels;
    std::uint16_t bitsPerSample;
    std::uint16_t blockAlign;
    std::uint32_t channelMask;
    bool floatingPoint;
};

[[nodiscard]] constexpr VirtualSurroundFormat
DetectVirtualSurroundFormat(const VirtualFormatDescription value) noexcept;
```

Return `FivePointOne` only for `{48000,6,32,24,0x003F,true}`, `SevenPointOne` only for `{48000,8,32,32,0x063F,true}`, and `Unsupported` otherwise.

- [ ] **Step 4: Expand the frame and implement the gain-aware matrix**

Add `sideLeft` and `sideRight` after the Back fields. Add:

```cpp
struct SurroundMixLevels
{
    float back = 1.0f;
    float side = 1.0f;
};
```

Clamp each gain once, detect native rear content from BL/BR/SL/SR before gain, compute `back * BL + side * SL` and its right-channel equivalent, and then use the existing `Limit` function. Preserve the current Front and rear-fill formulas exactly.

- [ ] **Step 5: Build and run all Debug tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```powershell
git add src/audio/VirtualSurroundContract.h src/audio/AudioTypes.h src/audio/ChannelRouter.h src/audio/ChannelRouter.cpp tests/audio/AudioTypesTests.cpp tests/audio/ChannelRouterTests.cpp
git commit -m "feat: route configurable back and side surround channels"
```

### Task 2: Dual-format loopback decoding and live surround gains

**Files:**
- Modify: `src/audio/LoopbackCapture.h`
- Modify: `src/audio/LoopbackCapture.cpp`
- Modify: `tests/audio/LoopbackCaptureTests.cpp`

**Interfaces:**
- Consumes: `VirtualSurroundFormat`, `VirtualFormatDescription`, `SurroundMixLevels`, and the gain-aware `RouteSurroundFrame`
- Produces: `CaptureTelemetry::surroundFormat`, `ILoopbackCapture::SetSurroundMixLevels(SurroundMixLevels)`, and safe six/eight-channel packet decoding

- [ ] **Step 1: Write failing loopback tests**

Add tests that make the fake backend report both exact formats, then feed one frame per format:

```cpp
observed->format = {48000, 8, 32, 32, 0x063F, true};
observed->samples = {0, 0, 0, 0, 0.25f, -0.25f, 0.5f, -0.5f};
EXPECT_EQ(capture.Snapshot().surroundFormat,
          VirtualSurroundFormat::SevenPointOne);
```

Read the routed frame from the ring and expect Rear `{0.75,-0.75}` at 100/100. Call `SetSurroundMixLevels({0.0f,0.5f})`, feed another frame, and expect `{0.25,-0.25}`. Add a six-channel case that yields Side zero and a wrong-mask case that fails preparation with `VirtualEndpointFormatCode`.

- [ ] **Step 2: Build to verify RED**

Run the Debug test-project build. Expected: compilation fails on the missing telemetry field and setter.

- [ ] **Step 3: Replace the fixed 5.1 contract**

Rename the friendly-name constant to `L"SoundStage Router Surround"`. Make `IsVirtualCaptureFormat` call `DetectVirtualSurroundFormat` and accept either supported result. Store the detected result after `GetMixFormat` and publish it in telemetry.

- [ ] **Step 4: Decode packets using the detected channel stride**

Use `format.channels` instead of the literal `6`. Always load indices 0–5; load 6–7 only for `SevenPointOne`. Apply the packet endpoint master gain to all populated fields. Do not read Side samples from a six-channel span.

- [ ] **Step 5: Add lock-free live level publication**

Store clamped Back and Side gains as two `std::atomic<float>` members in `WasapiLoopbackCapture::Impl`. `SetSurroundMixLevels` publishes them with release ordering. The worker loads them with acquire ordering immediately before `RouteSurroundFrame`.

- [ ] **Step 6: Run Debug tests and commit**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
git add src/audio/LoopbackCapture.h src/audio/LoopbackCapture.cpp tests/audio/LoopbackCaptureTests.cpp
git commit -m "feat: capture 5.1 and 7.1 virtual audio"
```

### Task 3: Persist and publish Back/Side controls

**Files:**
- Modify: `src/RouterSettings.h`
- Modify: `src/RouterSettings.cpp`
- Modify: `src/audio/AudioTypes.h`
- Modify: `src/audio/EngineController.h`
- Modify: `src/audio/EngineController.cpp`
- Modify: `src/audio/AudioEngineCoordinator.h`
- Modify: `src/audio/AudioEngineCoordinator.cpp`
- Modify: `tests/RouterSettingsTests.cpp`
- Modify: `tests/audio/EngineControllerTests.cpp`
- Modify: `tests/audio/AudioEngineCoordinatorTests.cpp`

**Interfaces:**
- Consumes: `ILoopbackCapture::SetSurroundMixLevels` and existing coordinator command serialization
- Produces: `RouterSettings::{backLevelPercent,sideLevelPercent}`, `RunConfiguration::surroundMix`, `EngineController::SetSurroundMixLevels`, and `AudioEngineCoordinator::PostSurroundMixLevels`

- [ ] **Step 1: Write failing settings tests**

Verify a file with no new keys loads both values as 100 without setting `loadAdjustedValues`; round-trip `BackLevelPercent=40` and `SideLevelPercent=75`; and verify `-1`, `101`, and non-numeric values fall back to 100 and mark adjusted values.

- [ ] **Step 2: Write failing engine/coordinator tests**

Extend the fake capture with `lastMixLevels`. Start system routing with `{0.4f,0.75f}`, expect `Prepare` to receive those values, then post `{0.2f,1.0f}` and expect the live setter exactly once without restarting capture.

- [ ] **Step 3: Build to verify RED**

Run the Debug test-project build. Expected: compilation fails for missing settings, configuration, and command APIs.

- [ ] **Step 4: Implement settings parsing separately from delay parsing**

Add a `ParsePercent` helper accepting only decimal integers 0–100. Use keys `BackLevelPercent` and `SideLevelPercent`, defaults `100`, and the migration behavior from the spec. Do not reuse `ParseDelay`, whose valid range is 0–2000.

- [ ] **Step 5: Thread the mix through startup and live commands**

Add `SurroundMixLevels surroundMix{}` to `RunConfiguration`. During system-audio startup, publish it to capture before `Start`. Add coordinator `CommandType::SurroundMix` carrying the two floats and dispatch it to `EngineController::SetSurroundMixLevels`. In test-signal mode the setter is a safe no-op because capture is absent.

- [ ] **Step 6: Run all Debug tests and commit**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
git add src/RouterSettings.h src/RouterSettings.cpp src/audio/AudioTypes.h src/audio/EngineController.h src/audio/EngineController.cpp src/audio/AudioEngineCoordinator.h src/audio/AudioEngineCoordinator.cpp tests/RouterSettingsTests.cpp tests/audio/EngineControllerTests.cpp tests/audio/AudioEngineCoordinatorTests.cpp
git commit -m "feat: persist and publish surround mix levels"
```

### Task 4: Endpoint discovery, detected-format status, and format faults

**Files:**
- Modify: `src/AudioEndpoints.cpp`
- Modify: `src/audio/AudioTypes.h`
- Modify: `src/audio/EngineController.cpp`
- Modify: `src/audio/LoopbackCapture.cpp`
- Modify: `tests/audio/EngineControllerTests.cpp`
- Modify: `tests/audio/LoopbackCaptureTests.cpp`

**Interfaces:**
- Consumes: the shared virtual format detector and capture telemetry
- Produces: `EngineStatus::surroundFormat`, dual-format virtual contract validation, and neutral user-facing fault text

- [ ] **Step 1: Add failing status and fault tests**

Make fake capture telemetry report `SevenPointOne` and assert `EngineStatus::surroundFormat` matches after `Tick`. Add expectations that missing/duplicate/wrong-format messages name `SoundStage Router Surround`, not `5.1`.

- [ ] **Step 2: Build to verify RED**

Expected: compilation fails because `EngineStatus::surroundFormat` is absent and old strings remain.

- [ ] **Step 3: Reuse the exact shared contract in endpoint enumeration**

Set `isVirtualEndpoint` for the neutral interface name or existing WDM identity. Set `virtualContractValid` when `DetectVirtualSurroundFormat` returns either supported value. Continue excluding any `isVirtualEndpoint` item from Front/Rear choices.

- [ ] **Step 4: Publish format and improve capture fault text**

Copy `CaptureTelemetry::surroundFormat` into `EngineStatus` in system-audio mode. Replace fixed 5.1 fault copy with the neutral endpoint name and explicit 5.1/7.1 at 48 kHz guidance for `VirtualEndpointFormatCode`.

- [ ] **Step 5: Run all Debug tests and commit**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
git add src/AudioEndpoints.cpp src/audio/AudioTypes.h src/audio/EngineController.cpp src/audio/LoopbackCapture.cpp tests/audio/EngineControllerTests.cpp tests/audio/LoopbackCaptureTests.cpp
git commit -m "feat: detect active virtual surround format"
```

### Task 5: Advertise 5.1 and 7.1 in the virtual driver

**Files:**
- Modify: `driver/SoundStageRouterVirtualAudio/EndpointsCommon/speakerwavtable.h`
- Modify: `driver/SoundStageRouterVirtualAudio/EndpointsCommon/speakertoptable.h`
- Modify: `driver/SoundStageRouterVirtualAudio/TabletAudioSample/SoundStageRouterVirtualAudio.inx`
- Modify: `driver/SoundStageRouterVirtualAudio/README.md`
- Modify: `driver/SoundStageRouterVirtualAudio/scripts/Install-SoundStageRouterDriver.ps1`

**Interfaces:**
- Consumes: SysVAD host/offload/loopback format-table conventions
- Produces: one neutral endpoint whose default descriptor is 8-channel `0x063F` and whose second supported descriptor is 6-channel `0x003F`

- [ ] **Step 1: Record the pre-change driver failure**

Run:

```powershell
rg -n 'MAX_CHANNELS\s+6|KSAUDIO_SPEAKER_5POINT1|SoundStage Router 5.1' driver/SoundStageRouterVirtualAudio
```

Expected: the format tables, topology, INF, docs, and install message still prove a fixed 5.1 contract.

- [ ] **Step 2: Add 7.1-first and 5.1-second format entries**

For every engine/host/offload supported-format array, make index 0:

```cpp
WAVE_FORMAT_EXTENSIBLE, 8, 48000, 1536000, 32, 32,
KSAUDIO_SPEAKER_7POINT1_SURROUND,
KSDATAFORMAT_SUBTYPE_PCM
```

and index 1 the current six-channel `{1152000,24,0x003F}` descriptor. Set device, host, offload, and loopback maximum channels to 8. Keep processing-mode defaults pointed at index 0 and keep both entries in each pin's supported list.

- [ ] **Step 3: Update topology and names**

Advertise the maximum physical jack layout as `KSAUDIO_SPEAKER_7POINT1_SURROUND`. Change Wave and Topology friendly names and user-visible script messages to `SoundStage Router Surround`. Keep existing internal KS symbolic identifiers so an unnecessary PnP identity migration is not introduced.

- [ ] **Step 4: Build and validate the driver package**

Run from an elevated developer environment:

```powershell
& '.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1' -Configuration Debug -Platform x64
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\infverif.exe' /v '.\build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\x64\Debug\SoundStageRouterVirtualAudioPackage\SoundStageRouterVirtualAudio.inf'
```

Expected: driver build and INF verification succeed.

- [ ] **Step 5: Re-scan the source contract**

Run the Task 5 Step 1 `rg` command. Expected: fixed-5.1 prose/names are gone; remaining `KSAUDIO_SPEAKER_5POINT1` occurrences are only the intended second supported format.

- [ ] **Step 6: Commit**

```powershell
git add driver/SoundStageRouterVirtualAudio
git commit -m "feat(driver): offer 5.1 and 7.1 surround formats"
```

### Task 6: Functional application controls and detected-format feedback

**Files:**
- Modify: `src/AppWindow.h`
- Modify: `src/AppWindow.cpp`
- Modify: `src/main.cpp`
- Modify: `SoundStageRouter.vcxproj`

**Interfaces:**
- Consumes: saved Back/Side percentages, `RunConfiguration::surroundMix`, coordinator live commands, and `EngineStatus::surroundFormat`
- Produces: live Back Level and Side Level trackbars, a detected-format badge, and neutral 5.1/7.1 copy

- [ ] **Step 1: Initialize common controls and create stable command IDs**

Initialize `ICC_BAR_CLASSES` in `main.cpp`. Add Back/Side labels, trackbars, percentage values, and a format-status control in `AppWindow`. Configure both trackbars for 0–100 with tick frequency 10 and page size 10.

- [ ] **Step 2: Load, save, and start with the selected values**

Populate trackbar positions from `RouterSettings`. Save them as integers. In `BuildRunConfiguration`, convert them to `{back / 100.0f, side / 100.0f}`. Keep both at 100 for missing legacy keys.

- [ ] **Step 3: Publish live slider changes**

Handle `WM_HSCROLL` only for the two surround trackbars. Update their percentage labels, save the settings, and call:

```cpp
coordinator_->PostSurroundMixLevels({
    backPercent / 100.0f,
    sidePercent / 100.0f});
```

Do not restart routing.

- [ ] **Step 4: Render the detected format and mode-specific availability**

Display `5.1 detected`, `7.1 detected`, or `Surround format unavailable` from `EngineStatus`. Disable Side Level in system-audio 5.1 mode while keeping it visible. Enable it for 7.1 and test-signal setup. Replace all user-facing `SoundStage Router 5.1` copy with the neutral endpoint name.

- [ ] **Step 5: Build and run Debug tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Expected: application and tests build and all automated tests pass.

- [ ] **Step 6: Commit**

```powershell
git add src/AppWindow.h src/AppWindow.cpp src/main.cpp SoundStageRouter.vcxproj
git commit -m "feat: add 5.1 and 7.1 surround controls"
```

### Task 7: Core documentation and full verification

**Files:**
- Modify: `README.md`
- Modify: `docs/testing/hardware-acceptance.md`
- Modify: `docs/superpowers/plans/2026-08-10-dual-format-surround-core.md`

**Interfaces:**
- Consumes: completed driver and application behavior
- Produces: exact setup, switching, matrix, control, reinstall, and acceptance instructions

- [ ] **Step 1: Update user and hardware documentation**

Document the neutral endpoint, Windows-owned format selection, exact 5.1 and 7.1 masks/order, Back/Side formulas, 0–100% controls, no fixed attenuation, old-driver removal, safe stop on format change, and the seven-step hardware procedure from the design spec.

- [ ] **Step 2: Run repository consistency checks**

```powershell
rg -n 'SoundStage Router 5.1|virtual 5.1|fixed 5.1' README.md docs src driver tests
git diff --check
```

Expected: any remaining 5.1-specific text describes the selectable format, migration, or test case; no stale endpoint name or whitespace error remains.

- [ ] **Step 3: Rebuild and test Debug**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Expected: build succeeds and all tests pass.

- [ ] **Step 4: Rebuild and test Release**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Expected: build succeeds and all tests pass.

- [ ] **Step 5: Build and validate the Release driver**

```powershell
& '.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1' -Configuration Release -Platform x64
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\infverif.exe' /v '.\build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\x64\Release\SoundStageRouterVirtualAudioPackage\SoundStageRouterVirtualAudio.inf'
```

Expected: driver build, signing, and INF verification succeed.

- [ ] **Step 6: Commit documentation and plan checkmarks**

```powershell
git add README.md docs/testing/hardware-acceptance.md docs/superpowers/plans/2026-08-10-dual-format-surround-core.md
git commit -m "docs: explain dual-format surround routing"
```
