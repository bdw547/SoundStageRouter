# Two-device hardware acceptance

Use this protocol only after the synchronized playback software checks pass.
It requires the target Realtek front output, Bluetooth rear output, one
stationary microphone, and an external recorder or separate recording
application. SoundStage Router does not capture the microphone; system mode
does loopback-capture only the **SoundStage Router Surround** virtual render
endpoint.

## Setup

1. Select the Realtek device as **Front** and the Bluetooth headrest as
   **Chair**.
2. Select **Paired clicks**, start playback, and manually adjust the endpoint
   delays until each front/rear pair is heard as one event.
3. Place one microphone at the listening position. Mark its position and do
   not move it until both recordings are complete.
4. Configure the external recorder for uncompressed 48,000 Hz PCM16 or
   IEEE float32 WAV, mono or stereo.

## Driver preparation

If Windows still shows the old **SoundStage Router 5.1** development endpoint,
stop SoundStage Router and use the driver's elevated uninstall script before
installing the rebuilt, test-signed package. An in-place install does not
replace the old device instance's name and format list. Follow the exact
[driver install and uninstall instructions](../../driver/SoundStageRouterVirtualAudio/README.md#install-and-uninstall);
do not install either package from a normal shell.

The updated endpoint has this exact contract:

| Windows format | Channels | Mask | Channel order |
|---|---:|---:|---|
| 5.1 | 6 | `0x003F` (`KSAUDIO_SPEAKER_5POINT1`) | FL, FR, FC, LFE, BL, BR |
| 7.1 | 8 | `0x063F` (`KSAUDIO_SPEAKER_7POINT1_SURROUND`) | FL, FR, FC, LFE, BL, BR, SL, SR |

Both formats are 48 kHz with 32-bit containers, and the shared audio engine
provides float32 loopback samples. Windows owns the active-format choice; the
app only detects and reports it. Stop routing before an intentional format
change whenever possible.

## Seven-step system-audio routing acceptance

1. Install the updated driver and verify Windows shows exactly one **SoundStage
   Router Surround** render endpoint. Confirm Windows offers both formats above
   and initially selects 7.1. Set it as the Windows default output. Confirm the
   app does not offer the virtual endpoint as a physical Front or Chair output.
2. In Windows select 5.1 at 48 kHz. In the app select **System audio (virtual
   surround)**, the target Front and Chair devices, and rear fill Off. Start
   routing, confirm **5.1 detected**, and play a known six-channel identification
   file from an ordinary shared-mode application. Verify FL/FR/FC/LFE reach
   only Front according to `FL + 0.707 FC + 0.5 LFE` and its right-channel
   equivalent; verify BL/BR reach the chair speakers with correct orientation.
   Side Level must be disabled and have no signal effect.
3. Stop routing, select 7.1 at 48 kHz in Windows, and restart routing. Confirm
   **7.1 detected**, then play a known eight-channel identification file.
   Verify all channels follow the documented order and that Front behavior is
   unchanged.
4. With isolated native channels, confirm Back Left and Side Left reach only
   the left chair speaker, while Back Right and Side Right reach only the right
   chair speaker. The chair formulas must be `BackGain × BL + SideGain × SL`
   and `BackGain × BR + SideGain × SR`. Here `BackGain = Back Level / 100` and
   `SideGain = Side Level / 100`.
5. Exercise Back Level and Side Level independently at 0%, 50%, and 100%.
   Confirm each control remains linked stereo, is live without a routing
   restart, and has no fixed attenuation: an isolated contribution at 100%
   matches its source level. When Back and Side are present together, only the
   existing output limiter may reduce an overload. Also verify native Back or
   Side content suppresses rear fill even when its logical level is 0%.
6. While routing, change the Windows format. Verify capture and both physical
   outputs stop safely and the app reports a fault; it must not restart
   automatically. After the format change completes, restart manually and
   confirm the correct detected-format label. Also disconnect each physical
   endpoint in turn and invalidate/remove the virtual endpoint; each event
   must stop both outputs and require a manual restart.
7. Repeat the paired-click start and 30-minute recordings below. Both analyzer
   runs must meet the established 10 ms at 95% acoustic-alignment requirement.
   During the interval, also play diverse system content and record overflow,
   underrun, clock-correction, and audible-glitch observations. The app must
   remain open throughout.

Separately, play stereo content and verify rear fill Off is silent at the
chair speakers, Duplicate produces rear stereo at -6 dB, and Ambient produces
difference ambience. Repeat with native Back or Side content and verify native surround
replaces fill.

## Start recording

1. While paired-click playback continues, record at least 30 seconds.
2. Save the file without processing, resampling, trimming click events, or
   changing channel order.
3. Record the endpoint names, configured delays, recording format, date, and
   the analyzer console output.

## Drift interval and end recording

1. Keep playback continuous for 30 minutes. Do not change endpoints, delays,
   microphone position, recorder settings, or listening-position geometry.
2. Without moving the microphone, record a second WAV for at least 30 seconds.
3. Run the Release analyzer on both recordings:

   ```powershell
   .\build\Release\SoundStageAlignmentAnalyzer.exe .\start.wav
   .\build\Release\SoundStageAlignmentAnalyzer.exe .\after-30-minutes.wav
   ```

## Acceptance

Accept the objective timing criterion only when both commands exit `0`, each
report contains at least 20 paired clicks, and each 95th-percentile absolute
offset is at most 10.00 ms. Retain both WAV paths and complete console reports.
A result with fewer than 20 pairs is invalid and must be recorded again.

Separately document the playback plan's listening check for live 1–20 ms delay
edits and its Bluetooth disconnect/reconnect test. Those checks do not replace
the two analyzer results. Do not infer acceptance from endpoint-clock
telemetry alone.
