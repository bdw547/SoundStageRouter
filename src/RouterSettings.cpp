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

    std::optional<int> ParseKeepAliveDb(const std::wstring_view text)
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
            *end != L'\0' || value < -90 || value > -30)
        {
            return std::nullopt;
        }
        return static_cast<int>(value);
    }

    std::optional<int> ParsePercent(const std::wstring_view text)
    {
        if (text.empty())
        {
            return std::nullopt;
        }
        int value = 0;
        for (const wchar_t character : text)
        {
            if (character < L'0' || character > L'9')
            {
                return std::nullopt;
            }
            value = value * 10 + (character - L'0');
            if (value > soundstage::audio::MaximumLevelPercent)
            {
                return std::nullopt;
            }
        }
        return value;
    }

    bool IsKnownPatternString(const std::wstring_view value) noexcept
    {
        return value == L"PairedClicks" ||
               value == L"AlternatingClicks" ||
               value == L"FrontTone" ||
               value == L"RearTone";
    }

    bool IsKnownModeString(const std::wstring_view value) noexcept
    {
        return value == L"SystemAudio" || value == L"TestSignals";
    }

    bool IsKnownRearFillString(const std::wstring_view value) noexcept
    {
        return value == L"Off" || value == L"Duplicate" ||
               value == L"Ambient";
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

    std::wstring_view PlaybackModeToString(
        const audio::PlaybackMode mode) noexcept
    {
        return mode == audio::PlaybackMode::TestSignals
            ? L"TestSignals" : L"SystemAudio";
    }

    std::wstring_view RearFillModeToString(
        const audio::RearFillMode mode) noexcept
    {
        switch (mode)
        {
        case audio::RearFillMode::Duplicate: return L"Duplicate";
        case audio::RearFillMode::Ambient: return L"Ambient";
        default: return L"Off";
        }
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

    RouterSettingsStore::RouterSettingsStore(std::wstring path)
        : path_(std::move(path))
    {
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
        // Front/Rear keys predate the surround controls. Preserve their
        // historical signed decimal parse and clamp-on-load migration.
        const std::optional<int> frontLevel = ParseDelay(
            ReadString(path_, L"FrontLevelPercent", L"100"));
        const std::optional<int> rearLevel = ParseDelay(
            ReadString(path_, L"RearLevelPercent", L"100"));
        const std::optional<int> backLevel = ParsePercent(
            ReadString(path_, L"BackLevelPercent", L"100"));
        const std::optional<int> sideLevel = ParsePercent(
            ReadString(path_, L"SideLevelPercent", L"100"));
        const std::optional<int> keepAliveDb = ParseKeepAliveDb(
            ReadString(path_, L"FrontKeepAliveDb", L"-48"));
        const std::wstring pattern = ReadString(
            path_, L"TestPattern", L"PairedClicks");
        const std::wstring mode = ReadString(
            path_, L"Mode", L"SystemAudio");
        const std::wstring rearFill = ReadString(
            path_, L"RearFill", L"Off");
        settings.frontDelayMs = frontDelay.value_or(0);
        settings.rearDelayMs = rearDelay.value_or(0);
        settings.frontLevelPercent = audio::ClampLevelPercent(
            frontLevel.value_or(100));
        settings.rearLevelPercent = audio::ClampLevelPercent(
            rearLevel.value_or(100));
        settings.backLevelPercent = backLevel.value_or(100);
        settings.sideLevelPercent = sideLevel.value_or(100);
        settings.frontKeepAliveDb = keepAliveDb.value_or(-48);
        settings.lastPattern = TestPatternFromString(pattern);
        settings.mode = mode == L"TestSignals"
            ? audio::PlaybackMode::TestSignals
            : audio::PlaybackMode::SystemAudio;
        settings.rearFill = rearFill == L"Duplicate"
            ? audio::RearFillMode::Duplicate
            : rearFill == L"Ambient"
                ? audio::RearFillMode::Ambient
                : audio::RearFillMode::Off;
        settings.loadAdjustedValues =
            !frontDelay.has_value() || !rearDelay.has_value() ||
            !frontLevel.has_value() || !rearLevel.has_value() ||
            !backLevel.has_value() || !sideLevel.has_value() ||
            !keepAliveDb.has_value() ||
            !IsKnownPatternString(pattern) ||
            !IsKnownModeString(mode) ||
            !IsKnownRearFillString(rearFill);
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
        WriteString(path_, L"FrontLevelPercent", std::to_wstring(
            audio::ClampLevelPercent(settings.frontLevelPercent)));
        WriteString(path_, L"RearLevelPercent", std::to_wstring(
            audio::ClampLevelPercent(settings.rearLevelPercent)));
        WriteString(path_, L"BackLevelPercent", std::to_wstring(
            audio::ClampLevelPercent(settings.backLevelPercent)));
        WriteString(path_, L"SideLevelPercent", std::to_wstring(
            audio::ClampLevelPercent(settings.sideLevelPercent)));
        WriteString(path_, L"FrontKeepAliveDb", std::to_wstring(
            std::clamp(settings.frontKeepAliveDb, -90, -30)));
        WriteString(path_, L"TestPattern",
                    std::wstring(TestPatternToString(settings.lastPattern)));
        WriteString(path_, L"Mode",
                    std::wstring(PlaybackModeToString(settings.mode)));
        WriteString(path_, L"RearFill",
                    std::wstring(RearFillModeToString(settings.rearFill)));
    }

    const std::wstring& RouterSettingsStore::Path() const noexcept
    {
        return path_;
    }
}
