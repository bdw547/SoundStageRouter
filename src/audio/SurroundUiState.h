#pragma once

#include "VirtualSurroundContract.h"

#include <string>
#include <string_view>

namespace soundstage::audio
{
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

        // Compatibility fields used by the current native window while it is
        // migrated to the presentation names above.
        VirtualSurroundFormat format =
            VirtualSurroundFormat::Unsupported;
        bool sideEnabled = false;
        std::wstring_view badge = L"Surround format unavailable";
        std::wstring_view hint{};
        std::wstring_view restartAction{};
        std::wstring_view recovery{};
    };

    [[nodiscard]] constexpr bool ShouldRefreshSurroundDiscovery(
        const PlaybackState previous,
        const PlaybackState current) noexcept
    {
        return previous != current &&
               (current == PlaybackState::Stopped ||
                current == PlaybackState::Faulted);
    }

    [[nodiscard]] SurroundUiState BuildSurroundUiState(
        const EngineStatus& status,
        PlaybackMode mode);

    [[nodiscard]] SurroundUiState BuildSurroundUiState(
        const PlaybackMode mode,
        const VirtualSurroundFormat liveFormat,
        const VirtualSurroundFormat discoveredFormat,
        const bool faulted);
}
