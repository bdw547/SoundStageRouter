# Dual-Format Surround Final-Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve every Critical, Important, and Minor finding from the final review of the 5.1/7.1 surround core.

**Architecture:** Put the exact driver layout rules in a small kernel-safe, host-buildable contract used by both the WaveRT miniport and deterministic tests. Keep Windows format selection authoritative, synchronize device/mix/default-mode/loopback state while no streams can be created, and model stopped UI presentation as a pure state function fed by current endpoint enumeration or live capture telemetry.

**Tech Stack:** C++20 Win32/WASAPI application and native test harness, C++ WDK WaveRT/SysVAD driver, PowerShell packaging scripts, Visual Studio 18/WDK 10.0.28000.0.

## Global Constraints

- One endpoint named **SoundStage Router Surround** supports exactly 48 kHz 32-bit 5.1 (`0x003F`) and 7.1 (`0x063F`), with 7.1 first/default.
- Do not install or uninstall the driver during automated verification.
- Preserve event-driven render/capture and the real loopback ring-buffer reliability changes.
- Do not claim installed WASAPI switching or hardware playback without actually running it.
- Every production behavior change starts with a genuine failing behavioral test or host-buildable harness.

---

### Task 1: Exact driver negotiation and shared format state

**Files:**
- Create: `driver/SoundStageRouterVirtualAudio/EndpointsCommon/SoundStageSurroundContract.h`
- Create: `tests/driver/SurroundDriverContractTests.cpp`
- Modify: `SoundStageRouter.Tests.vcxproj`
- Modify: `driver/SoundStageRouterVirtualAudio/EndpointsCommon/speakerwavtable.h`
- Modify: `driver/SoundStageRouterVirtualAudio/EndpointsCommon/minwavert.h`
- Modify: `driver/SoundStageRouterVirtualAudio/EndpointsCommon/minwavert.cpp`
- Modify: `driver/SoundStageRouterVirtualAudio/EndpointsCommon/MiniportAudioEngineNode.cpp`

**Interfaces:**
- Produces exact scalar-format identification, exact-channel range intersection, a 7.1/5.1 shared-state transition, and miniport serialization that prevents stream creation during a format switch.

- [x] Write host tests for 8/8 and 6/6 intersections, every independently malformed descriptor field, 7.1→5.1→7.1 device/mix/host/offload/loopback transitions, busy rejection, and loopback position reset.
- [x] Run the focused test build and record the missing-contract RED failure.
- [x] Implement the portable contract and use it in the driver validator and stream creation path.
- [x] Add distinct 8-channel-first and 6-channel host/offload/loopback data ranges, keeping processing-mode attribute pointers.
- [x] Validate and atomically converge device, mix, processing-mode defaults, selected stream layouts, and loopback state while no streams or stream creations are active.
- [x] Run the focused tests and record GREEN.
- [x] Commit the driver contract fix.

### Task 2: Current-format application and UI state

**Files:**
- Create: `src/audio/SurroundUiState.h`
- Modify: `tests/audio/AudioTypesTests.cpp`
- Modify: `SoundStageRouter.Tests.vcxproj`
- Modify: `src/AudioEndpoints.h`
- Modify: `src/AudioEndpoints.cpp`
- Modify: `src/audio/EngineController.cpp`
- Modify: `tests/audio/EngineControllerTests.cpp`
- Modify: `src/AppWindow.h`
- Modify: `src/AppWindow.cpp`

**Interfaces:**
- Produces endpoint-retained `VirtualSurroundFormat`, immediate post-Prepare status, cleared stopped engine state, and pure stopped/running UI copy/availability.

- [x] Write failing tests for endpoint-derived stopped state, known-format restart copy, 5.1/unknown Side disabling, test-mode Side enabling, immediate Prepare publication, and Stop clearing.
- [x] Run focused RED.
- [x] Preserve and refresh the endpoint enum, refresh before start, publish capture telemetry immediately after Prepare, and clear stale state on Stop/fault teardown.
- [x] Render the persistent Side hint and format-specific restart action from the pure UI state.
- [x] Run focused GREEN and commit.

### Task 3: Settings migration and decoder coverage

**Files:**
- Modify: `tests/RouterSettingsTests.cpp`
- Modify: `src/RouterSettings.cpp`
- Modify: `tests/audio/LoopbackCaptureTests.cpp`
- Modify: `src/audio/LoopbackCapture.h`
- Modify: `src/audio/LoopbackCapture.cpp`

**Interfaces:**
- Restores legacy Front/Rear parsing while leaving Back/Side strict and exposes a tested exact channel-order decoder used by capture.

- [x] Add failing migration cases for `+50`, `101`, and `2000` without adjustment.
- [x] Add one-hot FL/FR/FC/LFE/BL/BR/SL/SR decoder tests through a missing pure decoder API and record RED.
- [x] Restore legacy Front/Rear parse-then-clamp and extract/use the decoder.
- [x] Run focused GREEN and commit.

### Task 4: Idempotent package installation

**Files:**
- Create: `driver/SoundStageRouterVirtualAudio/scripts/DriverInstallPlan.psm1`
- Create: `tests/driver/DriverInstallPlan.Tests.ps1`
- Modify: `driver/SoundStageRouterVirtualAudio/scripts/Install-SoundStageRouterDriver.ps1`
- Modify: `driver/SoundStageRouterVirtualAudio/TabletAudioSample/SoundStageRouterVirtualAudio.inx`
- Modify: `driver/SoundStageRouterVirtualAudio/README.md`

**Interfaces:**
- Produces a deterministic plan that removes every matching devnode and old published package before one install and expects exactly one resulting instance.

- [x] Add a failing pure PowerShell plan test for zero, one, and duplicate existing instances.
- [x] Run RED without invoking install/uninstall commands.
- [x] Implement and consume the plan, assert the post-install instance count, and bump `DriverVer` beyond 2026-08-08/1.0.0.0.
- [x] Run GREEN and commit.

### Task 5: Verification and durable evidence

**Files:**
- Modify: `docs/testing/dual-format-execution-evidence.md`
- Modify: `.superpowers/sdd/2026-08-10-dual-format-surround-core/final-review-fix-report.md`

- [x] Rebuild and run Debug application tests.
- [x] Rebuild and run Release application tests.
- [x] Build and sign the Release driver.
- [x] Run INF validation.
- [x] Verify the SYS embedded signature and CAT/package-member signatures explicitly.
- [x] Run diff checks, self-review every finding, and record unverified installed/live/hardware boundaries.
- [x] Update durable evidence only with observed commands/output and commit documentation.
- [x] Confirm the worktree is clean.
