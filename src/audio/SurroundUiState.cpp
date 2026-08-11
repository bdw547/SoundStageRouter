#include "SurroundUiState.h"

namespace soundstage::audio
{
    namespace
    {
        constexpr bool IsSupportedFormat(
            const VirtualSurroundFormat format) noexcept
        {
            return format == VirtualSurroundFormat::FivePointOne ||
                   format == VirtualSurroundFormat::SevenPointOne;
        }

        void PresentFormat(SurroundUiState& ui,
                           const VirtualSurroundFormat format)
        {
            ui.format = format;
            ui.formatSeverity = IsSupportedFormat(format)
                ? UiSeverity::Healthy
                : UiSeverity::Warning;

            if (format == VirtualSurroundFormat::FivePointOne)
            {
                ui.formatText = L"5.1 detected";
                ui.badge = L"5.1 detected";
            }
            else if (format == VirtualSurroundFormat::SevenPointOne)
            {
                ui.formatText = L"7.1 detected";
                ui.badge = L"7.1 detected";
            }
            else
            {
                ui.formatText = L"Surround format unavailable";
                ui.badge = L"Surround format unavailable";
            }
        }

        void PresentRouteState(SurroundUiState& ui,
                               const PlaybackState state,
                               const bool routable)
        {
            switch (state)
            {
            case PlaybackState::Stopped:
                ui.routeStateText = routable ? L"Ready" : L"Setup required";
                ui.routeSeverity = routable
                    ? UiSeverity::Healthy
                    : UiSeverity::Warning;
                break;
            case PlaybackState::Preparing:
            case PlaybackState::Primed:
                ui.routeStateText = L"Starting";
                break;
            case PlaybackState::Running:
                ui.routeStateText = L"Routing";
                ui.routeSeverity = UiSeverity::Healthy;
                break;
            case PlaybackState::Stopping:
                ui.routeStateText = L"Stopping";
                break;
            case PlaybackState::Faulted:
                ui.routeStateText = L"Needs attention";
                ui.routeSeverity = UiSeverity::Fault;
                break;
            }
        }

        void PresentClockHealth(SurroundUiState& ui,
                                const ClockHealth health)
        {
            switch (health)
            {
            case ClockHealth::Active:
                ui.syncText = L"Aligned";
                break;
            case ClockHealth::Settling:
                ui.syncText = L"Synchronizing outputs...";
                break;
            case ClockHealth::Unavailable:
                ui.syncText = L"Synchronization unavailable";
                break;
            }
        }
    }

    SurroundUiState BuildSurroundUiState(
        const EngineStatus& status,
        const PlaybackMode mode)
    {
        SurroundUiState ui;
        const bool routable =
            mode == PlaybackMode::TestSignals ||
            IsSupportedFormat(status.surroundFormat);
        PresentFormat(ui, status.surroundFormat);
        PresentRouteState(ui, status.state, routable);
        PresentClockHealth(ui, status.clockHealth);

        ui.sideLevelEnabled =
            mode == PlaybackMode::TestSignals ||
            status.surroundFormat == VirtualSurroundFormat::SevenPointOne;
        ui.sideEnabled = ui.sideLevelEnabled;
        // Explain the disabled control only while it is actually disabled;
        // in test-signal mode the side slider stays live in 5.1.
        if (!ui.sideLevelEnabled &&
            status.surroundFormat == VirtualSurroundFormat::FivePointOne)
        {
            ui.sideLevelHint = L"Used when Windows is set to 7.1.";
            ui.hint = L"Used when Windows is set to 7.1.";
        }

        const bool inactive =
            status.state == PlaybackState::Stopped ||
            status.state == PlaybackState::Faulted;
        ui.startEnabled = inactive && routable;
        ui.stopEnabled =
            status.state == PlaybackState::Preparing ||
            status.state == PlaybackState::Primed ||
            status.state == PlaybackState::Running;
        ui.deviceSelectionEnabled = inactive;
        ui.liveTuningEnabled = ui.stopEnabled;

        if (status.state == PlaybackState::Faulted)
        {
            if (!status.lastFault.message.empty())
            {
                ui.recoveryText = status.lastFault.message;
            }
            else
            {
                ui.recoveryText =
                    L"Routing stopped. Review the issue, then restart manually.";
            }
        }
        else if (mode == PlaybackMode::SystemAudio &&
                 !IsSupportedFormat(status.surroundFormat))
        {
            ui.recoveryText =
                L"Install or update SoundStage Router Surround, then choose "
                L"5.1 or 7.1 at 48 kHz in Windows.";
        }

        return ui;
    }

    SurroundUiState BuildSurroundUiState(
        const PlaybackMode mode,
        const VirtualSurroundFormat liveFormat,
        const VirtualSurroundFormat discoveredFormat,
        const bool faulted)
    {
        const VirtualSurroundFormat format =
            liveFormat != VirtualSurroundFormat::Unsupported
                ? liveFormat
                : discoveredFormat;

        EngineStatus status;
        status.state = faulted
            ? PlaybackState::Faulted
            : liveFormat != VirtualSurroundFormat::Unsupported
                ? PlaybackState::Running
                : PlaybackState::Stopped;
        status.surroundFormat = format;
        status.virtualEndpointReady = IsSupportedFormat(format);

        SurroundUiState ui = BuildSurroundUiState(status, mode);
        if (faulted && mode == PlaybackMode::SystemAudio)
        {
            if (format == VirtualSurroundFormat::FivePointOne)
            {
                ui.restartAction = L"Restart in 5.1";
                ui.recovery =
                    L"Routing stopped. Review the fault, then restart manually in 5.1.";
            }
            else if (format == VirtualSurroundFormat::SevenPointOne)
            {
                ui.restartAction = L"Restart in 7.1";
                ui.recovery =
                    L"Routing stopped. Review the fault, then restart manually in 7.1.";
            }
            ui.recoveryText = ui.recovery;
        }

        return ui;
    }
}
