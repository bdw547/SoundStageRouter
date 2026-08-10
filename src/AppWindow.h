#pragma once

#include "AudioEndpoints.h"
#include "RouterSettings.h"
#include "audio/AudioEngineCoordinator.h"

#include <windows.h>

#include <memory>
#include <optional>
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
        void UpdateModeControls() const;
        void SaveSettings();
        void StartTest();
        [[nodiscard]] std::optional<audio::RunConfiguration>
            BuildRunConfiguration() const;
        void RenderEngineStatus(const audio::EngineStatus& status) const;
        void SetPlaybackControlsEnabled(bool selectable) const;
        void SetStatus(const std::wstring& text) const;
        int FindEndpoint(const std::wstring& id) const;
        int SelectedEndpointIndex(HWND combo) const;
        int ReadDelay(HWND edit) const;
        int ReadLevel(HWND edit) const;
        void ApplyFont(HWND control, HFONT font) const;

        HINSTANCE instance_ = nullptr;
        HWND window_ = nullptr;
        HWND title_ = nullptr;
        HWND subtitle_ = nullptr;
        HWND deviceList_ = nullptr;
        HWND frontLabel_ = nullptr;
        HWND frontCombo_ = nullptr;
        HWND frontDelayLabel_ = nullptr;
        HWND frontDelay_ = nullptr;
        HWND frontLevelLabel_ = nullptr;
        HWND frontLevel_ = nullptr;
        HWND rearLabel_ = nullptr;
        HWND rearCombo_ = nullptr;
        HWND rearDelayLabel_ = nullptr;
        HWND rearDelay_ = nullptr;
        HWND rearLevelLabel_ = nullptr;
        HWND rearLevel_ = nullptr;
        HWND patternLabel_ = nullptr;
        HWND patternCombo_ = nullptr;
        HWND modeLabel_ = nullptr;
        HWND modeCombo_ = nullptr;
        HWND rearFillLabel_ = nullptr;
        HWND rearFillCombo_ = nullptr;
        HWND virtualStatus_ = nullptr;
        HWND refreshButton_ = nullptr;
        HWND saveButton_ = nullptr;
        HWND startButton_ = nullptr;
        HWND stopButton_ = nullptr;
        HWND frontStatus_ = nullptr;
        HWND rearStatus_ = nullptr;
        HWND syncStatus_ = nullptr;
        HWND status_ = nullptr;
        HFONT titleFont_ = nullptr;
        HFONT bodyFont_ = nullptr;
        HFONT smallFont_ = nullptr;
        HBRUSH backgroundBrush_ = nullptr;

        std::vector<AudioEndpoint> endpoints_;
        RouterSettingsStore settingsStore_;
        RouterSettings settings_;
        std::unique_ptr<audio::AudioEngineCoordinator> coordinator_;
    };
}
