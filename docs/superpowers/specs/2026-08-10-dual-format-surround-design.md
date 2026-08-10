# Dual-Format 5.1 and 7.1 Surround Design

**Date:** 2026-08-10  
**Status:** Approved for implementation

## Objective

Upgrade SoundStage Router from a fixed virtual 5.1 endpoint to one Windows
virtual surround endpoint that supports both 5.1 and 7.1. Windows owns the
active speaker-format choice. The application detects the current format and
routes it to the existing synchronized physical outputs: the Realtek soundbar
and subwoofer as Front, and the Bluetooth chair speakers as Rear.

For 7.1 content, the chair receives both the Back and Side pairs. The listener
controls their contributions independently. Existing delay, drift correction,
system-audio capture, test signals, rear fill, and physical-output level
features remain.

## Scope

Included:

- One virtual endpoint named **SoundStage Router Surround**.
- Selectable 5.1 and 7.1 formats in Windows Sound settings.
- 48 kHz, 32-bit formats, with 7.1 as the initial default.
- Automatic detection of the active six- or eight-channel loopback format.
- Eight-channel internal surround frames.
- Linked-stereo Back Level and Side Level controls from 0–100%.
- A streamlined Command Deck UI on the existing native Win32 foundation.
- Settings migration, driver validation, automated tests, and updated hardware
  acceptance instructions.

Excluded:

- A third physical output or discrete physical Side speakers.
- Automatic microphone calibration or automatic restart after a fault.
- A WinUI 3 migration, complex animation, or a new navigation framework.
- Dolby or DTS encoding or decoding.

## User Experience

Windows displays one virtual render endpoint named **SoundStage Router
Surround**. The user selects 5.1 or 7.1 in Windows Sound settings and starts
routing in the app. The app reports **5.1 detected** or **7.1 detected**; it
does not add a second format selector that could disagree with Windows.

The window becomes one dark Command Deck dashboard to minimize implementation
work while improving hierarchy:

- a header with application state and detected-format badge;
- Front and Chair cards with endpoint and delay controls;
- a Chair Mix card with Back Level and Side Level controls;
- plain-language synchronization health;
- a stable Start/Stop Routing action area;
- low-level telemetry inside collapsed **Technical details**.

Back Level and Side Level default to 100%. Side Level remains visible but is
disabled in 5.1 mode with "Used when Windows is set to 7.1." Device selection
is locked while routing; delay and level controls remain live.

The visual system uses Segoe UI Variable where available, a near-black navy
background, dark blue cards, high-contrast text, cyan interaction accents,
mint healthy states, amber warnings, and coral faults. Normal Win32 controls
and focused custom drawing provide the polish without a framework rewrite.

## Virtual Driver Contract

The SysVAD-derived driver exposes one root-enumerated render endpoint. All
host, offload, loopback, audio-engine, topology, and jack declarations agree on
these layouts:

| Format | Channels | Channel mask | Channel order |
|---|---:|---:|---|
| 5.1 | 6 | `KSAUDIO_SPEAKER_5POINT1` (`0x003F`) | FL, FR, FC, LFE, BL, BR |
| 7.1 | 8 | `KSAUDIO_SPEAKER_7POINT1_SURROUND` (`0x063F`) | FL, FR, FC, LFE, BL, BR, SL, SR |

Both device formats are 48,000 Hz with 32-bit PCM containers. The Windows
shared audio engine supplies float32 loopback packets in the active mix
format. The 7.1 descriptor is the initial/default device format and 5.1 stays
available for selection in Windows.

Driver and INF strings use **SoundStage Router Surround**. The old development
5.1 driver instance must be removed and the rebuilt test-signed package
installed before the new formats and name take effect.

## Format Discovery and Validation

Virtual endpoint discovery recognizes the neutral endpoint name and keeps the
existing driver-interface identity fallback. The virtual endpoint remains
excluded from physical-output selectors to prevent feedback.

Loopback preparation accepts exactly:

- 48 kHz, float32, 6 channels, block alignment 24, mask `0x003F`; or
- 48 kHz, float32, 8 channels, block alignment 32, mask `0x063F`.

Every other rate, sample type, count, alignment, or mask fails before physical
streams start. Capture telemetry publishes the detected format for the UI.

## Internal Frame and Data Flow

`SurroundFrame` expands to eight named samples: `frontLeft`, `frontRight`,
`frontCenter`, `lfe`, `backLeft`, `backRight`, `sideLeft`, and `sideRight`.
Six-channel capture fills the first six and leaves Side fields at zero.
Eight-channel capture fills all fields in channel-mask order. Silent packets
generate zeroed frames. The master ring buffer and endpoint readers continue
to carry already-routed Front/Rear stereo frames.

The Front matrix is unchanged:

```text
Front L = limit(FL + 0.70710678 * FC + 0.5 * LFE)
Front R = limit(FR + 0.70710678 * FC + 0.5 * LFE)
```

The Rear matrix becomes:

```text
Rear L = limit(backGain * BL + sideGain * SL)
Rear R = limit(backGain * BR + sideGain * SR)
```

`backGain` and `sideGain` are the saved percentages divided by 100 and default
to 1.0. There is no fixed −3 dB attenuation. The existing sample limiter acts
only when a combined result exceeds the valid range. Left and right gains are
linked so the controls preserve stereo balance.

Native-surround detection examines original Back and Side samples before user
gain. Rear fill is eligible only when all four native surround channels are
silent. Setting a level to 0% must not make duplicate or ambient fill appear.
In 5.1 the Side samples are zero and Side Level has no signal effect.

Existing physical Front and Rear master levels remain separate post-routing
gains. Back Level and Side Level operate on logical channel contributions
before the Rear master gain. Live gains are published without locks or
allocation on the real-time audio path.

## Settings and Migration

The existing `%LOCALAPPDATA%\SoundStageRouter\routing.ini` adds:

```ini
BackLevelPercent=100
SideLevelPercent=100
```

Missing keys from an older profile default to 100 without invalidating other
settings. Endpoint IDs, delays, playback mode, test pattern, rear fill, and
physical-output levels remain. Malformed new values fall back to 100 and use
the existing adjusted-values notice. Load and save clamp values to 0–100%.

## State and Error Handling

- **Ready:** both outputs exist and the virtual endpoint has a supported active
  format.
- **Starting:** preparation and synchronization are reported in plain language.
- **Running:** the format badge is stable; device selectors are locked; delay
  and level controls remain live.
- **Format changed:** routing stops and an amber message offers one **Restart in
  5.1** or **Restart in 7.1** action. There is no automatic restart.
- **Physical output disconnected:** routing stops, preserves the configuration,
  highlights the affected card, and asks the user to reconnect it.
- **Old or missing driver:** the app directs the user to updated driver setup;
  technical codes appear only in Technical details.
- **Unexpected virtual format:** no physical stream starts and the app directs
  the user to choose 5.1 or 7.1 at 48 kHz in Windows.

Partial startup always stops capture and prepared endpoint sessions using the
existing cancellation and fault-publication model.

## Testing

Driver and discovery checks:

- Verify descriptors, data ranges, maxima, modes, topology masks, and names all
  agree on the two layouts.
- Build x64, run INF validation, install the test-signed package, and confirm
  Windows offers both formats with 7.1 initially selected.
- Confirm exactly one virtual endpoint appears and is excluded from physical
  output selectors.

Unit tests:

- Accept the exact six- and eight-channel formats and reject wrong masks,
  counts, alignments, rates, and encodings.
- Decode isolated FL, FR, FC, LFE, BL, BR, SL, and SR inputs.
- Prove the Front matrix is unchanged.
- Prove Back and Side levels independently affect their linked stereo pairs at
  0%, intermediate values, and 100%.
- Prove 100% + 100% has no automatic attenuation and limits only overloads.
- Prove native-surround detection includes Side channels and precedes gain.
- Cover silent packets, rear fill, settings defaults/round trips/migration, and
  the important UI state models without requiring audio hardware.

Hardware acceptance:

1. Install the updated driver and verify the neutral endpoint name.
2. Select 5.1 and play a known six-channel identification file.
3. Select 7.1, restart routing, and play an eight-channel identification file.
4. Confirm Back Left and Side Left reach only the left chair speaker, and Back
   Right and Side Right reach only the right chair speaker.
5. Exercise each logical gain at 0%, 50%, and 100%.
6. Change Windows format during a run and verify the safe stop and restart
   action.
7. Repeat the existing paired-click start and 30-minute checks; the established
   10 ms at 95% acoustic-alignment requirement remains.

## Completion Boundary

The change is complete when the rebuilt driver offers both Windows formats,
the app detects and routes each through the approved matrix, the new controls
and streamlined dashboard work as specified, Debug and Release automated tests
pass, driver validation passes, and the hardware procedure is documented for
the target speakers.
