# Two-device hardware acceptance

Use this protocol only after the synchronized playback software checks pass.
It requires the target Realtek front output, Bluetooth rear output, one
stationary microphone, and an external recorder or separate recording
application. SoundStage Router does not capture the microphone; system mode
does loopback-capture only the SoundStage Router 5.1 virtual render endpoint.

## Setup

1. Select the Realtek device as **Front** and the Bluetooth headrest as
   **Rear**.
2. Select **Paired clicks**, start playback, and manually adjust the endpoint
   delays until each front/rear pair is heard as one event.
3. Place one microphone at the listening position. Mark its position and do
   not move it until both recordings are complete.
4. Configure the external recorder for uncompressed 48,000 Hz PCM16 or
   IEEE float32 WAV, mono or stereo.

## System-audio routing acceptance

1. Install the driver by its documented procedure and verify exactly one
   **SoundStage Router 5.1** endpoint reports 48 kHz, float32, six channels,
   mask FL/FR/FC/LFE/BL/BR.
2. Set that endpoint as the Windows default output. In the app select **System
   audio (virtual 5.1)**, Front and Rear physical devices, and rear fill Off.
3. Start routing and play a known six-channel channel-identification file from
   an ordinary shared-mode Windows application. Verify FL/FR/FC/LFE reach only
   the front pair according to the documented mix and BL/BR reach the rear
   pair with correct left/right orientation.
4. Play stereo content and verify rear fill Off is silent at the rear, Duplicate
   produces rear stereo at -6 dB, and Ambient produces difference ambience.
   Repeat with native BL/BR content and verify native rear replaces fill.
5. Confirm choosing the virtual endpoint as a physical output is impossible.
   Disconnect each physical endpoint in turn, then invalidate/remove the
   virtual endpoint; each event must stop both outputs and require a manual
   restart.
6. Keep diverse system playback running for 30 minutes. Record overflow,
   underrun, clock-correction, and audible-glitch observations. The app must
   remain open throughout.

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
