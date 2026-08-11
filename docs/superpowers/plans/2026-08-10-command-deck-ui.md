# Streamlined Command Deck UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the functional SoundStage Router window into a professional, plain-language Command Deck without replacing its native Win32 UI framework.

**Architecture:** A small pure presentation model converts engine state into stable user-facing labels and visibility/enabled flags. `AppWindow` keeps ownership of native controls while a focused theme helper supplies colors, fonts, brushes, and card painting. Existing controls are regrouped into Front, Chair, Chair Mix, and Technical details sections.

**Tech Stack:** C++20, Win32/GDI, Windows common controls, Segoe UI Variable/Segoe UI fallback, MSBuild v145, custom native test harness.

## Global Constraints

- Keep the existing Win32 application and C++ audio engine; do not introduce WinUI 3 or another UI framework.
- Use one dashboard rather than a new multi-page navigation system.
- Preserve every functional control and all existing routing behavior.
- Show plain-language status first and keep low-level telemetry collapsed by default.
- Keep Back and Side controls visible; disable Side in 5.1 with a concise explanation.
- Use a near-black navy background, dark blue cards, cyan actions, mint health, amber warnings, coral faults, and high-contrast text.
- Use Segoe UI Variable where installed and Segoe UI as fallback.
- Avoid continuous animation, transparency effects, and GPU dependencies.
- Maintain keyboard navigation, visible focus, 100–200% DPI behavior, and a usable minimum window size.

---

### Task 1: Testable presentation model

**Files:**
- Create: `src/ui/SurroundUiModel.h`
- Create: `src/ui/SurroundUiModel.cpp`
- Create: `tests/ui/SurroundUiModelTests.cpp`
- Modify: `SoundStageRouter.vcxproj`
- Modify: `SoundStageRouter.Tests.vcxproj`

**Interfaces:**
- Consumes: `audio::EngineStatus`, `audio::PlaybackMode`, and `audio::VirtualSurroundFormat`
- Produces: `SurroundUiState BuildSurroundUiState(const audio::EngineStatus&, audio::PlaybackMode)`

- [x] **Step 1: Write failing presentation tests**

Cover these exact outputs:

```cpp
EngineStatus status;
status.state = PlaybackState::Running;
status.surroundFormat = VirtualSurroundFormat::SevenPointOne;
status.clockHealth = ClockHealth::Active;
const SurroundUiState ui = BuildSurroundUiState(
    status, PlaybackMode::SystemAudio);
EXPECT_EQ(ui.formatText, std::wstring(L"7.1 detected"));
EXPECT_EQ(ui.routeStateText, std::wstring(L"Routing"));
EXPECT_EQ(ui.syncText, std::wstring(L"Aligned"));
EXPECT_TRUE(ui.sideLevelEnabled);
```

Add 5.1 expectations (`SideLevelEnabled == false`, explanation text), Settling (`Synchronizing outputs...`), stopped Ready, missing virtual endpoint, and Faulted with a nonempty recovery message.

- [x] **Step 2: Build to verify RED**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.Tests.vcxproj -t:Build -p:Configuration=Debug -p:Platform=x64
```

Expected: build fails because the new model does not exist.

- [x] **Step 3: Implement the focused model**

Define:

```cpp
enum class UiSeverity { Neutral, Healthy, Warning, Fault };

struct SurroundUiState
{
    std::wstring formatText;
    std::wstring routeStateText;
    std::wstring syncText;
    std::wstring recoveryText;
    std::wstring sideLevelHint;
    UiSeverity formatSeverity = UiSeverity::Neutral;
    UiSeverity routeSeverity = UiSeverity::Neutral;
    bool startEnabled = false;
    bool stopEnabled = false;
    bool deviceSelectionEnabled = true;
    bool liveTuningEnabled = false;
    bool sideLevelEnabled = false;
};
```

Keep it pure: no HWND, COM, timers, global state, or resource lookup. Map known states to the exact plain-language strings in the approved design.

- [x] **Step 4: Add the sources to both project files and run tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Expected: all tests pass.

- [x] **Step 5: Commit**

```powershell
git add src/ui tests/ui SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat(ui): add surround presentation model"
```

### Task 2: Native Command Deck theme and layout

**Files:**
- Create: `src/ui/CommandDeckTheme.h`
- Create: `src/ui/CommandDeckTheme.cpp`
- Modify: `src/AppWindow.h`
- Modify: `src/AppWindow.cpp`
- Modify: `SoundStageRouter.vcxproj`

**Interfaces:**
- Consumes: `SurroundUiState`, existing HWND controls, and functional Back/Side trackbars from the core plan
- Produces: themed dashboard painting, stable card layout, collapsed Technical details, and keyboard/DPI-friendly native controls

- [x] **Step 1: Add a centralized native theme**

Define one palette rather than scattering literals:

```cpp
struct CommandDeckColors
{
    COLORREF window = RGB(7, 13, 23);
    COLORREF card = RGB(13, 23, 38);
    COLORREF cardRaised = RGB(16, 29, 46);
    COLORREF border = RGB(34, 51, 74);
    COLORREF text = RGB(246, 249, 255);
    COLORREF secondary = RGB(143, 163, 189);
    COLORREF accent = RGB(69, 216, 255);
    COLORREF healthy = RGB(79, 224, 173);
    COLORREF warning = RGB(255, 201, 107);
    COLORREF fault = RGB(255, 99, 125);
};
```

`CommandDeckTheme` owns fonts, solid brushes, pens, DPI-scaled spacing, and `PaintCard(HDC, RECT, COLORREF)`. Release every GDI object in its destructor.

- [x] **Step 2: Replace the long-form layout with four stable regions**

At approximately 1240×780, use a 32-pixel outer margin, 24-pixel gaps, a 72-pixel header, left content width near two-thirds, and right content width near one-third:

```text
Header:  SoundStage Router | detected format | route state
Left:    Front card
         Chair card
Right:   Chair Mix card
Bottom:  recovery/status text | Start or Stop Routing
Hidden:  Technical details panel below the main cards
```

At narrower widths, stack Chair Mix below the two device cards rather than clipping controls. Set a practical minimum track size in `WM_GETMINMAXINFO`.

- [x] **Step 3: Paint cards and apply accessible control styling**

Handle `WM_ERASEBKGND`, `WM_PAINT`, `WM_CTLCOLORSTATIC`, `WM_CTLCOLOREDIT`, and `WM_CTLCOLORBTN` through the theme. Keep native focus cues and trackbar behavior. Use cyan only for primary actions/focus, mint for healthy state, amber for recoverable stops, and coral for faults. Do not add glow animation or transparent windows.

- [x] **Step 4: Add the Technical details disclosure**

Add one `Technical details` button and `technicalDetailsExpanded_` state. Default it to false on every launch. When collapsed, hide the existing front/rear/sync telemetry controls and size the window to the dashboard. When expanded, show the exact existing sample-rate, channel-count, buffer, delay, underrun, reference/follower, ppm, and fault data without changing the engine.

- [x] **Step 5: Bind the presentation model**

In `RenderEngineStatus`, build `SurroundUiState` once and apply its text, severity colors, enable flags, hint, and Start/Stop state. Keep timer work nonblocking. During a format fault, show the amber recovery message and relabel the primary action `Restart in 5.1` or `Restart in 7.1` when detected.

- [x] **Step 6: Build and run automated tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Build -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Expected: app and tests build, and all tests pass.

- [x] **Step 7: Inspect the running window at 100%, 150%, and 200% scaling**

Launch `.\build\Debug\SoundStageRouter.exe`. Verify no clipped labels or controls, clear keyboard focus, readable disabled Side Level in 5.1, a stable layout when Technical details toggles, and a usable stacked layout at minimum width. Record screenshots for the implementation review; do not commit machine-specific images.

- [x] **Step 8: Commit**

```powershell
git add src/ui/CommandDeckTheme.h src/ui/CommandDeckTheme.cpp src/AppWindow.h src/AppWindow.cpp SoundStageRouter.vcxproj
git commit -m "feat(ui): add streamlined command deck"
```

### Task 3: UI copy, documentation, and final verification

**Files:**
- Modify: `README.md`
- Modify: `docs/testing/hardware-acceptance.md`
- Modify: `docs/superpowers/plans/2026-08-10-command-deck-ui.md`

**Interfaces:**
- Consumes: completed Command Deck and dual-format core behavior
- Produces: consistent plain-language copy and verified Debug/Release deliverables

- [x] **Step 1: Audit user-facing copy**

Run:

```powershell
rg -n 'clock|ppm|underrun|fault code|virtual 5.1|SoundStage Router 5.1' src/AppWindow.cpp README.md docs/testing
```

Keep technical clock/ppm/underrun language only inside Technical details. Replace stale endpoint copy with `SoundStage Router Surround`. Use `Front`, `Chair`, `Back Level`, `Side Level`, `Aligned`, and `Synchronizing outputs...` consistently.

- [x] **Step 2: Document the dashboard**

Add a short README section explaining where to see the active Windows format, select Front/Chair endpoints, tune delays, balance Back/Side contributions, expand diagnostics, and recover after a format change.

- [x] **Step 3: Rebuild and test Debug**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
```

Expected: build succeeds and all tests pass.

- [x] **Step 4: Rebuild and test Release**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
```

Expected: build succeeds and all tests pass.

- [x] **Step 5: Run final repository checks**

```powershell
git diff --check
git status --short
git log -12 --oneline
```

Expected: no whitespace errors; only explicitly documented hardware-install artifacts may remain untracked; commits are scoped by task.

- [x] **Step 6: Commit documentation and plan checkmarks**

```powershell
git add README.md docs/testing/hardware-acceptance.md docs/superpowers/plans/2026-08-10-command-deck-ui.md
git commit -m "docs: explain the command deck interface"
```

## Execution evidence

| Task | Commits | Verification |
|---|---|---|
| 1 — Presentation model | `001c0de`, fix `56aebd0` | Debug 135/135; independent review clean after gating Ready on routability |
| 2 — Command deck theme and layout | `95df8a5` | Debug and Release 142/142 with zero build warnings; window inspected at 100/150/200% scaling, keyboard focus and stacked minimum width verified |
| 2 — Independent review fixes | `448f49e` | Review found 2 Important (work-area sizing, short-client overlap) and 5 Minor findings; fixed via height-floored scrollable layout, work-area clamps, mode-gated restart copy, side-label repaint, stacked badge/status room, tab order, test-mode hint; Debug and Release 144/144; live-window smoke test verified short and stacked windows scroll to a usable action bar |
| 3 — Copy, documentation, verification | docs commit (this change) | Copy audit aligned remaining user-facing strings with the Front/Chair card names; README documents the dashboard; full Debug and Release rebuilds passed 142/142 before the fix wave and 144/144 after |
