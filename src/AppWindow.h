#pragma once

#include "AudioEndpoints.h"
#include "RouterSettings.h"
#include "audio/AudioEngineCoordinator.h"
#include "audio/SurroundUiState.h"
#include "ui/CommandDeckTheme.h"

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
        void ApplyThemeFonts() const;
        void PaintWindow(HDC dc) const;
        void SetTechnicalDetailsExpanded(bool expanded);
        void UpdateTechnicalDetailsVisibility() const;
        [[nodiscard]] bool RefreshDevices();
        void PopulateControls();
        void UpdateModeControls();
        void UpdateSurroundLevelLabels() const;
        void SaveSurroundLevels();
        void SaveSettings();
        void StartTest();
        [[nodiscard]] std::optional<audio::RunConfiguration>
            BuildRunConfiguration() const;
        void RenderEngineStatus(const audio::EngineStatus& status);
        void SetPlaybackControlsEnabled(bool selectable);
        void SetStatus(
            const std::wstring& text,
            audio::UiSeverity severity = audio::UiSeverity::Neutral);
        [[nodiscard]] COLORREF SeverityColor(
            audio::UiSeverity severity) const noexcept;
        int FindEndpoint(const std::wstring& id) const;
        int SelectedEndpointIndex(HWND combo) const;
        int ReadDelay(HWND edit) const;
        int ReadLevel(HWND edit) const;
        int ReadSurroundLevel(HWND trackbar) const;
        void ApplyFont(HWND control, HFONT font) const;

        HINSTANCE instance_ = nullptr;
        HWND window_ = nullptr;
        HWND title_ = nullptr;
        HWND subtitle_ = nullptr;
        HWND routeStatus_ = nullptr;
        HWND syncSummary_ = nullptr;
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
        HWND backLevelLabel_ = nullptr;
        HWND backLevel_ = nullptr;
        HWND backLevelValue_ = nullptr;
        HWND sideLevelLabel_ = nullptr;
        HWND sideLevel_ = nullptr;
        HWND sideLevelValue_ = nullptr;
        HWND sideLevelHint_ = nullptr;
        HWND mixTitle_ = nullptr;
        HWND patternLabel_ = nullptr;
        HWND patternCombo_ = nullptr;
        HWND modeLabel_ = nullptr;
        HWND modeCombo_ = nullptr;
        HWND rearFillLabel_ = nullptr;
        HWND rearFillCombo_ = nullptr;
        HWND virtualStatus_ = nullptr;
        HWND formatStatus_ = nullptr;
        HWND refreshButton_ = nullptr;
        HWND saveButton_ = nullptr;
        HWND technicalDetailsButton_ = nullptr;
        HWND startButton_ = nullptr;
        HWND stopButton_ = nullptr;
        HWND frontStatus_ = nullptr;
        HWND rearStatus_ = nullptr;
        HWND syncStatus_ = nullptr;
        HWND status_ = nullptr;

        std::unique_ptr<ui::CommandDeckTheme> theme_;
        audio::UiSeverity formatSeverity_ = audio::UiSeverity::Warning;
        audio::UiSeverity routeSeverity_ = audio::UiSeverity::Warning;
        audio::UiSeverity syncSeverity_ = audio::UiSeverity::Neutral;
        audio::UiSeverity statusSeverity_ = audio::UiSeverity::Neutral;
        bool technicalDetailsExpanded_ = false;
        bool frontCardFault_ = false;
        bool chairCardFault_ = false;
        bool modelRecoveryVisible_ = false;

        std::vector<AudioEndpoint> endpoints_;
        bool hasSingleVirtualEndpoint_ = false;
        audio::VirtualSurroundFormat detectedVirtualFormat_ =
            audio::VirtualSurroundFormat::Unsupported;
        audio::VirtualSurroundFormat routedVirtualFormat_ =
            audio::VirtualSurroundFormat::Unsupported;
        audio::PlaybackState observedPlaybackState_ =
            audio::PlaybackState::Stopped;
        RouterSettingsStore settingsStore_;
        RouterSettings settings_;
        std::unique_ptr<audio::AudioEngineCoordinator> coordinator_;
    };
}
