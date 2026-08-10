#pragma once

#include "audio/AudioTypes.h"

#include <string>
#include <string_view>

namespace soundstage
{
    [[nodiscard]] std::wstring_view TestPatternToString(
        audio::TestPattern pattern) noexcept;
    [[nodiscard]] audio::TestPattern TestPatternFromString(
        std::wstring_view value) noexcept;
    [[nodiscard]] std::wstring_view PlaybackModeToString(
        audio::PlaybackMode mode) noexcept;
    [[nodiscard]] std::wstring_view RearFillModeToString(
        audio::RearFillMode mode) noexcept;

    struct RouterSettings
    {
        std::wstring frontEndpointId;
        std::wstring rearEndpointId;
        int frontDelayMs = 0;
        int rearDelayMs = 0;
        int frontLevelPercent = 100;
        int rearLevelPercent = 100;
        audio::TestPattern lastPattern =
            audio::TestPattern::PairedClicks;
        audio::PlaybackMode mode = audio::PlaybackMode::SystemAudio;
        audio::RearFillMode rearFill = audio::RearFillMode::Off;
        bool loadAdjustedValues = false;
    };

    class RouterSettingsStore
    {
    public:
        RouterSettingsStore();
        explicit RouterSettingsStore(std::wstring path);

        [[nodiscard]] RouterSettings Load() const;
        void Save(const RouterSettings& settings) const;
        [[nodiscard]] const std::wstring& Path() const noexcept;

    private:
        std::wstring path_;
    };
}
