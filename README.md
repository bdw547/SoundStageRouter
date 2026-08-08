# SoundStage Router

SoundStage Router is a native Windows prototype for combining physical audio output
devices into one synchronized surround layout.

The current milestone provides:

- WASAPI/MMDevice enumeration of active Windows render endpoints.
- Friendly names, default-device detection, and native mix-format inspection.
- Front and rear endpoint assignment for a soundbar and Bluetooth headrest.
- Per-endpoint delay values from 0 to 2000 ms.
- Persistent per-user configuration in `%LOCALAPPDATA%\SoundStageRouter\routing.ini`.

## Build

Open `SoundStageRouter.sln` in Visual Studio 2026 and build `Release | x64`, or run:

```powershell
msbuild SoundStageRouter.sln -p:Configuration=Release -p:Platform=x64
```

The executable is written to `build\Release\SoundStageRouter.exe`.

## Next milestone

The next layer will open one WASAPI render stream per assigned endpoint, generate
channel-specific test tones, measure endpoint clock positions, and implement delay
buffers plus adaptive drift correction. A virtual 5.1/7.1 render endpoint follows
after the user-mode routing engine is stable.
