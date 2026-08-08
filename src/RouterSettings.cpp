#include "RouterSettings.h"

#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace
{
    std::wstring ReadString(const std::wstring& path, const wchar_t* key)
    {
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetPrivateProfileStringW(L"Routing", key, L"", buffer.data(),
                                                      static_cast<DWORD>(buffer.size()),
                                                      path.c_str());
        buffer.resize(length);
        return buffer;
    }

    int ReadDelay(const std::wstring& path, const wchar_t* key)
    {
        return std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Routing", key, 0,
                                                                 path.c_str())),
                          0, 2000);
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
        settings.frontDelayMs = ReadDelay(path_, L"FrontDelayMs");
        settings.rearDelayMs = ReadDelay(path_, L"RearDelayMs");
        return settings;
    }

    void RouterSettingsStore::Save(const RouterSettings& settings) const
    {
        WriteString(path_, L"FrontEndpointId", settings.frontEndpointId);
        WriteString(path_, L"RearEndpointId", settings.rearEndpointId);
        WriteString(path_, L"FrontDelayMs", std::to_wstring(settings.frontDelayMs));
        WriteString(path_, L"RearDelayMs", std::to_wstring(settings.rearDelayMs));
    }

    const std::wstring& RouterSettingsStore::Path() const noexcept
    {
        return path_;
    }
}
