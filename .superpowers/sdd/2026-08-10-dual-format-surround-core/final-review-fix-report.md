# Final Core Review Fix Report

Reviewed base: `73c8407`

Status: **COMPLETE within the authorized source/build/validation boundary.** No installed-runtime architectural blocker was found. Microsoft’s pinned SysVAD reference makes the hardware-engine mix format driver-owned/read-only to PortCls and explicitly notes that a device-format change may require the driver to change its mix format. This driver now converges the exact layouts locally while rejecting changes during stream creation or any allocated stream. Installation, live WASAPI/KS switching, and hardware playback remain explicitly unverified.

## Phase 1 — root-cause tracing before production edits

### Pin negotiation

Observed symptom: a six-channel KS data-range intersection cannot reach the valid six-channel format table.

Trace:

1. `speakerwavtable.h` has exact six- and eight-channel `KSDATAFORMAT_WAVEFORMATEXTENSIBLE` entries for device, host, and offload lists.
2. Each host/offload/loopback pin descriptor points to only one `KSDATARANGE_AUDIO`, whose `MaximumChannels` is 8.
3. `CMiniportWaveRT::DataRangeIntersection` intentionally exact-compares client and driver `MaximumChannels` and returns `STATUS_NO_MATCH` when they differ.
4. The pinned SysVAD reference documents the same invariant: every supported exact channel count needs its own data range.

Root cause: the dual-format change expanded the exact format arrays but did not expand any pin’s range-pointer list. The class handler never gets a 6/6 range pair.

Hypothesis to test: distinct 8-channel-first and 6-channel ranges on host, offload, and loopback, paired with the existing exact-match intersection rule, make both layouts negotiable while rejecting cross-count and unsupported counts.

### Device, mix, processing-mode, and loopback format state

Observed symptom: selecting the 5.1 device format leaves the reported mix format and raw loopback framing at 7.1.

Trace:

1. `CMiniportWaveRT::Init` copies supported format element zero (7.1) into both `m_pDeviceFormat` and `m_pMixFormat`.
2. `IMiniportAudioEngineNode::SetDeviceFormat` only copies the caller into `m_pDeviceFormat`; it performs no exact validation and does not touch `m_pMixFormat`.
3. `GetMixFormat` continues returning `m_pMixFormat`.
4. Every host/offload processing-mode default still points to format element zero.
5. `WriteLoopbackMix` raw-copies host bytes, and loopback `WriteBytes` raw-copies those bytes into the loopback stream. There is no channel conversion.
6. Stream timing trusts `nAvgBytesPerSec` during stream initialization, before the current validator checks that field.
7. The pinned SysVAD reference says the driver determines the hardware-engine mix format and that changing device format may require changing the mix format. It offers no separate mix setter. Its `m_DeviceFormatsAndModesLock` is the established serialization primitive for format/mode tables, while stream objects register only after format validation.

Root causes: one-sided state mutation, static 7.1 processing defaults, no shared selected-layout state for future host/loopback streams, no format-change/stream-creation exclusion, and incomplete descriptor validation.

Safety boundary: because the loopback path performs byte-for-byte copies, host and loopback must always use the selected mix layout. The minimal safe policy is to reject a format switch while any system/offload/loopback stream or stream creation exists, update device/mix/default/selected pin layouts while holding the format lock, then reset and zero the loopback ring under its lock before allowing a new stream creation. This is stricter than merely checking `KSSTATE_STOP` and avoids retaining old-format stream buffers.

Hypothesis to test: one host-buildable transition function can prove 7.1→5.1→7.1 convergence and busy rejection; the miniport can use that same function while performing the WDK copies under its locks.

### Stopped/current-format UI state

Observed symptom: startup shows an unavailable badge and enables Side for an unknown system format; Stop leaves the last running format stale; the restart action remains generic.

Trace:

1. `AudioEndpoints.cpp` detects a `VirtualSurroundFormat` but collapses it into `virtualContractValid`, discarding the enum.
2. `EngineController::Start` marks capture ready after `Prepare` without copying `CaptureTelemetry`; `Tick` is the first publication point and runs only in `Running`.
3. `EngineController::Stop` tears capture down without clearing `status_.surroundFormat`.
4. `AppWindow::RenderEngineStatus` uses only engine status, so stopped enumeration cannot supply the badge.
5. `UpdateSurroundControlAvailability` enables Side for every value except known 5.1, which makes unknown system mode unsafe.
6. The UI has no persistent “Used when Windows is set to 7.1.” control and resets the primary action to generic copy.

Root cause: run-scoped capture telemetry is incorrectly the sole source for a device-scoped format, and UI derivation lacks an explicit unknown-safe model.

Hypothesis to test: retain the enum on the enumerated endpoint, use it whenever live telemetry is unavailable, publish telemetry immediately after Prepare, clear engine format on teardown, and derive badge/hint/Side/restart copy through a pure state function where system Side is enabled only for known 7.1.

### Integrated-driver review addendum (before follow-up production edits)

The first integrated driver commit built successfully, but an independent
source review found four synchronization defects that compilation cannot
detect:

1. Several routines in the pageable `PAGE` section directly acquired
   `m_SharedFormatStateLock`. `KeAcquireSpinLock` raises IRQL to
   `DISPATCH_LEVEL`, so executing the surrounding pageable instructions can
   bugcheck. The format getters also copied into caller-owned output while the
   IRQL was raised. Root cause: the new lock was added inline rather than
   confined to resident helpers that snapshot or commit state and release the
   lock before pageable callers continue.
2. Stream-array registration/removal was locked, but `UpdateDrmRights` and
   `SetLoopbackProtection` still read and dereferenced those weak pointers
   without the same lifetime protocol. Root cause: writer synchronization was
   added without auditing every reader. A reader can be preempted after loading
   a pointer and resume after stream teardown.
3. Stream capacity was checked in `ValidateStreamCreate` and reserved later by
   `StreamCreated`. Two concurrent creations can pass a one-stream limit before
   either increments it. Root cause: creation gating counted operations for
   format-switch exclusion but did not reserve the per-pin resource atomically.
4. The host test's `TrySwitchSharedFormat` modeled the intended transition but
   production manually reimplemented the decision. Root cause: the pure helper
   described outcome state, not the production-used gate/transition decision.

Follow-up hypotheses: resident lock helpers can make each critical section
self-contained; copying content IDs while locked and taking rundown-protected
stream references before calling pageable methods can close the weak-pointer race;
atomic per-pin reservation with rollback can close the check/act window; and a
production-used pure transition decision can keep the harness coupled to the
miniport branch.

## Reference paths read

- Pinned Microsoft SysVAD commit `26a27df80772dbcfd69e6449b671d5c29eb5aedc`:
  - `audio/sysvad/EndpointsCommon/MiniportAudioEngineNode.cpp`
  - `audio/sysvad/EndpointsCommon/minwavert.cpp`
  - `audio/sysvad/EndpointsCommon/minwavertstream.cpp`
- Vendored corresponding paths under `driver/SoundStageRouterVirtualAudio/EndpointsCommon/`, including proposed-format/default-mode lookup, stream registration, state transitions, timing, and loopback copy paths.

## RED evidence

### Driver contract harness

Command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64 -nologo -verbosity:minimal
```

Observed exit code: `1`.

Observed diagnostic:

```text
SurroundDriverContractTests.cpp(2,10): error C1083: Cannot open include file:
'../../driver/SoundStageRouterVirtualAudio/EndpointsCommon/SoundStageSurroundContract.h':
No such file or directory
```

This is the expected RED: the behavioral harness names the portable contract
that production will consume, and that contract does not exist at the reviewed
base.

### Additional genuine RED results

All production behavior changes were preceded by a failing test or validation
command. Retained failure excerpts were:

- Processing-range adjacency test: missing contract symbols, followed by a WDK
  compile failure until `speakerwavtable.h` included the contract directly.
- Exact kernel helper integration: the first WDK compile pulled user-mode
  `<stdint.h>` into the kernel build and failed warnings-as-errors; the portable
  contract was changed to built-in unsigned types.
- UI state: `C1083`, missing `SurroundUiState.h`.
- Engine lifecycle: `115/117 passed`; immediate post-Prepare format publication
  and stale format after Stop both failed.
- Legacy settings: `117/118 passed`; `FrontLevelPercent=+50` did not retain the
  legacy parse-and-clamp behavior.
- Decoder: missing `DecodeVirtualSurroundFrame` (`C3861`).
- Installer: the pure test could not import `DriverInstallPlan.psm1`; later
  concurrency/idempotency REDs observed a near-match devnode incorrectly
  counted (`expected 2, observed 3`) and a missing DevCon success helper.
- Synchronization follow-up: host compilation failed on missing
  `StreamCapacityState`/`TryReserveStreamSlot`, and the source safety contract
  reported `Pageable audio-engine properties must delegate locking to resident
  helpers.`
- Signing: the first explicit `/all` and catalog-member verification returned
  exit 1 because `/as` left the locally untrusted automatic WDK signature as
  the catalog primary. `DriverSigningSource.Tests.ps1` then failed with
  `Build signing must replace ... not append behind it.`

The intermediate WDK and signature failures were not treated as success; each
was corrected and rerun before the full verification below.

## GREEN and full verification evidence

### Focused contracts

Final observed results:

```text
123/123 passed
DriverSynchronizationSource.Tests.ps1: PASS
DriverInstallPlan.Tests.ps1: PASS
DriverSigningSource.Tests.ps1: PASS
Install-SoundStageRouterDriver.ps1 parse: PASS (0 errors)
```

The 123 native tests include exact 6/8 range intersection, every exposed exact
descriptor field, 7.1→5.1→7.1 convergence, busy rejection, ring reset, atomic
capacity reservation, stopped UI derivation, capture lifecycle, decoder
channel order, isolated format negatives, and legacy settings migration.

### Full application builds

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Debug\SoundStageRouter.Tests.exe
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Observed: both rebuilds returned exit 0 without warning/error diagnostics;
both executables returned exit 0 with `123/123 passed`.

### Release driver, INF, and signatures

Commands actually run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1' -Configuration Release -Platform x64
infverif.exe /w /v $taskInf
signtool.exe verify /pa /all /v $taskStandaloneSys
signtool.exe verify /pa /all /v $taskPackageSys
signtool.exe verify /pa /all /v $taskCat
signtool.exe verify /pa /v /c $taskCat $taskPackageSys
signtool.exe verify /pa /v /c $taskCat $taskInf
```

Observed final output:

```text
Signability test complete.
Errors:
None
Warnings:
None
Catalog generation complete.
INF is VALID
INFVERIF_EXIT=0
STANDALONE_SYS_VERIFY_EXIT=0
PACKAGE_SYS_VERIFY_EXIT=0
CAT_VERIFY_EXIT=0
CATALOG_SYS_MEMBER_VERIFY_EXIT=0
CATALOG_INF_MEMBER_VERIFY_EXIT=0
```

Each final SignTool command reported one successful verification, zero warnings,
and zero errors. The standalone and packaged SYS SHA-256 was
`734C024B03147D628E02FFD2694E20CD5C2CCA64F1B586146888AC86F5721780`.

## Files and commits

- `79f2ef6 fix(driver): synchronize dual surround formats`
  - portable driver contract and host tests;
  - exact 8/6 pin ranges and attribute adjacency;
  - exact format validation, dynamic proposed defaults, shared format/ring
    transition, and stream-layout enforcement.
- `4d66f23 fix(ui): refresh stopped surround format state`
  - endpoint format retention, stopped discovery refresh, unknown-safe Side
    availability, persistent 5.1 hint/restart copy, and capture lifecycle state.
- `6763a8f test(audio): cover surround decoding and settings migration`
  - production-used exact decoder, one-hot channel coverage, isolated negatives,
    and restored Front/Rear parse-and-clamp behavior.
- `30e7419 fix(driver): make package installation idempotent`
  - deterministic all-instance/all-package cleanup plan, exact root matching,
    DevCon reboot-success handling, exactly-one postcondition, documentation,
    and `DriverVer` source bump to `08/10/2026, 1.1.0.0`.
- `3fdc052 fix(driver): harden shared format synchronization`
  - resident lock helpers, atomic capacity reservation/rollback, weak-pointer
    rundown, production-used transition contract, and synchronization guards.
- `953f058 fix(driver): replace package test signatures`
  - replace rather than append the local test signature so SYS, CAT, and
    catalog-member verification use the intended primary signature.

The implementation touches the files named in the plan plus
`tests/driver/DriverSynchronizationSource.Tests.ps1` and
`tests/driver/DriverSigningSource.Tests.ps1`. The UTF-16LE INX retained its BOM;
its one exact `DriverVer` replacement used an encoding-preserving PowerShell
operation because `apply_patch` rejects non-UTF-8 input. All other edits used
`apply_patch`.

## Self-review

1. **Six-channel negotiation:** host, offload, and loopback each expose distinct
   8-first and 6-second ranges. Every flagged processing range is immediately
   followed by its attribute list. Exact 8/8 and 6/6 intersection is tested.
2. **Shared mix/loopback state:** `SetDeviceFormat` uses the production-tested
   transition under a strict no-creation/no-allocated-stream gate, zeroes and
   resets the byte ring, and commits device, mix, selected host/offload/loopback
   layout together. `PROPOSEDATAFORMAT2` derives the current per-instance
   default. Timer deletion and queued-DPC flushing precede the final allocation
   decrement.
3. **Exact validation:** one validator requires exact format size, GUIDs,
   extensible tag/cbSize, channels, rate, byte rate, alignment, container and
   valid bits, mask, and PCM subformat. It is used before stream timing and by
   the device setter.
4. **Stopped UI:** enumeration retains the exact enum, refreshes at startup,
   before start, and on stopped/faulted transition; capture publishes after
   Prepare; teardown clears live state; discovery supplies the stopped badge.
   System Side is enabled only for known 7.1; test mode remains enabled.
5. **Idempotent installation:** the pure plan selects every exact matching root
   devnode plus attached/stale package, orders unique OEM INF names, removes
   them before one creation, accepts DevCon reboot-required success, polls for
   exactly one signed devnode, and does nothing under `-WhatIf`. No installer
   command was executed during verification.
6. **Legacy parsing:** only new Back/Side keys remain strict; Front/Rear retain
   signed decimal parsing and load-time clamp. `+50`, `101`, and `2000` are
   covered without the adjustment flag.
7. **Decoder/tests:** FL/FR/FC/LFE/BL/BR/SL/SR one-hot order is covered; 5.1
   leaves Side at zero; wrong bits/alignment/mask/count/rate/float and kernel
   byte-rate/size fields are isolated.
8. **Synchronization review:** an independent re-review traced success,
   allocation/Init failure, concurrent create, switch-first, teardown-first,
   and rundown interleavings. It found no remaining blocking issue, counter
   leak/double decrement, format-gate gap, lock inversion, or rundown deadlock.

`git diff --check` is part of the final clean-tree check recorded after this
report is committed.

## Unverified boundaries and concerns

- No driver installation or uninstallation was performed.
- No installed KS property call, Windows Sound format switch, live
  `IAudioClient::GetMixFormat`, loopback channel-count capture, or AudioSrv
  lifecycle trace was performed.
- No physical playback, acoustic measurement, or hardware acceptance was
  performed.
- The strict safety policy rejects a switch while any stream exists, including
  `KSSTATE_STOP`, because the stream caches immutable framing/DMA state and
  `SetFormat` is unsupported. If AudioSrv retains a STOP stream during its
  format-change sequence, Windows may receive `STATUS_DEVICE_BUSY`; only the
  excluded installed runtime experiment can determine that ordering.
- The existing ring copies the system-render stream, not offload render data;
  this fix does not claim universal offload-loopback behavior.
- The synchronization harness is deterministic source/host coverage, not an
  executing kernel interleaving or Driver Verifier run.
- The locally verified certificate is an untimestamped development test
  certificate, not production/attestation signing.
