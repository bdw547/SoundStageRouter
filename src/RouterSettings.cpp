#include "RouterSettings.h"

#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace
{
    std::wstring ReadString(const std::wstring& path, const wchar_t* key,
                            const wchar_t* defaultValue = L"")
    {
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetPrivateProfileStringW(L"Routing", key, defaultValue, buffer.data(),
                                                      static_cast<DWORD>(buffer.size()),
                                                      path.c_str());
        buffer.resize(length);
        return buffer;
    }

    std::optional<int> ParseDelay(const std::wstring_view text)
    {
        if (text.empty())
        {
            return std::nullopt;
        }
        std::wstring owned(text);
        wchar_t* end = nullptr;
        errno = 0;
        const long value = std::wcstol(owned.c_str(), &end, 10);
        if (errno == ERANGE || end == owned.c_str() ||
            *end != L'\0' || value < 0 ||
            value > soundstage::audio::MaximumDelayMs)
        {
            return std::nullopt;
        }
        return static_cast<int>(value);
    }

    bool IsKnownPatternString(const std::wstring_view value) noexcept
    {
        return value == L"PairedClicks" ||
               value == L"AlternatingClicks" ||
               value == L"FrontTone" ||
               value == L"RearTone";
    }

    void WriteString(const std::wstring& path, const wchar_t* key,
                     const std::wstring& value)
    {
        if (WritePrivateProfileStringW(L"Routing", key, value.c_str(), path.c_str()) == FALSE)
        {
            throw std::runtime_error("Unable to write the router settings file.");
        }
    }
}

namespace soundstage
{
    std::wstring_view TestPatternToString(
        const audio::TestPattern pattern) noexcept
    {
        switch (pattern)
        {
        case audio::TestPattern::PairedClicks:
            return L"PairedClicks";
        case audio::TestPattern::AlternatingClicks:
            return L"AlternatingClicks";
        case audio::TestPattern::FrontTone:
            return L"FrontTone";
        case audio::TestPattern::RearTone:
            return L"RearTone";
        }
        return L"PairedClicks";
    }

    audio::TestPattern TestPatternFromString(
        const std::wstring_view value) noexcept
    {
        if (value == L"AlternatingClicks")
        {
            return audio::TestPattern::AlternatingClicks;
        }
        if (value == L"FrontTone")
        {
            return audio::TestPattern::FrontTone;
        }
        if (value == L"RearTone")
        {
            return audio::TestPattern::RearTone;
        }
        return audio::TestPattern::PairedClicks;
    }

    RouterSettingsStore::RouterSettingsStore()
    {
        PWSTR localAppData = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr,
                                        &localAppData)))
        {
            throw std::runtime_error("Unable to locate the Local AppData folder.");
        }

        const std::filesystem::path directory =
            std::filesystem::path(localAppData) / L"SoundStageRouter";
        CoTaskMemFree(localAppData);
        std::filesystem::create_directories(directory);
        path_ = (directory / L"routing.ini").wstring();
    }

    RouterSettings RouterSettingsStore::Load() const
    {
        RouterSettings settings;
        settings.frontEndpointId = ReadString(path_, L"FrontEndpointId");
        settings.rearEndpointId = ReadString(path_, L"RearEndpointId");
        const std::optional<int> frontDelay = ParseDelay(
            ReadString(path_, L"FrontDelayMs", L"0"));
        const std::optional<int> rearDelay = ParseDelay(
            ReadString(path_, L"RearDelayMs", L"0"));
        const std::wstring pattern = ReadString(
            path_, L"TestPattern", L"PairedClicks");
        settings.frontDelayMs = frontDelay.value_or(0);
        settings.rearDelayMs = rearDelay.value_or(0);
        settings.lastPattern = TestPatternFromString(pattern);
        settings.loadAdjustedValues =
            !frontDelay.has_value() || !rearDelay.has_value() ||
            !IsKnownPatternString(pattern);
        return settings;
    }

    void RouterSettingsStore::Save(const RouterSettings& settings) const
    {
        WriteString(path_, L"FrontEndpointId", settings.frontEndpointId);
        WriteString(path_, L"RearEndpointId", settings.rearEndpointId);
        WriteString(path_, L"FrontDelayMs", std::to_wstring(
            audio::ClampDelayMs(settings.frontDelayMs)));
        WriteString(path_, L"RearDelayMs", std::to_wstring(
            audio::ClampDelayMs(settings.rearDelayMs)));
        WriteString(path_, L"TestPattern",
                    std::wstring(TestPatternToString(settings.lastPattern)));
    }

    const std::wstring& RouterSettingsStore::Path() const noexcept
    {
        return path_;
    }
}
