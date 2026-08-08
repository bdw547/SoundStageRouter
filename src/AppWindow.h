#pragma once

#include "AudioEndpoints.h"
#include "RouterSettings.h"

#include <windows.h>

#include <string>
#include <vector>

namespace soundstage
{
    class AppWindow
    {
    public:
        explicit AppWindow(HINSTANCE instance);
        ~AppWindow();

        int Run(int showCommand);

    private:
        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam,
                                                LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void CreateControls();
        void LayoutControls(int width, int height) const;
        void RefreshDevices();
        void PopulateControls();
        void SaveSettings();
        void SetStatus(const std::wstring& text) const;
        int FindEndpoint(const std::wstring& id) const;
        int SelectedEndpointIndex(HWND combo) const;
        int ReadDelay(HWND edit) const;
        void ApplyFont(HWND control, HFONT font) const;

        HINSTANCE instance_ = nullptr;
        HWND window_ = nullptr;
        HWND title_ = nullptr;
        HWND subtitle_ = nullptr;
        HWND deviceList_ = nullptr;
        HWND frontCombo_ = nullptr;
        HWND frontDelay_ = nullptr;
        HWND rearCombo_ = nullptr;
        HWND rearDelay_ = nullptr;
        HWND refreshButton_ = nullptr;
        HWND saveButton_ = nullptr;
        HWND status_ = nullptr;
        HFONT titleFont_ = nullptr;
        HFONT bodyFont_ = nullptr;
        HFONT smallFont_ = nullptr;
        HBRUSH backgroundBrush_ = nullptr;

        std::vector<AudioEndpoint> endpoints_;
        RouterSettings settings_;
        RouterSettingsStore settingsStore_;
    };
}
