# Dual-format surround execution evidence

This is the durable verification record for
[`2026-08-10-dual-format-surround-core.md`](../superpowers/plans/2026-08-10-dual-format-surround-core.md).
It transcribes the commands and concise observed-output excerpts retained when
Tasks 0–7 were executed. Where a raw excerpt was not captured, the limitation
is stated rather than reconstructed.

The application commands below used the installed Visual Studio MSBuild at:

```text
C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe
```

## Task 0 — preserved system-routing baseline

Commit: `6e6d109` (`feat: preserve system routing reliability and level controls`)

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Retained observed summaries:

```text
Debug: 95/95 passed
Release: 95/95 passed
```

Evidence limit: the retained Task 0 ledger did not preserve the raw MSBuild
summary, so warning/error counts are not asserted here.

## Task 1 — shared contract and eight-channel matrix

Commit: `98ca919` (`feat: route configurable back and side surround channels`)

### RED

Command actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64
```

Retained compiler-failure summary:

```text
C1083: missing VirtualSurroundContract.h
C2660: RouteSurroundFrame did not accept three arguments
C2078: SurroundFrame did not yet have side fields
```

### GREEN

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Retained observed output:

```text
Build succeeded.
    0 Warning(s)
    0 Error(s)
99/99 passed
```

## Task 2 — dual-format capture and live gains

Commit: `dc8cfef` (`feat: capture 5.1 and 7.1 virtual audio`)

### RED

Command actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64
```

Relevant retained compiler output:

```text
Build FAILED.
error C2039: 'surroundFormat': is not a member of 'soundstage::audio::CaptureTelemetry'
error C2039: 'SetSurroundMixLevels': is not a member of 'soundstage::audio::WasapiLoopbackCapture'
    0 Warning(s)
    8 Error(s)
```

### GREEN

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Relevant retained output:

```text
Focused build: Build succeeded; 0 Warning(s); 0 Error(s)
Full Debug build: Build succeeded; 0 Warning(s); 0 Error(s)
PASS LoopbackCapture_DecodesSevenPointOneAndPublishesLiveLevels
PASS LoopbackCapture_DecodesFivePointOneWithoutReadingSideChannels
101/101 passed
```

## Task 3 — persisted Back/Side controls

Commit: `7264638` (`feat: persist and publish surround mix levels`)

### RED

Command actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64
```

Retained compiler-failure summary:

```text
Exit code 1 with 20 compile diagnostics for missing Task 3 APIs:
RouterSettings::{backLevelPercent,sideLevelPercent}
RunConfiguration::surroundMix
EngineController::SetSurroundMixLevels
injectable AudioEngineCoordinator constructor
AudioEngineCoordinator::PostSurroundMixLevels
```

Evidence limit: the report retained the diagnostic count and missing symbols,
not the individual raw compiler lines.

### GREEN

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Retained observed output:

```text
Focused build: exit code 0, 0 warnings, 0 errors
Focused test run: exit code 0, 107/107 passed
Full Debug build: exit code 0, 0 warnings, 0 errors
Fresh full test run: exit code 0, 107/107 passed
```

## Task 4 — endpoint discovery and format status

Commit: `be38c65` (`feat: detect active virtual surround format`)

### RED

Command actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
```

Retained compiler-failure summary:

```text
Exit code 1
C2039: EngineStatus::surroundFormat was absent at the new status assertion
```

### GREEN

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Retained observed output:

```text
Build succeeded.
    0 Warning(s)
    0 Error(s)
108/108 passed
```

## Task 5 — dual-format virtual driver

Commit: `ff238b0` (`feat(driver): offer 5.1 and 7.1 surround formats`)

### Pre-change contract scan

Command actually run:

```powershell
rg -n 'MAX_CHANNELS\s+6|KSAUDIO_SPEAKER_5POINT1|SoundStage Router 5.1' driver/SoundStageRouterVirtualAudio
```

Relevant retained output:

```text
README.md: endpoint named SoundStage Router 5.1
Install-SoundStageRouterDriver.ps1: SoundStage Router 5.1 device created
SoundStageRouterVirtualAudio.inx: Wave and Topology names were SoundStage Router 5.1
speakerwavtable.h: device, host, and offload MAX_CHANNELS values were 6
speakerwavtable.h: three KSAUDIO_SPEAKER_5POINT1 format entries
```

### Post-change contract scan

The same command was rerun. Relevant retained output:

```text
README.md: KSAUDIO_SPEAKER_5POINT1 documented as the alternative format
speakerwavtable.h: three KSAUDIO_SPEAKER_5POINT1 entries remained
```

The retained assertion scan also reported:

```text
PASS: 3 format arrays each advertise exact 7.1-first/5.1-second descriptors.
PASS: device, host, offload, and loopback maxima are 8; mode defaults remain index 0.
PASS: topology and INF names are updated; internal KS symbolic IDs remain.
```

### Debug driver build and INF validation

Commands actually run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1' -Configuration Debug -Platform x64
& 'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\infverif.exe' /v '.\build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\x64\Debug\SoundStageRouterVirtualAudioPackage\SoundStageRouterVirtualAudio.inf'
```

Relevant retained output:

```text
Building ...SoundStageRouterVirtualAudio.sln (Debug|x64)...
SoundStageRouterVirtualAudio.vcxproj -> ...\x64\Debug\SoundStageRouterVirtualAudio.sys
Signability test complete.
Errors:
None
Warnings:
None
Catalog generation complete.
Package output: ...\SoundStageRouterVirtualAudioPackage\x64\Debug\SoundStageRouterVirtualAudioPackage
Running in Verbose
Validating SoundStageRouterVirtualAudio.inf
INF is VALID
Checked 1 INF(s) in 0 m 0 s 4 ms
```

Both commands returned exit code 0. No driver install or uninstall command was
run.

## Task 6 — application controls and format feedback

Commit: `613ab97` (`feat: add 5.1 and 7.1 surround controls`)

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Retained observed output:

```text
Solution build: exit code 0, 0 warnings, 0 errors
Test executable: exit code 0, 108/108 passed
```

Safe manual inspection scope:

- The Debug application was launched without starting routing and without
  installing or changing the driver.
- Front/Rear level edits, Back/Side trackbars at 100%, neutral driver copy, and
  unavailable-format status were visible.
- Both new controls were native `msctls_trackbar32`, visible, ranged 0–100,
  positioned at 100, and used page size 10.
- Side was enabled after switching to test-signal mode.
- The 5.1/7.1 runtime branches were source-reviewed because no driver format
  was activated during this inspection.

## Task 7 — documentation and full verification

Commits: `6b711dd` (`docs: explain dual-format surround routing`) and `3dde100`
(`docs: record dual-format execution evidence`); this record is a subsequent
documentation-only review follow-up.

### Debug application

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Retained observed output:

```text
Build succeeded.
    0 Warning(s)
    0 Error(s)
Time Elapsed 00:00:43.76
108/108 passed
```

### Release application

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Retained observed output:

```text
Build succeeded.
    0 Warning(s)
    0 Error(s)
Time Elapsed 00:00:41.33
108/108 passed
```

### Release driver build and INF validation

Commands actually run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1' -Configuration Release -Platform x64
& 'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\infverif.exe' /w /v '.\build\driver\SoundStageRouterVirtualAudio\obj\SoundStageRouterVirtualAudioPackage\x64\Release\SoundStageRouterVirtualAudioPackage\SoundStageRouterVirtualAudio.inf'
```

Relevant retained output:

```text
Signability test complete.
Errors:
None
Warnings:
None
Catalog generation complete.
Successfully signed: ...\SoundStageRouterVirtualAudio.sys
Successfully signed: ...\SoundStageRouterVirtualAudioPackage\SoundStageRouterVirtualAudio.sys
Successfully signed: ...\SoundStageRouterVirtualAudioPackage\soundstageroutervirtualaudio.cat
Running in Verbose
Running Windows Driver INF check
Validating SoundStageRouterVirtualAudio.inf
INF is VALID
Checked 1 INF(s) in 0 m 0 s 0 ms
```

The application builds, test runs, driver build/signing, and INF validation all
returned exit code 0. No driver installation or uninstallation was performed,
and no hardware playback or hardware acceptance was performed.

## Evidence boundaries

- This record contains concise excerpts, not complete build logs or the full
  native test-name listings.
- Task 0 warning/error counts were not retained and are not inferred.
- Task 3 retained a compiler-failure summary rather than raw diagnostic lines.
- Documentation-only review follow-ups did not rerun heavy builds; they rely on
  the Task 7 results recorded above.

## Final-review fix wave — 2026-08-10

Reviewed base: `73c8407`.

### Focused behavioral and source contracts

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Debug\SoundStageRouter.Tests.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tests\driver\DriverSynchronizationSource.Tests.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tests\driver\DriverInstallPlan.Tests.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tests\driver\DriverSigningSource.Tests.ps1'
```

Observed final results:

```text
123/123 passed
DriverSynchronizationSource.Tests.ps1: PASS
DriverInstallPlan.Tests.ps1: PASS
DriverSigningSource.Tests.ps1: PASS
Install-SoundStageRouterDriver.ps1 parse: PASS (0 errors)
```

No install or uninstall script was executed. The installer checks exercised
only its pure plan module and parsed the entry script without invoking it.

### Clean Debug and Release application verification

Commands actually run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Debug\SoundStageRouter.Tests.exe
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Both rebuilds returned exit code 0 and emitted no warning or error diagnostic.
Both native test executables returned exit code 0 with `123/123 passed`.

### Release driver build, INF, and explicit signatures

Commands actually run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1' -Configuration Release -Platform x64
& 'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\infverif.exe' /w /v $taskInf
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe' verify /pa /all /v $taskStandaloneSys
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe' verify /pa /all /v $taskPackageSys
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe' verify /pa /all /v $taskCat
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe' verify /pa /v /c $taskCat $taskPackageSys
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe' verify /pa /v /c $taskCat $taskInf
```

Relevant final output:

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

Each final SignTool check reported one file/signature successfully verified,
zero warnings, and zero errors. The two SYS files had SHA-256 hash
`734C024B03147D628E02FFD2694E20CD5C2CCA64F1B586146888AC86F5721780`.

An earlier explicit verification run was a genuine RED: the old signing
script appended the SoundStage test signature behind the WDK's automatically
generated, locally untrusted primary signature. SignTool reported an untrusted
root and nonzero exits for `/all` and catalog-member verification. Replacing
the primary signature in the build script, rebuilding, and rerunning the same
matrix produced the final zero-exit results above. No certificate was imported
and no machine trust setting was changed during this fix wave.

### Unverified boundaries

- The driver was not installed or uninstalled.
- No live KS or WASAPI format switch, proposed-format property call, loopback
  capture, endpoint-count check, or AudioSrv stream-lifecycle trace was run.
- No physical speaker playback, acoustic alignment, or hardware acceptance
  was run.
- The package uses an untimestamped local development test certificate; this
  is not production or Microsoft attestation signing.
