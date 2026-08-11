#pragma once

#include "VirtualSurroundContract.h"

#include <string_view>

namespace soundstage::audio
{
    struct SurroundUiState
    {
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

    [[nodiscard]] constexpr SurroundUiState BuildSurroundUiState(
        const PlaybackMode mode,
        const VirtualSurroundFormat liveFormat,
        const VirtualSurroundFormat discoveredFormat,
        const bool faulted) noexcept
    {
        const VirtualSurroundFormat format =
            liveFormat != VirtualSurroundFormat::Unsupported
                ? liveFormat
                : discoveredFormat;

        SurroundUiState state;
        state.format = format;
        state.sideEnabled =
            mode == PlaybackMode::TestSignals ||
            format == VirtualSurroundFormat::SevenPointOne;

        if (format == VirtualSurroundFormat::FivePointOne)
        {
            state.badge = L"5.1 detected";
            state.hint = L"Used when Windows is set to 7.1.";
            if (faulted && mode == PlaybackMode::SystemAudio)
            {
                state.restartAction = L"Restart in 5.1";
                state.recovery =
                    L"Routing stopped. Review the fault, then restart manually in 5.1.";
            }
        }
        else if (format == VirtualSurroundFormat::SevenPointOne)
        {
            state.badge = L"7.1 detected";
            if (faulted && mode == PlaybackMode::SystemAudio)
            {
                state.restartAction = L"Restart in 7.1";
                state.recovery =
                    L"Routing stopped. Review the fault, then restart manually in 7.1.";
            }
        }

        return state;
    }
}
