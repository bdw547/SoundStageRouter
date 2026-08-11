# Driver Data-Intersection Fix Report

Reviewed base: `a7b2d40`

## Root cause recorded before production edits

The WaveRT pin tables and the callback no longer implement the same contract.

1. Commit `ff238b0` added exact 8-channel and 6-channel
   `KSDATAFORMAT_WAVEFORMATEXTENSIBLE` format descriptors. Commit `79f2ef6`
   then added separate 8-channel and 6-channel `KSDATARANGE_AUDIO` entries and
   strict exact-format validation, but neither commit changed
   `CMiniportWaveRT::DataRangeIntersection`.
2. The callback remains the vendored SysVAD sample implementation: it reports
   `sizeof(KSDATAFORMAT_WAVEFORMATEX)`, checks only equal maximum channel
   counts, and returns `STATUS_NOT_IMPLEMENTED` for a matched pair.
3. The checked-in SysVAD-derived reference paths confirm the inherited split:
   `basetopo.cpp` declines topology intersections, `mintopo.cpp` forwards to
   it, and `EndpointsCommon/minwavert.cpp` only gates equal channel counts
   before delegating to PortCls. The pinned upstream provenance is Microsoft
   Windows-driver-samples commit
   `26a27df80772dbcfd69e6449b671d5c29eb5aedc`; the current official SysVAD
   `minwavert.cpp` retains the same WaveRT callback behavior.
4. Microsoft documents that `STATUS_NOT_IMPLEMENTED` delegates to PortCls'
   default handler, but that handler supports only mono/stereo PCM and only
   `KSDATAFORMAT_WAVEFORMATEX`/`KSDATAFORMAT_DSOUND`; it does not produce a
   `WAVEFORMATEXTENSIBLE` channel mask. Microsoft also documents that a zero
   output length is a size query returning `STATUS_BUFFER_OVERFLOW`, a nonzero
   undersized buffer returns `STATUS_BUFFER_TOO_SMALL`, and multichannel audio
   requires a proprietary intersection handler.
5. The downstream `IdentifySurroundFormat`, device setter, and `NewStream`
   path accept only the complete 104-byte extensible descriptor with exact
   GUIDs, extensible tag/size, channel count, 48-kHz rate, byte rate, block
   alignment, 32-bit container/valid bits, channel mask, and PCM subformat.
   PortCls' delegated result therefore cannot converge on the descriptor that
   the miniport itself requires.

Root cause: the exact multichannel descriptor producer was never upgraded
with the exact descriptor consumers. Delegation that is valid for SysVAD's
mono/stereo sample is incapable of negotiating SoundStage Router's 5.1/7.1
`WAVEFORMATEXTENSIBLE` contract.

Single hypothesis: a shared, WDK-independent pure helper that validates both
audio ranges, implements the PortCls output-size states, and constructs the
one exact 5.1 or 7.1 scalar descriptor will let the kernel callback return a
complete matching extensible descriptor without changing stream, format-gate,
rundown, synchronization, or processing-mode range behavior.

Official references read on 2026-08-10:

- <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/portcls/nf-portcls-iminiport-datarangeintersection>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/output-buffer-size>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/default-data-intersection-handlers>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/data-intersection>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/extensible-wave-format-descriptors>
- <https://github.com/microsoft/Windows-driver-samples/blob/main/audio/sysvad/EndpointsCommon/minwavert.cpp>

## Baseline reproduction

Commands run before test or production edits:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Observed: build exit `0`; native harness exit `0`, `123/123 passed`. The only
existing intersection behavior test covers channel equality, so this baseline
does not exercise or obtain a resultant descriptor.

## TDD RED/GREEN

### Test-first RED

The final host behavior tests were written before the helper API existed. The
first focused build exited `1` with the expected missing-contract diagnostics,
including:

```text
SurroundDriverContractTests.cpp(8,5): error C4430: missing type specifier
SurroundDriverContractTests.cpp(33,29): error C3861:
    'IntersectExactSurroundDataRanges': identifier not found
SurroundDriverContractTests.cpp(35,9): error C2653:
    'DataRangeIntersectionStatus': is not a class or namespace name
```

The minimal type/function scaffold then made the behavior test executable but
intentionally returned no resultant descriptor. The focused test build exited
`0`; the executable exited `1` with `125/129 passed`. The exact primary RED was:

```text
FAIL DriverContract_ProducesExactSevenPointOneIntersection:
result.status != DataRangeIntersectionStatus::Success
```

The companion failures showed that 5.1 was also unavailable, the PortCls size
query status was missing, and the unavailable descriptor was rejected by the
production-used strict validator. This RED called the same pure helper now used
by `CMiniportWaveRT::DataRangeIntersection`; it did not grep source, assert a
mock, or reproduce a separate intersection model.

### Minimal implementation and GREEN

The pure helper was changed to:

- report the 104-byte size query and nonzero undersized-buffer states;
- require audio/PCM/WAVEFORMATEX range identity on both inputs;
- reject malformed range sizes and inverted bit/rate bounds;
- require both ranges to include the 32-bit, 48-kHz point;
- require an exact 6/6 or 8/8 channel pair; and
- construct the exact scalar 5.1 or 7.1 extensible descriptor.

The kernel callback normalizes WDK range structures into that helper, maps its
four outcomes to PortCls status codes, zeroes the caller's complete output
structure, and fills every `KSDATAFORMAT_WAVEFORMATEXTENSIBLE` field. The
focused harness then exited `0` with `129/129 passed`.

### WDK ABI follow-up RED/GREEN

The first Release WDK compile produced a genuine integration RED at the new
ABI assertion:

```text
minwavert.cpp(267,35): error C2607: static assertion failed
```

Investigation showed that the installed WDK's x64
`sizeof(KSDATARANGE_AUDIO)` is 88 bytes, not the initial hand assumption of 84.
The host fixture was changed to the WDK-derived literal first; the focused
harness then failed `126/129` because production still expected 84. Updating
the portable constant to 88 restored `129/129`, and the Release WDK build then
compiled, cataloged, and signed successfully. The paired static assertions keep
the 88-byte input ABI and 104-byte output ABI from silently drifting.

## Files and commits

- `driver/SoundStageRouterVirtualAudio/EndpointsCommon/SoundStageSurroundContract.h`
  - portable normalized range/result types and exact intersection helper;
  - compile-time scalar constants for the WDK range and extensible result sizes.
- `driver/SoundStageRouterVirtualAudio/EndpointsCommon/minwavert.cpp`
  - production callback adapter, status mapping, range normalization, and
    complete extensible output construction.
- `tests/driver/SurroundDriverContractTests.cpp`
  - size query, undersized output, exact 5.1/7.1, channel mismatch,
    client/miniport type/rate/bits/size negatives, and mutation of every
    produced field consumed by the strict validator.
- `.superpowers/sdd/2026-08-10-dual-format-surround-core/driver-intersection-report.md`
  - this evidence record.

The scoped commit SHA is recorded in the final handoff after the report is
added to the otherwise ignored SDD workspace.

## Full verification

### Focused driver contract

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Final result: both exit `0`; `129/129 passed`.

### Full Debug and Release application suites

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Debug\SoundStageRouter.Tests.exe
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64 -nologo -verbosity:minimal
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Final result: both rebuilds exit `0` without warning/error diagnostics; both
test executables exit `0` with `129/129 passed`.

### Existing driver safety contracts

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tests\driver\DriverSynchronizationSource.Tests.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tests\driver\DriverSigningSource.Tests.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tests\driver\DriverInstallPlan.Tests.ps1'
```

Final result: all three exit `0` and report `PASS`. No install or uninstall
entry point was invoked.

### Release WDK build, INF, signatures, and package members

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\driver\SoundStageRouterVirtualAudio\scripts\Build-Driver.ps1' -Configuration Release -Platform x64
infverif.exe /w /v $packageInf
signtool.exe verify /pa /all /v $standaloneSys
signtool.exe verify /pa /all /v $packageSys
signtool.exe verify /pa /all /v $packageCat
signtool.exe verify /pa /v /c $packageCat $packageSys
signtool.exe verify /pa /v /c $packageCat $packageInf
```

Final result:

```text
Release WDK build: exit 0
Signability test: Errors None; Warnings None
INF is VALID
Standalone SYS verification: exit 0; 1 signature; 0 warnings; 0 errors
Packaged SYS verification: exit 0; 1 signature; 0 warnings; 0 errors
CAT verification: exit 0; 1 signature; 0 warnings; 0 errors
Catalog SYS member: exit 0; 1 file; 0 warnings; 0 errors
Catalog INF member: exit 0; 1 file; 0 warnings; 0 errors
Standalone/package SYS SHA-256:
C92D7B9149E116639F236FAC02378E2550CF858A645099F7D6FCB131E8D84249
```

`git diff --check` exits `0`. A final clean-tree check is run after committing
this report.

## Self-review

1. **PortCls contract:** a zero output length returns the required 104 bytes
   with `STATUS_BUFFER_OVERFLOW`; a nonzero length below 104 returns
   `STATUS_BUFFER_TOO_SMALL`; a full buffer receives exactly 104 bytes.
   Non-success paths never partially write the result.
2. **Range validation:** the callback checks null contract pointers, avoids
   reading audio-specific fields unless `FormatSize` is exactly the WDK audio
   range size, and the shared helper rejects malformed sizes, wrong major
   format/subtype/specifier, inverted bounds, ranges excluding 32-bit or
   48-kHz, unsupported channel counts, and 6/8 mismatches on either side.
3. **Exact output:** success zero-initializes and fills the data-format GUIDs,
   extensible tag and extension size, channel count, rate, byte rate, block
   alignment, container/valid bits, mask, and wave PCM subformat. It cannot
   return `STATUS_NOT_IMPLEMENTED` for a supported request.
4. **Consumer convergence:** tests pass the helper's actual resultant scalar
   into `IdentifyExactPcmFormat`, then independently mutate every field that
   validator consumes. Each mutation is rejected, so the producer and strict
   consumer share one enforced contract.
5. **Scope/safety:** no stream lifecycle, reservation, format-switch gate,
   rundown protection, locking, device/mix/default state, loopback path, range
   table, pin pointer, attribute-list, install, or signing implementation was
   changed. Existing synchronization and package source contracts remain green.
6. **Mutation check:** realistic wrong status, size, GUID identity, channels,
   sample point, byte rate, alignment, bit precision, extension size, mask,
   and PCM subformat mutations are caught by at least one host test.

## Concerns and unverified boundaries

- The driver was not installed or uninstalled.
- No live `KSPROPERTY_PIN_DATAINTERSECTION` call was made, so PortCls' runtime
  call ordering and the real caller buffer are not directly observed.
- No live WASAPI endpoint enumeration, format switch, mix-format query,
  loopback capture, AudioSrv lifecycle trace, or hardware playback was run.
- No kernel Driver Verifier or executing concurrency/interleaving test was run;
  the unchanged synchronization/rundown paths retain their existing source and
  host contract coverage.
- The package is signed with an untimestamped local development test
  certificate, not a production/attestation certificate.
