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

    struct RouterSettings
    {
        std::wstring frontEndpointId;
        std::wstring rearEndpointId;
        int frontDelayMs = 0;
        int rearDelayMs = 0;
        audio::TestPattern lastPattern =
            audio::TestPattern::PairedClicks;
        bool loadAdjustedValues = false;
    };

    class RouterSettingsStore
    {
    public:
        RouterSettingsStore();

        [[nodiscard]] RouterSettings Load() const;
        void Save(const RouterSettings& settings) const;
        [[nodiscard]] const std::wstring& Path() const noexcept;

    private:
        std::wstring path_;
    };
}
