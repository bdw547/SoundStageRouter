# Two-device hardware acceptance

Use this protocol only after the synchronized playback software checks pass.
It requires the target Realtek front output, Bluetooth rear output, one
stationary microphone, and an external recorder or separate recording
application. SoundStage Router and the analyzer never capture microphone or
system audio.

## Setup

1. Select the Realtek device as **Front** and the Bluetooth headrest as
   **Rear**.
2. Select **Paired clicks**, start playback, and manually adjust the endpoint
   delays until each front/rear pair is heard as one event.
3. Place one microphone at the listening position. Mark its position and do
   not move it until both recordings are complete.
4. Configure the external recorder for uncompressed 48,000 Hz PCM16 or
   IEEE float32 WAV, mono or stereo.

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
