# Synchronized Test Playback Design

**Date:** 2026-08-08  
**Status:** Approved architecture; written specification awaiting final review  
**Product:** SoundStage Router

## Purpose

SoundStage Router will prove that two independent Windows audio render endpoints can act as one front/rear listening layout while remaining perceptually synchronized. The target hardware is a Realtek-connected soundbar and subwoofer used as the front output, plus Bluetooth speakers built into a chair headrest used as the rear output.

This milestone generates its own test audio. It does not capture or replace normal Windows system audio. That boundary isolates the hardest technical risk—independent device latency and clock drift—before adding a virtual surround driver or application-audio routing.

## Success criteria

The milestone is successful when it can:

- Assign two distinct, active Windows render endpoints to front and rear roles.
- Play front-only, rear-only, alternating, and paired click/tone test patterns.
- Apply an independently adjustable delay of 0–2000 ms to each role.
- Let the listener manually align paired test sounds while playback is running.
- Hold the aligned outputs within 10 ms of one another for 30 minutes on the target Realtek/Bluetooth setup.
- Stop cleanly on device invalidation or repeated render failure without crashing or leaving one endpoint playing indefinitely.
- Persist endpoint assignments and manual delay values per Windows user.

The 10 ms target is an end-to-end hardware observation measured at the listening position. Internal telemetry alone cannot prove it because Bluetooth firmware and speaker electronics may add latency after the Windows endpoint clock.

## Scope

### Included

- Shared-mode WASAPI output to one front and one rear endpoint.
- A common 48 kHz, 32-bit floating-point stereo master timeline.
- Test-signal generation and role-specific channel mapping.
- Manual delay controls that can be adjusted during playback.
- Endpoint-clock telemetry and bounded drift correction.
- Clear playback, alignment, health, and error status in the existing native Win32 interface.
- Unit, simulation, fault-injection, and target-hardware verification.

### Excluded

- A virtual Windows 5.1 or 7.1 playback device.
- Capturing or rerouting system, game, browser, or movie audio.
- Decoding media files or surround bitstreams.
- Automatic microphone-based acoustic calibration.
- More than two simultaneously rendered endpoints in the user interface.
- Exclusive-mode WASAPI.

Core engine types must use role/endpoint collections rather than hard-coded front/rear internals so a later milestone can add more speakers without replacing the timing model.

## User experience

The existing endpoint selection and delay controls remain the starting point. The playback milestone adds:

- A test-pattern selector with `Paired clicks`, `Alternating clicks`, `Front only`, and `Rear only` choices.
- `Start test` and `Stop` controls.
- Live front and rear delay controls, constrained to 0–2000 ms.
- A compact status area showing each endpoint's state, the current reference endpoint, estimated relative drift, underrun count, and whether clock-based correction is active.

The normal alignment flow is:

1. Select the Realtek endpoint as front and the Bluetooth headrest endpoint as rear.
2. Start paired clicks.
3. Increase the faster endpoint's delay until the clicks are heard as one event at the listening position.
4. Leave playback running while the drift controller maintains the relationship.
5. Save the resulting endpoint assignments and delays.

Starting is rejected if either endpoint is unavailable or if the same endpoint is assigned to both roles. Changing endpoint assignment while playing requires stopping and restarting. Delay changes are allowed during playback and are applied smoothly.

## Architecture

### Data flow

```text
TestPatternGenerator (48 kHz float master frames)
                  |
             MasterTimeline
             /            \
     front role          rear role
          |                  |
     ChannelMap          ChannelMap
          |                  |
      DelayLine           DelayLine
          |                  |
  AdaptiveResampler   AdaptiveResampler
          |                  |
  EndpointConverter   EndpointConverter
          |                  |
  WASAPI Endpoint A   WASAPI Endpoint B
          \                  /
             ClockTelemetry
                  |
             SyncController
```

The generator creates deterministic frames on a logical master timeline. Each endpoint pipeline reads the same source position, maps the signal for its role, applies the listener-selected delay, makes a small adaptive sample-rate correction, and converts to the endpoint's shared-mode mix format. Separate event-driven render workers feed the WASAPI endpoints.

The Bluetooth rear endpoint is the initial synchronization reference because it is expected to have the larger and less controllable transport latency. The Realtek pipeline follows it by adding delay. The design still represents the reference as configuration rather than assuming that rear is always slower.

### Component responsibilities

#### `TestPatternGenerator`

Produces deterministic 48 kHz float frames for all supported patterns. It is pure DSP code: no COM, window calls, allocation during rendering, logging, or file access. Clicks use a short shaped pulse to avoid a full-scale single-sample impulse; tones use a bounded amplitude and short fade envelope.

#### `MasterTimeline`

Owns the monotonic logical frame position and defines frame zero for a playback run. Endpoint pipelines receive frames by absolute master-frame range so scheduling differences cannot cause the generated patterns to advance independently.

#### `DelayLine`

Implements a per-role FIFO sized for the maximum 2000 ms delay plus render safety margin. Delay is represented internally in frames. A live change crossfades or slews between read positions over a short interval so it does not create a discontinuity, pop, or duplicated click. The displayed millisecond value remains the persisted user setting.

#### `AdaptiveResampler`

Resamples the delayed master frames into the endpoint pipeline at a nominal 1:1 ratio. The synchronization controller may apply a small, bounded parts-per-million correction to this ratio to prevent long-term drift. Ratio changes are smoothed; the controller never corrects a large alignment error by an abrupt resampling jump.

#### `EndpointConverter`

Converts float stereo frames into the endpoint's shared-mode mix format and channel layout. Mono endpoints receive a downmix. Endpoints with more than two channels receive a conservative front-left/front-right mapping with other channels silent for this test milestone. Unsupported or malformed mix formats fail during preparation, before either stream starts.

#### `EndpointSession`

Owns one endpoint's `IAudioClient`, `IAudioRenderClient`, optional `IAudioClock`, render event, conversion state, and worker thread. COM interfaces are created and used on the owning worker after COM initialization. The event-driven loop queries buffer padding, obtains the available render buffer, fills it, releases it, and publishes telemetry.

The real-time loop must not allocate memory, write files, update controls, take a contended application mutex, or format log strings. Counters and a fixed-size telemetry snapshot are published atomically or through a single-producer/single-consumer structure.

#### `SyncController`

Consumes timestamped endpoint clock positions, device-buffer padding, and QPC observations. It estimates the relative rate error between the follower and reference after startup settles. A slow feedback loop produces a bounded correction for the follower resampler. The correction is clamped to a conservative range and slewed to avoid audible pitch modulation.

The manual delay remains the authority for absolute acoustic alignment. Clock control only prevents that alignment from drifting. If reliable clock telemetry is unavailable, playback continues with fixed delay, drift correction is disabled, and the UI clearly reports `Clock correction unavailable`.

#### `AudioEngineCoordinator`

Runs on a non-render control thread and owns the playback state machine. It validates settings, creates sessions, primes buffers, coordinates start and stop, forwards live delay updates, consumes error notifications, and exposes immutable status snapshots to the UI. The UI thread posts commands and polls or receives lightweight status messages; it never waits for a render worker.

## Playback state and lifecycle

The coordinator uses these externally visible states:

- `Stopped`: no active endpoint resources.
- `Preparing`: endpoints and formats are validated; buffers and workers are created.
- `Primed`: both endpoint buffers contain initial silence and workers are ready.
- `Running`: the common timeline is advancing and test audio is rendered.
- `Stopping`: output is faded, clients are stopped, and workers are joined.
- `Faulted`: playback has stopped and a diagnostic reason is retained for the UI.

A start operation performs the following atomically from the user's perspective:

1. Snapshot and validate endpoint assignments and delays.
2. Activate both shared-mode clients and obtain their mix formats and buffer sizes.
3. Allocate all delay, conversion, render, and telemetry storage.
4. Initialize event-driven streams and prime both with silence.
5. Arm both workers on a common QPC start boundary and establish master frame zero.
6. Begin the selected pattern, then allow clock estimation to settle before enabling adaptive correction.

WASAPI cannot guarantee that two unrelated hardware endpoints start on the same physical instant. Initial silence, a shared logical boundary, and the manual acoustic delay absorb this limitation. Steady-state resampling then addresses relative rate drift.

A normal stop applies an approximately 10 ms fade, signals both workers, stops both clients, joins the workers, releases COM resources on their owning threads, and returns to `Stopped`. Stop remains safe during every preparation stage and may be called repeatedly.

## Synchronization behavior

Two different effects are handled separately:

- **Fixed latency offset:** Windows buffering, Bluetooth transport, firmware, amplifiers, and speaker distance create a stable end-to-end offset. The listener compensates for this with the front and rear delay values.
- **Clock-rate drift:** The endpoint clocks run at slightly different rates, so a good initial alignment can move over time. The controller compensates with fractional resampling.

The controller does not infer acoustic latency from endpoint clocks. During a run it selects one reference, accumulates stable clock observations, rejects startup transients and implausible samples, estimates relative ppm error over a multi-second window, and slowly updates the follower ratio. Correction limits and tuning constants will be named configuration constants covered by simulation tests, not scattered magic numbers.

Manual delay edits reset the controller's short-term error accumulator without discarding the longer-term rate estimate. This prevents the controller from fighting an intentional user adjustment.

## Error handling

- If either endpoint fails to prepare, any prepared peer is torn down and neither starts audible playback.
- A single missed render deadline writes silence when possible, increments the endpoint underrun counter, and keeps running.
- Repeated underruns inside a bounded time window fault the entire run so the remaining endpoint cannot continue alone unnoticed.
- Device invalidation or Bluetooth disconnection stops both outputs, preserves the saved profile, and identifies the affected endpoint in the status message.
- A failed endpoint clock disables adaptive correction if rendering is otherwise healthy; it is visible as a degraded mode, not silently ignored.
- Unsupported format conversion, allocation failure, or thread startup failure aborts preparation and reports a specific error.
- Exceptions and failing HRESULTs never cross a render-thread boundary. Workers publish a compact fault record and the coordinator owns teardown.

The application does not automatically resume after reconnection in this milestone. The user refreshes endpoints and starts a new test, preventing unexpected sound from a newly reconnected Bluetooth device.

## Persistence

The existing `%LOCALAPPDATA%\SoundStageRouter\routing.ini` remains the per-user store. The profile records stable endpoint IDs, front/rear roles, front/rear manual delay milliseconds, and the last test pattern. Runtime drift estimates, underrun counters, and health state are not persisted.

Unknown or disconnected saved endpoint IDs remain in the profile but are shown as unavailable until endpoint enumeration finds them again. Corrupt or out-of-range delay values are clamped to the supported range and reported in status rather than causing startup failure.

## Threading and shutdown rules

- One UI thread owns all Win32 controls.
- One control path owns coordinator state transitions.
- Each active endpoint has one dedicated render worker.
- Endpoint workers never call each other and do not share mutable DSP buffers.
- The coordinator distributes read-only run configuration and command snapshots.
- Status travels from workers to coordinator to UI; UI objects are never referenced by audio code.
- Process shutdown first requests coordinated stop, then waits for workers before destroying the window and uninitializing the main COM apartment.

## Testing strategy

### Unit tests

- Test-pattern frame positions, channel selection, amplitude bounds, and fade envelopes.
- Delay-line exact offsets at 0 ms, representative values, and 2000 ms.
- Delay changes during non-silent audio for bounded discontinuity and correct final offset.
- Resampler unity behavior, positive and negative ppm correction, ratio slewing, and continuity across render blocks.
- Mix-format conversion for float, PCM, mono, stereo, and multichannel layouts used by shared-mode endpoints.

Pure DSP tests use deterministic buffers and do not require Windows audio hardware.

### Synchronization simulation

A fake endpoint-clock harness runs two independent clocks with configurable initial latency, rate error, jitter, buffer size, and observation loss. It verifies that:

- Fixed manual delay remains independent from drift estimation.
- The estimated rate converges for realistic positive and negative drift.
- The correction remains within its clamp and changes smoothly.
- Relative error stays within the target after convergence for a simulated 30-minute run.
- Missing or rejected clock observations degrade safely to fixed-delay operation.

### Lifecycle and fault tests

Inject failures at endpoint activation, format discovery, buffer allocation, worker startup, first render, stable playback, and shutdown. Verify idempotent stop, peer teardown, underrun escalation, device invalidation handling, and no use-after-release when stop races with preparation.

### Hardware acceptance

On the target Realtek soundbar/subwoofer and Bluetooth headrest:

1. Verify each role independently with front-only and rear-only patterns.
2. Use alternating clicks to confirm role placement.
3. Manually align paired clicks at the listening position.
4. Record an external measurement or listening observation at start and after 30 minutes; alignment must remain within 10 ms.
5. Adjust delay during playback and verify there is no obvious pop or unstable playback.
6. Disconnect and reconnect Bluetooth during a run; both streams must stop cleanly, the profile must remain, and a subsequent manually started run must work.

Hardware acceptance is required because shared-mode WASAPI telemetry cannot observe all latency beyond the OS render endpoint.

## Observability

The UI exposes enough information to diagnose the target setup without turning the render loop into a logger:

- Endpoint friendly name and role.
- Playback state per endpoint.
- Shared-mode sample rate, channel count, and buffer duration.
- Current manual delay.
- Reference/follower assignment.
- Clock-correction active/degraded status and estimated relative ppm.
- Underrun count and last fault summary.

Detailed diagnostic records, if later added, are assembled and written by a non-real-time thread from fixed-size event data.

## Security and privacy

This milestone only generates audio and opens selected render endpoints. It does not capture microphone input, loopback/system audio, media content, or network traffic. Settings remain local to the Windows user profile.

## Future compatibility

Once this milestone passes hardware acceptance, the same master timeline and endpoint pipeline can accept decoded or captured multichannel frames instead of generated test patterns. A later virtual audio driver can present a 5.1/7.1 endpoint to Windows and feed those frames to the user-mode router. Additional physical outputs can be represented as more role-to-endpoint pipelines, but their user experience and acoustic calibration are separate designs.

