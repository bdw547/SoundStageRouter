#pragma once

#include <string>

namespace soundstage
{
    struct RouterSettings
    {
        std::wstring frontEndpointId;
        std::wstring rearEndpointId;
        int frontDelayMs = 0;
        int rearDelayMs = 0;
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
