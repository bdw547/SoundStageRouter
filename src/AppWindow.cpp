#include "AppWindow.h"
#include "../resources/resource.h"
#include "audio/SurroundUiState.h"
#include "audio/VirtualSurroundContract.h"
#include "audio/WasapiEndpointSession.h"

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace
{
    constexpr wchar_t WindowClassName[] = L"SoundStageRouterMainWindow";

    constexpr int DeviceListId = 1001;
    constexpr int FrontComboId = 1002;
    constexpr int FrontDelayId = 1003;
    constexpr int RearComboId = 1004;
    constexpr int RearDelayId = 1005;
    constexpr int RefreshButtonId = 1006;
    constexpr int SaveButtonId = 1007;
    constexpr int PatternComboId = 1008;
    constexpr int StartButtonId = 1009;
    constexpr int StopButtonId = 1010;
    constexpr int ModeComboId = 1011;
    constexpr int RearFillComboId = 1012;
    constexpr int FrontLevelId = 1013;
    constexpr int RearLevelId = 1014;
    constexpr int BackLevelId = 1015;
    constexpr int SideLevelId = 1016;
    constexpr int TechnicalDetailsButtonId = 1017;
    constexpr UINT_PTR StatusTimerId = 1;
    constexpr UINT StatusTimerPeriodMs = 250;
    constexpr UINT TrayIconMessage = WM_APP + 1;
    constexpr UINT TrayIconId = 1;
    constexpr int TrayOpenCommandId = 2001;
    constexpr int TrayToggleRoutingCommandId = 2002;
    constexpr int TrayExitCommandId = 2003;
    constexpr unsigned AutoStartRetryIntervalMs = 5000;

    HMENU ControlId(const int value)
    {
        return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value));
    }

    HWND CreateLabel(HWND parent, const wchar_t* text, DWORD style = SS_LEFT)
    {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                               parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    // WM_SETTEXT repaints a control even when the text is identical; the
    // 250 ms status timer must not repaint anything that did not change.
    bool SetTextIfChanged(const HWND control, const std::wstring& text)
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring current;
        if (length > 0)
        {
            current.resize(static_cast<std::size_t>(length) + 1);
            const int copied = GetWindowTextW(
                control, current.data(), static_cast<int>(current.size()));
            current.resize(static_cast<std::size_t>(std::max(copied, 0)));
        }
        if (current == text)
        {
            return false;
        }
        SetWindowTextW(control, text.c_str());
        return true;
    }

    void InsertColumn(HWND list, int index, int width, const wchar_t* title)
    {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.iSubItem = index;
        column.cx = width;
        column.pszText = const_cast<LPWSTR>(title);
        ListView_InsertColumn(list, index, &column);
    }

    void SetListCell(HWND list, int row, int column, const std::wstring& value)
    {
        ListView_SetItemText(list, row, column, const_cast<LPWSTR>(value.c_str()));
    }
}

namespace soundstage
{
    AppWindow::AppWindow(const HINSTANCE instance, const bool backgroundMode)
        : instance_(instance),
          theme_(std::make_unique<ui::CommandDeckTheme>(GetDpiForSystem())),
          backgroundMode_(backgroundMode),
          autoStartArmed_(backgroundMode),
          settings_(settingsStore_.Load()),
          coordinator_(std::make_unique<audio::AudioEngineCoordinator>())
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 0, 0,
            LR_DEFAULTSIZE));
        windowClass.hIconSm = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR));
        if (windowClass.hIcon == nullptr)
        {
            windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = WindowClassName;
        windowClass.style = CS_HREDRAW | CS_VREDRAW;

        if (RegisterClassExW(&windowClass) == 0)
        {
            throw std::runtime_error("Unable to register the application window.");
        }

        constexpr DWORD windowStyle =
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL;
        constexpr DWORD extendedStyle = WS_EX_CONTROLPARENT;
        RECT initialBounds{
            0, 0, theme_->Scale(1240), theme_->Scale(780)};
        AdjustWindowRectExForDpi(
            &initialBounds, windowStyle, FALSE, extendedStyle, theme_->Dpi());
        int initialWidth = initialBounds.right - initialBounds.left;
        int initialHeight = initialBounds.bottom - initialBounds.top;
        RECT workArea{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0))
        {
            initialWidth = std::min(
                initialWidth,
                static_cast<int>(workArea.right - workArea.left));
            initialHeight = std::min(
                initialHeight,
                static_cast<int>(workArea.bottom - workArea.top));
        }
        window_ = CreateWindowExW(
            extendedStyle, WindowClassName, L"SoundStage Router",
            windowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
            initialWidth, initialHeight,
            nullptr, nullptr, instance_, this);
        if (window_ == nullptr)
        {
            throw std::runtime_error("Unable to create the application window.");
        }
    }

    AppWindow::~AppWindow()
    {
        if (coordinator_)
        {
            coordinator_->PostStop();
            coordinator_.reset();
        }
        UnregisterClassW(WindowClassName, instance_);
    }

    int AppWindow::Run(const int showCommand)
    {
        ShowWindow(window_, showCommand);
        UpdateWindow(window_);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(window_, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return static_cast<int>(message.wParam);
    }

    LRESULT CALLBACK AppWindow::WindowProcedure(const HWND window, const UINT message,
                                                const WPARAM wParam, const LPARAM lParam)
    {
        AppWindow* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<AppWindow*>(create->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        return self == nullptr ? DefWindowProcW(window, message, wParam, lParam)
                               : self->HandleMessage(message, wParam, lParam);
    }

    LRESULT AppWindow::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            theme_->SetDpi(GetDpiForWindow(window_));
            CreateControls();
            static_cast<void>(RefreshDevices());
            taskbarCreatedMessage_ =
                RegisterWindowMessageW(L"TaskbarCreated");
            AddTrayIcon();
            SetTimer(window_, StatusTimerId, StatusTimerPeriodMs, nullptr);
            return 0;
        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const auto layout = ui::ComputeCommandDeckLayout(
                theme_->Scale(1240), theme_->Scale(780), theme_->Dpi(),
                technicalDetailsExpanded_);
            RECT bounds{0, 0, layout.minimumClientSize.cx,
                        layout.minimumClientSize.cy};
            AdjustWindowRectExForDpi(
                &bounds,
                static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)),
                FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)),
                theme_->Dpi());
            int minWidth = bounds.right - bounds.left;
            int minHeight = bounds.bottom - bounds.top;
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(
                    MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST),
                    &monitorInfo))
            {
                minWidth = std::min(
                    minWidth,
                    static_cast<int>(monitorInfo.rcWork.right -
                                     monitorInfo.rcWork.left));
                minHeight = std::min(
                    minHeight,
                    static_cast<int>(monitorInfo.rcWork.bottom -
                                     monitorInfo.rcWork.top));
            }
            info->ptMinTrackSize.x = minWidth;
            info->ptMinTrackSize.y = minHeight;
            return 0;
        }
        case WM_VSCROLL:
            if (lParam == 0)
            {
                HandleVerticalScroll(LOWORD(wParam));
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            SetScrollOffset(
                scrollOffset_ -
                GET_WHEEL_DELTA_WPARAM(wParam) * theme_->Scale(96) /
                    WHEEL_DELTA);
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                LayoutControls(LOWORD(lParam), HIWORD(lParam));
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED:
        {
            EnumChildWindows(
                window_,
                [](const HWND child, LPARAM) -> BOOL {
                    SendMessageW(
                        child, WM_SETFONT,
                        reinterpret_cast<WPARAM>(
                            GetStockObject(DEFAULT_GUI_FONT)),
                        FALSE);
                    return TRUE;
                },
                0);
            theme_->SetDpi(HIWORD(wParam));
            ApplyThemeFonts();
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(
                window_, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            InvalidateRect(window_, nullptr, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ModeComboId &&
                HIWORD(wParam) == CBN_SELCHANGE)
            {
                UpdateModeControls();
                return 0;
            }
            if (HIWORD(wParam) == EN_CHANGE && coordinator_ &&
                coordinator_->Status()->state == audio::PlaybackState::Running)
            {
                if (LOWORD(wParam) == FrontDelayId)
                {
                    coordinator_->PostDelay(
                        audio::SpeakerRole::Front,
                        audio::ClampDelayMs(ReadDelay(frontDelay_)));
                }
                else if (LOWORD(wParam) == RearDelayId)
                {
                    coordinator_->PostDelay(
                        audio::SpeakerRole::Rear,
                        audio::ClampDelayMs(ReadDelay(rearDelay_)));
                }
            }
            switch (LOWORD(wParam))
            {
            case RefreshButtonId:
                static_cast<void>(RefreshDevices());
                return 0;
            case SaveButtonId:
                SaveSettings();
                return 0;
            case TechnicalDetailsButtonId:
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    SetTechnicalDetailsExpanded(!technicalDetailsExpanded_);
                }
                return 0;
            case StartButtonId:
                autoStartArmed_ = backgroundMode_;
                StartTest();
                return 0;
            case StopButtonId:
                autoStartArmed_ = false;
                if (coordinator_) coordinator_->PostStop();
                return 0;
            case TrayOpenCommandId:
                ShowFromTray();
                return 0;
            case TrayToggleRoutingCommandId:
                if (coordinator_ &&
                    coordinator_->Status()->state !=
                        audio::PlaybackState::Stopped &&
                    coordinator_->Status()->state !=
                        audio::PlaybackState::Faulted)
                {
                    autoStartArmed_ = false;
                    coordinator_->PostStop();
                }
                else
                {
                    autoStartArmed_ = backgroundMode_;
                    StartTest();
                }
                return 0;
            case TrayExitCommandId:
                DestroyWindow(window_);
                return 0;
            default:
                break;
            }
            break;
        case TrayIconMessage:
            HandleTrayMessage(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE)
            {
                HideToTray();
                return 0;
            }
            break;
        case WM_CLOSE:
            if (backgroundMode_)
            {
                HideToTray();
                if (!closeToTrayNoticeShown_)
                {
                    closeToTrayNoticeShown_ = true;
                    ShowTrayBalloon(
                        L"Still running",
                        L"SoundStage Router keeps routing from the "
                        L"notification area. Use Exit in the tray menu to "
                        L"quit.",
                        false);
                }
                return 0;
            }
            break;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == backLevel_ ||
                reinterpret_cast<HWND>(lParam) == sideLevel_)
            {
                SaveSurroundLevels();
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == StatusTimerId && coordinator_)
            {
                const auto status = coordinator_->Status();
                const audio::PlaybackState previousState =
                    observedPlaybackState_;
                if (audio::ShouldRefreshSurroundDiscovery(
                        observedPlaybackState_, status->state))
                {
                    static_cast<void>(RefreshDevices());
                }
                observedPlaybackState_ = status->state;
                RenderEngineStatus(*status);
                if (status->state == audio::PlaybackState::Running)
                {
                    faultBalloonShown_ = false;
                }
                else if (previousState != audio::PlaybackState::Faulted &&
                         status->state == audio::PlaybackState::Faulted &&
                         !IsWindowVisible(window_) && !faultBalloonShown_)
                {
                    faultBalloonShown_ = true;
                    ShowTrayBalloon(
                        L"Routing stopped",
                        status->lastFault.message.empty()
                            ? L"Review the issue in SoundStage Router."
                            : status->lastFault.message,
                        true);
                }
                TryBackgroundStart();
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            // WM_PAINT covers the full invalid region from a memory
            // surface; erasing here would just flash the background.
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window_, &paint);
            PaintWindow(dc);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_DRAWITEM:
        {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item != nullptr && item->CtlType == ODT_BUTTON)
            {
                const bool primary =
                    item->CtlID == StartButtonId || item->CtlID == StopButtonId;
                const bool active =
                    item->CtlID == TechnicalDetailsButtonId &&
                    technicalDetailsExpanded_;
                theme_->PaintButton(*item, primary, active);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX:
        {
            const HWND control = reinterpret_cast<HWND>(lParam);
            COLORREF color = theme_->Colors().secondary;
            COLORREF background = theme_->Colors().card;
            if (control == title_ || control == subtitle_ ||
                control == formatStatus_ || control == routeStatus_ ||
                control == syncSummary_ || control == status_)
            {
                background = theme_->Colors().cardRaised;
            }
            if (control == title_ || control == frontLabel_ ||
                control == rearLabel_ || control == mixTitle_ ||
                control == backLevelLabel_ || control == sideLevelLabel_ ||
                control == backLevelValue_ || control == sideLevelValue_ ||
                message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX)
            {
                color = theme_->Colors().text;
            }
            if (control == formatStatus_)
            {
                color = SeverityColor(formatSeverity_);
            }
            else if (control == routeStatus_)
            {
                color = SeverityColor(routeSeverity_);
            }
            else if (control == syncSummary_)
            {
                color = SeverityColor(syncSeverity_);
            }
            else if (control == status_)
            {
                color = SeverityColor(statusSeverity_);
            }
            if ((control == sideLevelLabel_ ||
                 control == sideLevelValue_) &&
                sideLevel_ != nullptr && !IsWindowEnabled(sideLevel_))
            {
                color = theme_->Colors().secondary;
            }
            return reinterpret_cast<LRESULT>(
                theme_->PrepareControl(message,
                                       reinterpret_cast<HDC>(wParam),
                                       control, color, background));
        }
        case WM_DESTROY:
            KillTimer(window_, StatusTimerId);
            RemoveTrayIcon();
            if (coordinator_)
            {
                coordinator_->PostStop();
                coordinator_.reset();
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        if (taskbarCreatedMessage_ != 0 &&
            message == taskbarCreatedMessage_)
        {
            // Explorer restarted; the notification area lost our icon.
            trayIconAdded_ = false;
            AddTrayIcon();
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void AppWindow::CreateControls()
    {
        title_ = CreateLabel(window_, L"SoundStage Router");
        subtitle_ = CreateLabel(window_,
            L"Front + Chair synchronized surround command deck");
        routeStatus_ = CreateLabel(window_, L"Setup required", SS_CENTER);
        syncSummary_ = CreateLabel(
            window_, L"Synchronizing outputs...", SS_CENTER);

        deviceList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, window_, ControlId(DeviceListId), instance_, nullptr);
        ListView_SetExtendedListViewStyle(deviceList_,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        InsertColumn(deviceList_, 0, 350, L"Output device");
        InsertColumn(deviceList_, 1, 280, L"Windows mix format");
        InsertColumn(deviceList_, 2, 110, L"Role");

        frontLabel_ = CreateLabel(
            window_, L"Front  -  soundbar / monitor");
        frontCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(FrontComboId), instance_, nullptr);
        frontDelayLabel_ = CreateLabel(window_, L"Delay (ms)");
        frontDelay_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | WS_TABSTOP,
            0, 0, 0, 0, window_, ControlId(FrontDelayId), instance_, nullptr);
        frontLevelLabel_ = CreateLabel(window_, L"Level (%)");
        frontLevel_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"100",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | WS_TABSTOP,
            0, 0, 0, 0, window_, ControlId(FrontLevelId),
            instance_, nullptr);

        rearLabel_ = CreateLabel(
            window_, L"Chair  -  Bluetooth headrest");
        rearCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(RearComboId), instance_, nullptr);
        rearDelayLabel_ = CreateLabel(window_, L"Delay (ms)");
        rearDelay_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | WS_TABSTOP,
            0, 0, 0, 0, window_, ControlId(RearDelayId), instance_, nullptr);
        rearLevelLabel_ = CreateLabel(window_, L"Level (%)");
        rearLevel_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"100",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | WS_TABSTOP,
            0, 0, 0, 0, window_, ControlId(RearLevelId),
            instance_, nullptr);

        modeLabel_ = CreateLabel(window_, L"Source mode");
        modeCombo_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(ModeComboId),
            instance_, nullptr);
        ComboBox_AddString(modeCombo_, L"System audio (virtual surround)");
        ComboBox_AddString(modeCombo_, L"Test signals");
        ComboBox_SetCurSel(
            modeCombo_,
            settings_.mode == audio::PlaybackMode::SystemAudio ? 0 : 1);
        patternLabel_ = CreateLabel(window_, L"Test pattern");
        patternCombo_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(PatternComboId),
            instance_, nullptr);
        rearFillLabel_ = CreateLabel(window_, L"Rear fill");
        rearFillCombo_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(RearFillComboId),
            instance_, nullptr);
        ComboBox_AddString(rearFillCombo_, L"Off");
        ComboBox_AddString(rearFillCombo_, L"Duplicate (-6 dB)");
        ComboBox_AddString(rearFillCombo_, L"Ambient difference");
        ComboBox_SetCurSel(
            rearFillCombo_, static_cast<int>(settings_.rearFill));
        virtualStatus_ = CreateLabel(
            window_,
            L"Driver status: checking. Set Windows default output to "
            L"SoundStage Router Surround.");
        formatStatus_ = CreateLabel(
            window_, L"Surround format unavailable", SS_CENTER);

        mixTitle_ = CreateLabel(window_, L"Chair Mix");
        backLevelLabel_ = CreateLabel(window_, L"Back Level");
        backLevel_ = CreateWindowExW(
            0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
            0, 0, 0, 0, window_, ControlId(BackLevelId),
            instance_, nullptr);
        backLevelValue_ = CreateLabel(window_, L"100%", SS_RIGHT);
        sideLevelLabel_ = CreateLabel(window_, L"Side Level");
        sideLevel_ = CreateWindowExW(
            0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
            0, 0, 0, 0, window_, ControlId(SideLevelId),
            instance_, nullptr);
        sideLevelValue_ = CreateLabel(window_, L"100%", SS_RIGHT);
        sideLevelHint_ = CreateLabel(
            window_, L"Used when Windows is set to 7.1.");
        for (const HWND trackbar : {backLevel_, sideLevel_})
        {
            SendMessageW(trackbar, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendMessageW(trackbar, TBM_SETTICFREQ, 10, 0);
            SendMessageW(trackbar, TBM_SETPAGESIZE, 0, 10);
        }
        SendMessageW(backLevel_, TBM_SETPOS, TRUE,
                     settings_.backLevelPercent);
        SendMessageW(sideLevel_, TBM_SETPOS, TRUE,
                     settings_.sideLevelPercent);
        UpdateSurroundLevelLabels();
        for (const wchar_t* pattern : {
                 L"Paired clicks", L"Alternating clicks",
                 L"Front tone", L"Chair tone"})
        {
            ComboBox_AddString(patternCombo_, pattern);
        }
        ComboBox_SetCurSel(
            patternCombo_, static_cast<int>(settings_.lastPattern));

        technicalDetailsButton_ = CreateWindowExW(
            0, L"BUTTON", L"Technical details",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_, ControlId(TechnicalDetailsButtonId),
            instance_, nullptr);
        refreshButton_ = CreateWindowExW(0, L"BUTTON", L"Refresh devices",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_, ControlId(RefreshButtonId), instance_, nullptr);
        saveButton_ = CreateWindowExW(0, L"BUTTON", L"Save layout",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_, ControlId(SaveButtonId), instance_, nullptr);
        startButton_ = CreateWindowExW(
            0, L"BUTTON", L"Start Routing",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_, ControlId(StartButtonId),
            instance_, nullptr);
        stopButton_ = CreateWindowExW(
            0, L"BUTTON", L"Stop Routing",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_, ControlId(StopButtonId),
            instance_, nullptr);
        EnableWindow(stopButton_, FALSE);
        frontStatus_ = CreateLabel(window_, L"Front: stopped");
        rearStatus_ = CreateLabel(window_, L"Rear: stopped");
        syncStatus_ = CreateLabel(
            window_, L"Clock correction: settling");
        status_ = CreateLabel(window_, L"", SS_LEFT);

        for (const HWND interactive : {
                 deviceList_, frontCombo_, frontDelay_, frontLevel_,
                 rearCombo_, rearDelay_, rearLevel_, backLevel_, sideLevel_,
                 modeCombo_, rearFillCombo_, patternCombo_, refreshButton_,
                 saveButton_, technicalDetailsButton_, startButton_, stopButton_})
        {
            SetWindowTheme(interactive, L"", L"");
        }
        ListView_SetBkColor(deviceList_, theme_->Colors().cardRaised);
        ListView_SetTextBkColor(deviceList_, theme_->Colors().cardRaised);
        ListView_SetTextColor(deviceList_, theme_->Colors().text);
        ApplyThemeFonts();
        UpdateTechnicalDetailsVisibility();
        UpdateModeControls();
    }

    void AppWindow::LayoutControls(const int width, const int height)
    {
        const auto layout = ui::ComputeCommandDeckLayout(
            width, height, theme_->Dpi(), technicalDetailsExpanded_);
        const int maxScroll = std::max(0, layout.virtualHeight - height);
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll);
        const int shift = scrollOffset_;
        const auto scale = [this](const int value) {
            return theme_->Scale(value);
        };
        // One atomic reposition pass keeps resize and scroll repaints from
        // landing control-by-control.
        HDWP positions = BeginDeferWindowPos(44);
        const auto move = [&positions, shift](
                              const HWND control, const int x, const int y,
                              const int controlWidth,
                              const int controlHeight) {
            const int width = std::max(0, controlWidth);
            const int height = std::max(0, controlHeight);
            if (positions != nullptr)
            {
                positions = DeferWindowPos(
                    positions, control, nullptr, x, y - shift, width, height,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (positions == nullptr)
            {
                MoveWindow(control, x, y - shift, width, height, TRUE);
            }
        };

        if (layout.stacked)
        {
            const int pad = scale(12);
            const int badgeWidth = scale(200);
            const int badgeLeft = layout.header.right - pad - badgeWidth;
            const int titleLeft = layout.header.left + pad;
            move(title_, titleLeft, layout.header.top + scale(2),
                 badgeLeft - titleLeft - scale(12), scale(34));
            ShowWindow(subtitle_, SW_HIDE);
            move(syncSummary_, titleLeft,
                 layout.header.top + scale(34),
                 badgeLeft - titleLeft - scale(12), scale(18));
            move(formatStatus_, badgeLeft,
                 layout.header.top + scale(5), badgeWidth, scale(20));
            move(routeStatus_, badgeLeft,
                 layout.header.top + scale(30), badgeWidth, scale(20));
        }
        else
        {
            const int pad = scale(20);
            const int formatWidth = scale(190);
            const int routeWidth = scale(150);
            const int badgeGap = scale(12);
            const int routeLeft = layout.header.right - pad - routeWidth;
            const int formatLeft = routeLeft - badgeGap - formatWidth;
            const int titleLeft = layout.header.left + pad;
            move(title_, titleLeft, layout.header.top + scale(7),
                 formatLeft - titleLeft - scale(16), scale(38));
            ShowWindow(subtitle_, SW_SHOW);
            move(subtitle_, titleLeft,
                 layout.header.top + scale(45),
                 formatLeft - titleLeft - scale(16), scale(19));
            move(routeStatus_, routeLeft,
                 layout.header.top + scale(13), routeWidth, scale(24));
            move(formatStatus_, formatLeft,
                 layout.header.top + scale(13), formatWidth, scale(24));
            move(syncSummary_, formatLeft,
                 layout.header.top + scale(42),
                 formatWidth + badgeGap + routeWidth, scale(18));
        }

        const auto placeDeviceCard = [&](const RECT& card,
                                         const HWND heading,
                                         const HWND combo,
                                         const HWND delayLabel,
                                         const HWND delay,
                                         const HWND levelLabel,
                                         const HWND level) {
            const int pad = scale(layout.stacked ? 14 : 20);
            const int fieldGap = scale(12);
            const int labelHeight = scale(18);
            const int fieldHeight = scale(layout.stacked ? 30 : 34);
            const int innerWidth = card.right - card.left - pad * 2;
            const int fieldWidth = (innerWidth - fieldGap) / 2;
            const int fieldTop = card.bottom - pad - fieldHeight;
            move(heading, card.left + pad,
                 card.top + scale(layout.stacked ? 9 : 17),
                 innerWidth, scale(24));
            move(combo, card.left + pad,
                 card.top + scale(layout.stacked ? 37 : 53),
                 innerWidth, scale(200));
            move(delayLabel, card.left + pad,
                 fieldTop - labelHeight - scale(3),
                 fieldWidth, labelHeight);
            move(delay, card.left + pad, fieldTop,
                 fieldWidth, fieldHeight);
            move(levelLabel, card.left + pad + fieldWidth + fieldGap,
                 fieldTop - labelHeight - scale(3),
                 fieldWidth, labelHeight);
            move(level, card.left + pad + fieldWidth + fieldGap,
                 fieldTop, fieldWidth, fieldHeight);
        };
        placeDeviceCard(layout.frontCard, frontLabel_, frontCombo_,
                        frontDelayLabel_, frontDelay_,
                        frontLevelLabel_, frontLevel_);
        placeDeviceCard(layout.chairCard, rearLabel_, rearCombo_,
                        rearDelayLabel_, rearDelay_,
                        rearLevelLabel_, rearLevel_);

        if (layout.stacked)
        {
            const int pad = scale(14);
            const int gap = scale(12);
            const int innerWidth = layout.mixCard.right -
                layout.mixCard.left - pad * 2;
            const int columnWidth = (innerWidth - gap) / 2;
            const int rightColumn = layout.mixCard.left + pad + columnWidth + gap;
            move(mixTitle_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(9), innerWidth, scale(24));
            move(modeLabel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(40), columnWidth, scale(18));
            move(modeCombo_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(59), columnWidth, scale(180));
            move(rearFillLabel_, rightColumn,
                 layout.mixCard.top + scale(40), columnWidth, scale(18));
            move(rearFillCombo_, rightColumn,
                 layout.mixCard.top + scale(59), columnWidth, scale(180));
            move(patternLabel_, rightColumn,
                 layout.mixCard.top + scale(40), columnWidth, scale(18));
            move(patternCombo_, rightColumn,
                 layout.mixCard.top + scale(59), columnWidth, scale(180));

            const int valueWidth = scale(48);
            move(backLevelLabel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(105),
                 columnWidth - valueWidth, scale(20));
            move(backLevelValue_,
                 layout.mixCard.left + pad + columnWidth - valueWidth,
                 layout.mixCard.top + scale(105), valueWidth, scale(20));
            move(backLevel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(126), columnWidth, scale(38));
            move(sideLevelLabel_, rightColumn,
                 layout.mixCard.top + scale(105),
                 columnWidth - valueWidth, scale(20));
            move(sideLevelValue_, rightColumn + columnWidth - valueWidth,
                 layout.mixCard.top + scale(105), valueWidth, scale(20));
            move(sideLevel_, rightColumn,
                 layout.mixCard.top + scale(126), columnWidth, scale(38));
            move(sideLevelHint_, rightColumn,
                 layout.mixCard.top + scale(166), columnWidth, scale(36));
        }
        else
        {
            const int pad = scale(20);
            const int innerWidth = layout.mixCard.right -
                layout.mixCard.left - pad * 2;
            const int valueWidth = scale(52);
            move(mixTitle_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(17), innerWidth, scale(24));
            move(modeLabel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(58), innerWidth, scale(18));
            move(modeCombo_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(79), innerWidth, scale(180));
            move(rearFillLabel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(126), innerWidth, scale(18));
            move(rearFillCombo_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(147), innerWidth, scale(180));
            move(patternLabel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(126), innerWidth, scale(18));
            move(patternCombo_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(147), innerWidth, scale(180));
            move(backLevelLabel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(211),
                 innerWidth - valueWidth, scale(20));
            move(backLevelValue_, layout.mixCard.right - pad - valueWidth,
                 layout.mixCard.top + scale(211), valueWidth, scale(20));
            move(backLevel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(233), innerWidth, scale(42));
            move(sideLevelLabel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(294),
                 innerWidth - valueWidth, scale(20));
            move(sideLevelValue_, layout.mixCard.right - pad - valueWidth,
                 layout.mixCard.top + scale(294), valueWidth, scale(20));
            move(sideLevel_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(316), innerWidth, scale(42));
            move(sideLevelHint_, layout.mixCard.left + pad,
                 layout.mixCard.top + scale(362), innerWidth, scale(38));
        }

        const int actionPad = scale(layout.stacked ? 12 : 16);
        const int buttonGap = scale(layout.stacked ? 8 : 12);
        const int buttonHeight = scale(layout.stacked ? 38 : 44);
        // Stacked mode uses two rows: full-width status above the buttons.
        const int buttonTop = layout.stacked
            ? layout.actionBar.bottom - actionPad - buttonHeight
            : layout.actionBar.top +
                (layout.actionBar.bottom - layout.actionBar.top -
                 buttonHeight) / 2;
        const int primaryWidth = scale(layout.stacked ? 140 : 170);
        const int saveWidth = scale(layout.stacked ? 88 : 110);
        const int refreshWidth = scale(layout.stacked ? 90 : 136);
        const int detailsWidth = scale(layout.stacked ? 128 : 160);
        int buttonRight = layout.actionBar.right - actionPad;
        const int primaryLeft = buttonRight - primaryWidth;
        move(startButton_, primaryLeft, buttonTop,
             primaryWidth, buttonHeight);
        move(stopButton_, primaryLeft, buttonTop,
             primaryWidth, buttonHeight);
        buttonRight = primaryLeft - buttonGap;
        const int saveLeft = buttonRight - saveWidth;
        move(saveButton_, saveLeft, buttonTop, saveWidth, buttonHeight);
        buttonRight = saveLeft - buttonGap;
        const int refreshLeft = buttonRight - refreshWidth;
        move(refreshButton_, refreshLeft, buttonTop,
             refreshWidth, buttonHeight);
        buttonRight = refreshLeft - buttonGap;
        const int detailsLeft = buttonRight - detailsWidth;
        move(technicalDetailsButton_, detailsLeft, buttonTop,
             detailsWidth, buttonHeight);
        if (layout.stacked)
        {
            move(status_, layout.actionBar.left + actionPad,
                 layout.actionBar.top + scale(8),
                 layout.actionBar.right - layout.actionBar.left -
                     actionPad * 2,
                 scale(42));
        }
        else
        {
            move(status_, layout.actionBar.left + actionPad,
                 layout.actionBar.top + scale(17),
                 std::max(scale(60), detailsLeft - buttonGap -
                     static_cast<int>(layout.actionBar.left) - actionPad),
                 scale(54));
        }
        SetTextIfChanged(refreshButton_,
                         layout.stacked ? L"Refresh" : L"Refresh devices");

        const int detailsPad = scale(16);
        const int detailsWidthPixels = layout.detailsCard.right -
            layout.detailsCard.left - detailsPad * 2;
        move(virtualStatus_, layout.detailsCard.left + detailsPad,
             layout.detailsCard.top + scale(12),
             detailsWidthPixels, scale(32));
        move(deviceList_, layout.detailsCard.left + detailsPad,
             layout.detailsCard.top + scale(47),
             detailsWidthPixels, scale(78));
        move(frontStatus_, layout.detailsCard.left + detailsPad,
             layout.detailsCard.top + scale(133),
             detailsWidthPixels, scale(28));
        move(rearStatus_, layout.detailsCard.left + detailsPad,
             layout.detailsCard.top + scale(164),
             detailsWidthPixels, scale(28));
        move(syncStatus_, layout.detailsCard.left + detailsPad,
             layout.detailsCard.top + scale(195),
             detailsWidthPixels, scale(32));
        if (positions != nullptr)
        {
            EndDeferWindowPos(positions);
        }
        if (detailsWidthPixels > 0)
        {
            ListView_SetColumnWidth(deviceList_, 0, detailsWidthPixels / 2);
            ListView_SetColumnWidth(deviceList_, 1,
                                    detailsWidthPixels * 35 / 100);
            ListView_SetColumnWidth(deviceList_, 2,
                                    detailsWidthPixels * 15 / 100);
        }

        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        scrollInfo.nMin = 0;
        scrollInfo.nMax = layout.virtualHeight - 1;
        scrollInfo.nPage = static_cast<UINT>(std::max(0, height));
        scrollInfo.nPos = scrollOffset_;
        SetScrollInfo(window_, SB_VERT, &scrollInfo, TRUE);
    }

    void AppWindow::SetScrollOffset(const int offset)
    {
        RECT client{};
        GetClientRect(window_, &client);
        const auto layout = ui::ComputeCommandDeckLayout(
            client.right, client.bottom, theme_->Dpi(),
            technicalDetailsExpanded_);
        const int maxScroll = std::max(
            0, layout.virtualHeight - static_cast<int>(client.bottom));
        const int clamped = std::clamp(offset, 0, maxScroll);
        if (clamped == scrollOffset_)
        {
            return;
        }
        scrollOffset_ = clamped;
        LayoutControls(client.right, client.bottom);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void AppWindow::HandleVerticalScroll(const int request)
    {
        RECT client{};
        GetClientRect(window_, &client);
        const int line = theme_->Scale(48);
        int target = scrollOffset_;
        switch (request)
        {
        case SB_LINEUP: target -= line; break;
        case SB_LINEDOWN: target += line; break;
        case SB_PAGEUP: target -= client.bottom; break;
        case SB_PAGEDOWN: target += client.bottom; break;
        case SB_TOP: target = 0; break;
        case SB_BOTTOM: target = INT_MAX / 2; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
        {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(window_, SB_VERT, &info))
            {
                target = info.nTrackPos;
            }
            break;
        }
        default:
            return;
        }
        SetScrollOffset(target);
    }

    void AppWindow::ApplyThemeFonts() const
    {
        for (const HWND child : {
                 subtitle_, routeStatus_, syncSummary_, deviceList_,
                 frontCombo_, frontDelayLabel_, frontDelay_,
                 frontLevelLabel_, frontLevel_, rearCombo_, rearDelayLabel_,
                 rearDelay_, rearLevelLabel_, rearLevel_, backLevelLabel_,
                 backLevel_, backLevelValue_, sideLevelLabel_, sideLevel_,
                 sideLevelValue_, modeLabel_, modeCombo_, rearFillLabel_,
                 rearFillCombo_, virtualStatus_, formatStatus_, patternLabel_,
                 patternCombo_, refreshButton_, saveButton_,
                 technicalDetailsButton_, startButton_, stopButton_,
                 frontStatus_, rearStatus_, syncStatus_, status_,
                 sideLevelHint_})
        {
            ApplyFont(child, theme_->BodyFont());
        }
        ApplyFont(title_, theme_->TitleFont());
        ApplyFont(frontLabel_, theme_->HeadingFont());
        ApplyFont(rearLabel_, theme_->HeadingFont());
        ApplyFont(mixTitle_, theme_->HeadingFont());
        for (const HWND small : {
                 subtitle_, routeStatus_, syncSummary_, virtualStatus_,
                 formatStatus_, frontStatus_, rearStatus_, syncStatus_,
                 status_, sideLevelHint_})
        {
            ApplyFont(small, theme_->SmallFont());
        }
    }

    void AppWindow::PaintWindow(const HDC dc) const
    {
        RECT client{};
        GetClientRect(window_, &client);
        if (client.right <= 0 || client.bottom <= 0)
        {
            return;
        }

        // Compose off-screen so a repaint never shows the background fill
        // before the cards land on top of it.
        const HDC memory = CreateCompatibleDC(dc);
        const HBITMAP surface = memory != nullptr
            ? CreateCompatibleBitmap(dc, client.right, client.bottom)
            : nullptr;
        const HGDIOBJ previousSurface = surface != nullptr
            ? SelectObject(memory, surface)
            : nullptr;
        const HDC target = surface != nullptr ? memory : dc;

        theme_->EraseBackground(target, client);
        const auto layout = ui::ComputeCommandDeckLayout(
            client.right, client.bottom, theme_->Dpi(),
            technicalDetailsExpanded_);
        const auto shifted = [this](RECT bounds) {
            OffsetRect(&bounds, 0, -scrollOffset_);
            return bounds;
        };
        theme_->PaintCard(target, shifted(layout.header),
                          theme_->Colors().cardRaised);
        theme_->PaintCard(target, shifted(layout.frontCard),
                          theme_->Colors().card, frontCardFault_);
        theme_->PaintCard(target, shifted(layout.chairCard),
                          theme_->Colors().card, chairCardFault_);
        theme_->PaintCard(target, shifted(layout.mixCard),
                          theme_->Colors().card);
        if (layout.detailsVisible)
        {
            theme_->PaintCard(
                target, shifted(layout.detailsCard), theme_->Colors().card);
        }
        theme_->PaintCard(
            target, shifted(layout.actionBar), theme_->Colors().cardRaised);

        if (surface != nullptr)
        {
            BitBlt(dc, 0, 0, client.right, client.bottom,
                   memory, 0, 0, SRCCOPY);
            SelectObject(memory, previousSurface);
            DeleteObject(surface);
        }
        if (memory != nullptr)
        {
            DeleteDC(memory);
        }
    }

    void AppWindow::SetTechnicalDetailsExpanded(const bool expanded)
    {
        if (technicalDetailsExpanded_ == expanded)
        {
            return;
        }

        technicalDetailsExpanded_ = expanded;
        SetWindowTextW(technicalDetailsButton_,
                       expanded ? L"Hide details" : L"Technical details");
        UpdateTechnicalDetailsVisibility();

        if (!IsZoomed(window_))
        {
            RECT client{};
            GetClientRect(window_, &client);
            const int designHeight = ui::ComputeCommandDeckLayout(
                client.right, 0, theme_->Dpi(), expanded).virtualHeight;
            const int delta = theme_->Scale(264);
            const int targetHeight = std::max(
                designHeight,
                static_cast<int>(client.bottom) +
                    (expanded ? delta : -delta));
            RECT target{0, 0, client.right, targetHeight};
            AdjustWindowRectExForDpi(
                &target,
                static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)),
                FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)),
                theme_->Dpi());
            RECT frame{};
            GetWindowRect(window_, &frame);
            int outerHeight = target.bottom - target.top;
            int windowTop = frame.top;
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(
                    MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST),
                    &monitorInfo))
            {
                outerHeight = std::min(
                    outerHeight,
                    static_cast<int>(monitorInfo.rcWork.bottom -
                                     monitorInfo.rcWork.top));
                windowTop = std::min(
                    windowTop,
                    static_cast<int>(monitorInfo.rcWork.bottom) -
                        outerHeight);
                windowTop = std::max(
                    windowTop, static_cast<int>(monitorInfo.rcWork.top));
            }
            SetWindowPos(
                window_, nullptr, frame.left, windowTop,
                frame.right - frame.left,
                outerHeight,
                SWP_NOACTIVATE | SWP_NOZORDER);
            // The clamped resize may keep the outer size unchanged, in
            // which case no WM_SIZE arrives; the shown/hidden details
            // controls still need fresh positions.
            RECT resized{};
            GetClientRect(window_, &resized);
            LayoutControls(resized.right, resized.bottom);
        }
        else
        {
            RECT client{};
            GetClientRect(window_, &client);
            LayoutControls(client.right, client.bottom);
        }
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                         RDW_UPDATENOW);
    }

    void AppWindow::AddTrayIcon()
    {
        if (trayIconAdded_)
        {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = TrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = TrayIconMessage;
        data.hIcon = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR));
        if (data.hIcon == nullptr)
        {
            data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        wcscpy_s(data.szTip, L"SoundStage Router");
        trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    }

    void AppWindow::RemoveTrayIcon()
    {
        if (!trayIconAdded_)
        {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = TrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        trayIconAdded_ = false;
    }

    void AppWindow::ShowFromTray()
    {
        ShowWindow(window_, SW_SHOW);
        if (IsIconic(window_))
        {
            ShowWindow(window_, SW_RESTORE);
        }
        SetForegroundWindow(window_);
    }

    void AppWindow::HideToTray()
    {
        ShowWindow(window_, SW_HIDE);
    }

    void AppWindow::ShowTrayBalloon(const std::wstring& title,
                                    const std::wstring& text,
                                    const bool warning)
    {
        if (!trayIconAdded_)
        {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = TrayIconId;
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = warning ? NIIF_WARNING : NIIF_INFO;
        wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void AppWindow::HandleTrayMessage(const LPARAM event)
    {
        switch (LOWORD(event))
        {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            ShowFromTray();
            return;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
        {
            const HMENU menu = CreatePopupMenu();
            if (menu == nullptr)
            {
                return;
            }
            const audio::PlaybackState state = coordinator_
                ? coordinator_->Status()->state
                : audio::PlaybackState::Stopped;
            const bool active =
                state != audio::PlaybackState::Stopped &&
                state != audio::PlaybackState::Faulted;
            AppendMenuW(menu, MF_STRING, TrayOpenCommandId,
                        L"Open SoundStage Router");
            AppendMenuW(menu, MF_STRING, TrayToggleRoutingCommandId,
                        active ? L"Stop Routing" : L"Start Routing");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, TrayExitCommandId, L"Exit");
            POINT cursor{};
            GetCursorPos(&cursor);
            // Required so the menu dismisses when focus moves elsewhere.
            SetForegroundWindow(window_);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0,
                           window_, nullptr);
            PostMessageW(window_, WM_NULL, 0, 0);
            DestroyMenu(menu);
            return;
        }
        default:
            return;
        }
    }

    void AppWindow::TryBackgroundStart()
    {
        if (!coordinator_)
        {
            return;
        }
        if (!ui::ShouldAttemptBackgroundStart(
                autoStartArmed_, coordinator_->Status()->state,
                GetTickCount64(), lastAutoStartAttemptTick_,
                AutoStartRetryIntervalMs))
        {
            return;
        }
        lastAutoStartAttemptTick_ = GetTickCount64();
        quietStartAttempt_ = true;
        StartTest();
        quietStartAttempt_ = false;
    }

    void AppWindow::ReportStartProblem(const wchar_t* text,
                                       const wchar_t* caption)
    {
        if (quietStartAttempt_)
        {
            SetStatus(std::wstring(L"Waiting to start: ") + text,
                      audio::UiSeverity::Warning);
            return;
        }
        MessageBoxW(window_, text, caption, MB_OK | MB_ICONWARNING);
    }

    void AppWindow::UpdateTechnicalDetailsVisibility() const
    {
        const int command = technicalDetailsExpanded_ ? SW_SHOW : SW_HIDE;
        for (const HWND control : {
                 deviceList_, frontStatus_, rearStatus_, syncStatus_})
        {
            ShowWindow(control, command);
        }
        const bool system =
            modeCombo_ == nullptr || ComboBox_GetCurSel(modeCombo_) != 1;
        ShowWindow(virtualStatus_,
                   technicalDetailsExpanded_ && system ? SW_SHOW : SW_HIDE);
    }

    bool AppWindow::RefreshDevices()
    {
        try
        {
            const int currentFront = SelectedEndpointIndex(frontCombo_);
            const int currentRear = SelectedEndpointIndex(rearCombo_);
            if (currentFront >= 0) settings_.frontEndpointId = endpoints_[currentFront].id;
            if (currentRear >= 0) settings_.rearEndpointId = endpoints_[currentRear].id;

            endpoints_ = AudioEndpointService::EnumerateRenderEndpoints();
            PopulateControls();

            std::wstring status = std::to_wstring(endpoints_.size()) + L" active output device";
            if (endpoints_.size() != 1) status += L"s";
            status += L" found.";
            if (endpoints_.size() < 2)
            {
                status += L" Connect the Bluetooth headrest and refresh.";
            }
            if (settings_.loadAdjustedValues)
            {
                status +=
                    L" Settings contained invalid values; supported defaults were applied.";
            }
            const auto virtualCount = std::count_if(
                endpoints_.begin(), endpoints_.end(),
                [](const AudioEndpoint& endpoint) {
                    return endpoint.isVirtualEndpoint;
                });
            const auto validVirtualCount = std::count_if(
                endpoints_.begin(), endpoints_.end(),
                [](const AudioEndpoint& endpoint) {
                    return endpoint.virtualContractValid;
                });
            const auto validVirtual = std::find_if(
                endpoints_.begin(), endpoints_.end(),
                [](const AudioEndpoint& endpoint) {
                    return endpoint.virtualContractValid;
                });
            hasSingleVirtualEndpoint_ = virtualCount == 1;
            detectedVirtualFormat_ =
                validVirtualCount == 1 && validVirtual != endpoints_.end()
                    ? validVirtual->virtualSurroundFormat
                    : audio::VirtualSurroundFormat::Unsupported;
            if (virtualCount == 0)
            {
                SetWindowTextW(
                    virtualStatus_,
                    L"Driver status: missing. Install the virtual driver, "
                    L"then set Windows default output to "
                    L"SoundStage Router Surround.");
            }
            else if (virtualCount > 1)
            {
                SetWindowTextW(
                    virtualStatus_,
                    L"Driver status: duplicate virtual endpoints found. "
                    L"Remove the duplicate before routing.");
            }
            else if (validVirtualCount != 1)
            {
                SetWindowTextW(
                    virtualStatus_,
                    L"Driver status: wrong virtual format; expected 5.1 or "
                    L"7.1 at 48 kHz float32.");
            }
            else
            {
                const wchar_t* ready =
                    detectedVirtualFormat_ ==
                        audio::VirtualSurroundFormat::FivePointOne
                    ? L"Driver status: ready in 5.1. Side Level is visible "
                      L"but disabled. Used when Windows is set to 7.1."
                    : L"Driver status: ready in 7.1. Windows default output "
                      L"must be SoundStage Router Surround.";
                SetWindowTextW(virtualStatus_, ready);
            }
            SetStatus(
                status,
                endpoints_.size() < 2
                    ? audio::UiSeverity::Warning
                    : audio::UiSeverity::Neutral);
            if (coordinator_)
            {
                RenderEngineStatus(*coordinator_->Status());
            }
            return true;
        }
        catch (const std::exception& error)
        {
            hasSingleVirtualEndpoint_ = false;
            detectedVirtualFormat_ =
                audio::VirtualSurroundFormat::Unsupported;
            const std::string message(error.what());
            SetStatus(L"Unable to enumerate audio devices.",
                      audio::UiSeverity::Fault);
            if (coordinator_)
            {
                RenderEngineStatus(*coordinator_->Status());
            }
            MessageBoxA(window_, message.c_str(), "Audio device error", MB_OK | MB_ICONERROR);
            return false;
        }
    }

    void AppWindow::PopulateControls()
    {
        ListView_DeleteAllItems(deviceList_);
        ComboBox_ResetContent(frontCombo_);
        ComboBox_ResetContent(rearCombo_);

        for (std::size_t index = 0; index < endpoints_.size(); ++index)
        {
            const auto& endpoint = endpoints_[index];

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(index);
            item.pszText = const_cast<LPWSTR>(endpoint.name.c_str());
            ListView_InsertItem(deviceList_, &item);
            SetListCell(deviceList_, static_cast<int>(index), 1, endpoint.formatDescription);
            SetListCell(deviceList_, static_cast<int>(index), 2,
                        endpoint.isDefault ? L"Default" : L"Available");

            if (!endpoint.isVirtualEndpoint)
            {
                const int frontItem =
                    ComboBox_AddString(frontCombo_, endpoint.name.c_str());
                const int rearItem =
                    ComboBox_AddString(rearCombo_, endpoint.name.c_str());
                ComboBox_SetItemData(
                    frontCombo_, frontItem, static_cast<LPARAM>(index));
                ComboBox_SetItemData(
                    rearCombo_, rearItem, static_cast<LPARAM>(index));
            }
        }

        const auto selectEndpoint = [](const HWND combo, const int endpoint) {
            const int count = ComboBox_GetCount(combo);
            for (int item = 0; item < count; ++item)
            {
                if (ComboBox_GetItemData(combo, item) == endpoint)
                {
                    return item;
                }
            }
            return -1;
        };
        int frontEndpoint = FindEndpoint(settings_.frontEndpointId);
        int frontSelection = selectEndpoint(frontCombo_, frontEndpoint);
        if (frontSelection < 0 && !settings_.frontEndpointId.empty())
        {
            frontSelection = ComboBox_AddString(
                frontCombo_, L"Saved Front output unavailable");
            ComboBox_SetItemData(frontCombo_, frontSelection, -1);
        }
        else if (frontSelection < 0)
        {
            const auto defaultDevice = std::find_if(endpoints_.begin(), endpoints_.end(),
                [](const AudioEndpoint& endpoint) {
                    return endpoint.isDefault && !endpoint.isVirtualEndpoint;
                });
            frontSelection = defaultDevice == endpoints_.end()
                ? (ComboBox_GetCount(frontCombo_) == 0 ? -1 : 0)
                : selectEndpoint(frontCombo_, static_cast<int>(
                    std::distance(endpoints_.begin(), defaultDevice)));
        }

        int rearEndpoint = FindEndpoint(settings_.rearEndpointId);
        int rearSelection = selectEndpoint(rearCombo_, rearEndpoint);
        if (rearSelection < 0 && !settings_.rearEndpointId.empty())
        {
            rearSelection = ComboBox_AddString(
                rearCombo_, L"Saved Chair output unavailable");
            ComboBox_SetItemData(rearCombo_, rearSelection, -1);
        }
        else if (rearSelection < 0)
        {
            for (int item = 0; item < ComboBox_GetCount(rearCombo_); ++item)
            {
                if (ComboBox_GetItemData(rearCombo_, item) !=
                    ComboBox_GetItemData(frontCombo_, frontSelection))
                {
                    rearSelection = item;
                    break;
                }
            }
        }

        ComboBox_SetCurSel(frontCombo_, frontSelection);
        ComboBox_SetCurSel(rearCombo_, rearSelection);
        SetWindowTextW(frontDelay_, std::to_wstring(settings_.frontDelayMs).c_str());
        SetWindowTextW(rearDelay_, std::to_wstring(settings_.rearDelayMs).c_str());
        SetWindowTextW(
            frontLevel_,
            std::to_wstring(settings_.frontLevelPercent).c_str());
        SetWindowTextW(
            rearLevel_,
            std::to_wstring(settings_.rearLevelPercent).c_str());
        SendMessageW(backLevel_, TBM_SETPOS, TRUE,
                     settings_.backLevelPercent);
        SendMessageW(sideLevel_, TBM_SETPOS, TRUE,
                     settings_.sideLevelPercent);
        UpdateSurroundLevelLabels();
    }

    void AppWindow::UpdateSurroundLevelLabels() const
    {
        const std::wstring back =
            std::to_wstring(ReadSurroundLevel(backLevel_)) + L"%";
        const std::wstring side =
            std::to_wstring(ReadSurroundLevel(sideLevel_)) + L"%";
        SetWindowTextW(backLevelValue_, back.c_str());
        SetWindowTextW(sideLevelValue_, side.c_str());
    }

    void AppWindow::SaveSurroundLevels()
    {
        settings_.backLevelPercent = ReadSurroundLevel(backLevel_);
        settings_.sideLevelPercent = ReadSurroundLevel(sideLevel_);
        UpdateSurroundLevelLabels();

        try
        {
            settingsStore_.Save(settings_);
        }
        catch (const std::exception&)
        {
            SetStatus(L"Unable to save surround mix levels.");
        }

        if (coordinator_)
        {
            coordinator_->PostSurroundMixLevels({
                static_cast<float>(settings_.backLevelPercent) / 100.0f,
                static_cast<float>(settings_.sideLevelPercent) / 100.0f});
        }
    }

    void AppWindow::SaveSettings()
    {
        const int front = SelectedEndpointIndex(frontCombo_);
        const int rear = SelectedEndpointIndex(rearCombo_);
        if (front < 0 || rear < 0)
        {
            MessageBoxW(window_, L"Select both a Front and Chair output device.",
                        L"Incomplete layout", MB_OK | MB_ICONWARNING);
            return;
        }
        if (front == rear)
        {
            MessageBoxW(window_, L"Front and Chair must use different Windows output devices.",
                        L"Duplicate output", MB_OK | MB_ICONWARNING);
            return;
        }

        settings_.frontEndpointId = endpoints_[front].id;
        settings_.rearEndpointId = endpoints_[rear].id;
        settings_.frontDelayMs = ReadDelay(frontDelay_);
        settings_.rearDelayMs = ReadDelay(rearDelay_);
        settings_.frontLevelPercent = ReadLevel(frontLevel_);
        settings_.rearLevelPercent = ReadLevel(rearLevel_);
        settings_.backLevelPercent = ReadSurroundLevel(backLevel_);
        settings_.sideLevelPercent = ReadSurroundLevel(sideLevel_);
        const int pattern = ComboBox_GetCurSel(patternCombo_);
        settings_.lastPattern = pattern >= 0 && pattern <= 3
            ? static_cast<audio::TestPattern>(pattern)
            : audio::TestPattern::PairedClicks;
        settings_.mode = ComboBox_GetCurSel(modeCombo_) == 1
            ? audio::PlaybackMode::TestSignals
            : audio::PlaybackMode::SystemAudio;
        const int rearFill = ComboBox_GetCurSel(rearFillCombo_);
        settings_.rearFill = rearFill >= 0 && rearFill <= 2
            ? static_cast<audio::RearFillMode>(rearFill)
            : audio::RearFillMode::Off;

        try
        {
            settingsStore_.Save(settings_);
            SetStatus(L"Layout saved. The routing engine can now use these endpoint IDs and delays.");
        }
        catch (const std::exception& error)
        {
            MessageBoxA(window_, error.what(), "Settings error", MB_OK | MB_ICONERROR);
        }
    }

    void AppWindow::StartTest()
    {
        if (!RefreshDevices())
        {
            return;
        }

        std::optional<audio::RunConfiguration> configuration =
            BuildRunConfiguration();
        if (!configuration)
        {
            return;
        }

        settings_.frontEndpointId =
            configuration->routes[0].endpointId;
        settings_.rearEndpointId =
            configuration->routes[1].endpointId;
        settings_.frontDelayMs = static_cast<int>(
            configuration->routes[0].delayMs);
        settings_.rearDelayMs = static_cast<int>(
            configuration->routes[1].delayMs);
        settings_.frontLevelPercent = static_cast<int>(
            configuration->routes[0].gain * 100.0f);
        settings_.rearLevelPercent = static_cast<int>(
            configuration->routes[1].gain * 100.0f);
        settings_.backLevelPercent = static_cast<int>(
            configuration->surroundMix.back * 100.0f);
        settings_.sideLevelPercent = static_cast<int>(
            configuration->surroundMix.side * 100.0f);
        settings_.lastPattern = configuration->pattern;
        settings_.mode = configuration->mode;
        settings_.rearFill = configuration->rearFill;
        routedVirtualFormat_ =
            configuration->mode == audio::PlaybackMode::SystemAudio
                ? detectedVirtualFormat_
                : audio::VirtualSurroundFormat::Unsupported;
        try
        {
            settingsStore_.Save(settings_);
        }
        catch (const std::exception& error)
        {
            if (quietStartAttempt_)
            {
                SetStatus(L"Waiting to start: settings could not be saved.",
                          audio::UiSeverity::Warning);
                return;
            }
            MessageBoxA(window_, error.what(), "Settings error",
                        MB_OK | MB_ICONERROR);
            return;
        }
        SetPlaybackControlsEnabled(false);
        modelRecoveryVisible_ = false;
        SetStatus(configuration->mode == audio::PlaybackMode::SystemAudio
            ? L"Preparing physical outputs, then virtual loopback capture..."
            : L"Preparing synchronized test playback...");
        try
        {
            coordinator_->PostStart(std::move(*configuration));
        }
        catch (const std::exception& error)
        {
            SetPlaybackControlsEnabled(true);
            if (quietStartAttempt_)
            {
                SetStatus(L"Waiting to start: playback could not begin.",
                          audio::UiSeverity::Warning);
                return;
            }
            MessageBoxA(window_, error.what(), "Playback error",
                        MB_OK | MB_ICONERROR);
        }
    }

    std::optional<audio::RunConfiguration>
    AppWindow::BuildRunConfiguration()
    {
        const int frontIndex = SelectedEndpointIndex(frontCombo_);
        const int rearIndex = SelectedEndpointIndex(rearCombo_);
        if (frontIndex < 0 || rearIndex < 0)
        {
            ReportStartProblem(
                L"Both saved outputs must be active before starting.",
                L"Output unavailable");
            return std::nullopt;
        }
        if (frontIndex == rearIndex)
        {
            ReportStartProblem(
                L"Front and Chair must use different Windows output devices.",
                L"Duplicate output");
            return std::nullopt;
        }

        audio::RunConfiguration configuration;
        configuration.mode = ComboBox_GetCurSel(modeCombo_) == 1
            ? audio::PlaybackMode::TestSignals
            : audio::PlaybackMode::SystemAudio;
        const int rearFill = ComboBox_GetCurSel(rearFillCombo_);
        configuration.rearFill = rearFill >= 0 && rearFill <= 2
            ? static_cast<audio::RearFillMode>(rearFill)
            : audio::RearFillMode::Off;
        if (endpoints_[frontIndex].isVirtualEndpoint ||
            endpoints_[rearIndex].isVirtualEndpoint)
        {
            ReportStartProblem(
                L"The virtual endpoint cannot be a physical output.",
                L"Feedback prevented");
            return std::nullopt;
        }
        if (configuration.mode == audio::PlaybackMode::SystemAudio)
        {
            const auto virtualEndpoint = std::find_if(
                endpoints_.begin(), endpoints_.end(),
                [](const AudioEndpoint& endpoint) {
                    return endpoint.virtualContractValid;
                });
            const auto count = std::count_if(
                endpoints_.begin(), endpoints_.end(),
                [](const AudioEndpoint& endpoint) {
                    return endpoint.isVirtualEndpoint;
                });
            if (count != 1 || virtualEndpoint == endpoints_.end())
            {
                ReportStartProblem(
                    L"SoundStage Router Surround is missing, duplicated, or "
                    L"is not set to 5.1 or 7.1 at 48 kHz float32.",
                    L"Virtual driver unavailable");
                return std::nullopt;
            }
            configuration.virtualEndpointId = virtualEndpoint->id;
        }
        const int pattern = ComboBox_GetCurSel(patternCombo_);
        configuration.pattern = pattern >= 0 && pattern <= 3
            ? static_cast<audio::TestPattern>(pattern)
            : audio::TestPattern::PairedClicks;
        configuration.clockReferenceRole =
            audio::SpeakerRole::Rear;
        configuration.surroundMix = {
            static_cast<float>(ReadSurroundLevel(backLevel_)) / 100.0f,
            static_cast<float>(ReadSurroundLevel(sideLevel_)) / 100.0f};
        configuration.routes = {
            {audio::SpeakerRole::Front,
             endpoints_[frontIndex].id,
             audio::ClampDelayMs(ReadDelay(frontDelay_)),
             false,
             static_cast<float>(ReadLevel(frontLevel_)) / 100.0f},
            {audio::SpeakerRole::Rear,
             endpoints_[rearIndex].id,
             audio::ClampDelayMs(ReadDelay(rearDelay_)),
             true,
             static_cast<float>(ReadLevel(rearLevel_)) / 100.0f}
        };
        return configuration;
    }

    void AppWindow::RenderEngineStatus(
        const audio::EngineStatus& engineStatus)
    {
        const audio::PlaybackMode mode =
            ComboBox_GetCurSel(modeCombo_) == 1
                ? audio::PlaybackMode::TestSignals
                : audio::PlaybackMode::SystemAudio;

        audio::EngineStatus presentedStatus = engineStatus;
        if (presentedStatus.surroundFormat ==
                audio::VirtualSurroundFormat::Unsupported &&
            detectedVirtualFormat_ !=
                audio::VirtualSurroundFormat::Unsupported)
        {
            presentedStatus.surroundFormat = detectedVirtualFormat_;
            presentedStatus.virtualEndpointReady = true;
        }

        audio::SurroundUiState ui =
            audio::BuildSurroundUiState(presentedStatus, mode);
        if (engineStatus.surroundFormat ==
                audio::VirtualSurroundFormat::FivePointOne ||
            engineStatus.surroundFormat ==
                audio::VirtualSurroundFormat::SevenPointOne)
        {
            routedVirtualFormat_ = engineStatus.surroundFormat;
        }
        const bool virtualCaptureFault =
            engineStatus.state == audio::PlaybackState::Faulted &&
            engineStatus.lastFault.message.find(L"Virtual endpoint") !=
                std::wstring::npos;
        const bool systemMode = mode == audio::PlaybackMode::SystemAudio;
        const bool formatChanged = systemMode &&
            ui::IsCommandDeckFormatChange(
                virtualCaptureFault, routedVirtualFormat_,
                detectedVirtualFormat_);
        const bool unsupportedFormatFault = systemMode &&
            ui::IsCommandDeckUnsupportedFormatFault(
                virtualCaptureFault, hasSingleVirtualEndpoint_,
                routedVirtualFormat_,
                detectedVirtualFormat_);
        const bool physicalOutputFault =
            engineStatus.state == audio::PlaybackState::Faulted &&
            engineStatus.lastFault.code != 0 &&
            engineStatus.lastFault.message.empty();
        const bool frontFault = physicalOutputFault &&
            engineStatus.lastFault.role == audio::SpeakerRole::Front;
        const bool chairFault = physicalOutputFault &&
            engineStatus.lastFault.role == audio::SpeakerRole::Rear;
        if (frontFault != frontCardFault_ || chairFault != chairCardFault_)
        {
            frontCardFault_ = frontFault;
            chairCardFault_ = chairFault;
            InvalidateRect(window_, nullptr, FALSE);
        }

        const bool physicalRouteReady =
            ui::HasValidPhysicalRouteSelection(
                SelectedEndpointIndex(frontCombo_),
                SelectedEndpointIndex(rearCombo_));
        if (!physicalRouteReady)
        {
            ui.startEnabled = false;
            if (presentedStatus.state == audio::PlaybackState::Stopped)
            {
                ui.routeStateText = L"Setup required";
                ui.routeSeverity = audio::UiSeverity::Warning;
            }
            if (ui.recoveryText.empty())
            {
                ui.recoveryText =
                    L"Choose two different active outputs for Front and Chair.";
            }
        }

        const audio::UiSeverity syncSeverity =
            engineStatus.clockHealth == audio::ClockHealth::Active
                ? audio::UiSeverity::Healthy
                : engineStatus.clockHealth ==
                    audio::ClockHealth::Unavailable
                    ? audio::UiSeverity::Warning
                    : audio::UiSeverity::Neutral;
        // The statics take their color from these members at paint time;
        // repaint one only when its severity actually changes.
        if (formatSeverity_ != ui.formatSeverity)
        {
            formatSeverity_ = ui.formatSeverity;
            InvalidateRect(formatStatus_, nullptr, FALSE);
        }
        if (routeSeverity_ != ui.routeSeverity)
        {
            routeSeverity_ = ui.routeSeverity;
            InvalidateRect(routeStatus_, nullptr, FALSE);
        }
        if (syncSeverity_ != syncSeverity)
        {
            syncSeverity_ = syncSeverity;
            InvalidateRect(syncSummary_, nullptr, FALSE);
        }
        SetTextIfChanged(formatStatus_, ui.formatText);
        SetTextIfChanged(routeStatus_, ui.routeStateText);
        SetTextIfChanged(syncSummary_, ui.syncText);
        SetTextIfChanged(sideLevelHint_, ui.sideLevelHint);

        if (formatChanged)
        {
            const bool fivePointOne =
                detectedVirtualFormat_ ==
                audio::VirtualSurroundFormat::FivePointOne;
            SetTextIfChanged(startButton_, fivePointOne
                ? L"Restart in 5.1" : L"Restart in 7.1");
            SetStatus(
                fivePointOne
                    ? L"Routing stopped after the format changed. Restart manually in 5.1."
                    : L"Routing stopped after the format changed. Restart manually in 7.1.",
                audio::UiSeverity::Warning);
            modelRecoveryVisible_ = true;
        }
        else
        {
            SetTextIfChanged(
                startButton_,
                mode == audio::PlaybackMode::SystemAudio
                    ? L"Start Routing" : L"Start Test");
            if (unsupportedFormatFault)
            {
                SetStatus(
                    L"Routing stopped because Windows is not using 5.1 or 7.1 at 48 kHz. Choose a supported format, then start routing again.",
                    audio::UiSeverity::Warning);
                modelRecoveryVisible_ = true;
            }
            else if (physicalOutputFault)
            {
                SetStatus(
                    frontCardFault_
                        ? L"Front output stopped. Reconnect it, then refresh devices."
                        : L"Chair output stopped. Reconnect it, then refresh devices.",
                    audio::UiSeverity::Fault);
                modelRecoveryVisible_ = true;
            }
            else if (!ui.recoveryText.empty())
            {
                SetStatus(ui.recoveryText, ui.routeSeverity);
                modelRecoveryVisible_ = true;
            }
            else if (engineStatus.state == audio::PlaybackState::Running)
            {
                SetStatus(L"Routing to Front and Chair. " + ui.syncText,
                          syncSeverity_);
                modelRecoveryVisible_ = false;
            }
            else if (engineStatus.state == audio::PlaybackState::Stopping)
            {
                SetStatus(L"Stopping routing...");
                modelRecoveryVisible_ = true;
            }
            else if (modelRecoveryVisible_)
            {
                std::wstring summary = std::to_wstring(endpoints_.size()) +
                    L" active output device";
                if (endpoints_.size() != 1)
                {
                    summary += L"s";
                }
                summary += L" found.";
                SetStatus(summary);
                modelRecoveryVisible_ = false;
            }
        }

        const bool selectable = ui.deviceSelectionEnabled;
        const bool liveTuning = ui.liveTuningEnabled || selectable;
        for (const HWND control : {
                 frontCombo_, rearCombo_, patternCombo_, modeCombo_,
                 rearFillCombo_, refreshButton_, saveButton_})
        {
            EnableWindow(control, selectable ? TRUE : FALSE);
        }
        EnableWindow(frontDelay_, liveTuning ? TRUE : FALSE);
        EnableWindow(rearDelay_, liveTuning ? TRUE : FALSE);
        EnableWindow(frontLevel_, selectable ? TRUE : FALSE);
        EnableWindow(rearLevel_, selectable ? TRUE : FALSE);
        EnableWindow(backLevel_, liveTuning ? TRUE : FALSE);
        EnableWindow(backLevelLabel_, liveTuning ? TRUE : FALSE);
        EnableWindow(backLevelValue_, liveTuning ? TRUE : FALSE);
        const bool sideEnabled = liveTuning && ui.sideLevelEnabled;
        EnableWindow(sideLevelLabel_, TRUE);
        EnableWindow(sideLevel_, sideEnabled ? TRUE : FALSE);
        EnableWindow(sideLevelValue_, TRUE);
        if (sideEnabled != sideLevelWasEnabled_)
        {
            // The statics derive their dim color from the slider's enabled
            // state at paint time; EnableWindow repaints only the slider.
            sideLevelWasEnabled_ = sideEnabled;
            InvalidateRect(sideLevelLabel_, nullptr, TRUE);
            InvalidateRect(sideLevelValue_, nullptr, TRUE);
        }
        EnableWindow(startButton_, ui.startEnabled ? TRUE : FALSE);
        EnableWindow(stopButton_, ui.stopEnabled ? TRUE : FALSE);

        const bool showStop = ui.stopEnabled;
        const HWND focused = GetFocus();
        ShowWindow(startButton_, showStop ? SW_HIDE : SW_SHOW);
        ShowWindow(stopButton_, showStop ? SW_SHOW : SW_HIDE);
        if (focused == startButton_ && showStop)
        {
            SetFocus(stopButton_);
        }
        else if (focused == stopButton_ && !showStop)
        {
            SetFocus(startButton_);
        }

        const auto endpointText = [](const wchar_t* name,
                                     const audio::EndpointTelemetry& endpoint,
                                     const bool reference)
        {
            std::wostringstream text;
            text << name << L": "
                 << (endpoint.running ? L"running" :
                     endpoint.prepared ? L"prepared" : L"stopped")
                 << L" | " << endpoint.sampleRate << L" Hz / "
                 << endpoint.channels << L" ch"
                 << L" | buffer " << std::fixed << std::setprecision(1)
                 << endpoint.bufferDurationMs << L" ms"
                 << L" | delay " << endpoint.delayMs << L" ms"
                 << L" | underruns " << endpoint.underrunCount
                 << L" | " << (reference ? L"reference" : L"follower");
            return text.str();
        };
        SetTextIfChanged(
            frontStatus_,
            endpointText(
                L"Front", engineStatus.endpoints[0], false));
        SetTextIfChanged(
            rearStatus_,
            endpointText(
                L"Rear", engineStatus.endpoints[1], true));

        const wchar_t* health =
            engineStatus.clockHealth == audio::ClockHealth::Active
                ? L"active"
                : engineStatus.clockHealth ==
                    audio::ClockHealth::Unavailable
                    ? L"unavailable" : L"settling";
        std::wostringstream sync;
        sync << L"Clock correction: " << health
             << L" | relative " << std::fixed << std::setprecision(1)
             << engineStatus.relativePpm << L" ppm"
             << L" | correction " << engineStatus.correctionPpm
             << L" ppm";
        if (engineStatus.virtualEndpointReady)
        {
            sync << L" | capture overflow "
                << engineStatus.captureOverflowCount
                << L", source underrun "
                << engineStatus.captureUnderrunCount;
        }
        if (engineStatus.lastFault.code != 0)
        {
            sync << L" | "
                 << audio::FormatFault(engineStatus.lastFault);
        }
        SetTextIfChanged(syncStatus_, sync.str());
    }

    void AppWindow::UpdateModeControls()
    {
        const bool system =
            ComboBox_GetCurSel(modeCombo_) != 1;
        ShowWindow(patternLabel_, system ? SW_HIDE : SW_SHOW);
        ShowWindow(patternCombo_, system ? SW_HIDE : SW_SHOW);
        ShowWindow(rearFillLabel_, system ? SW_SHOW : SW_HIDE);
        ShowWindow(rearFillCombo_, system ? SW_SHOW : SW_HIDE);
        ShowWindow(virtualStatus_,
                   system && technicalDetailsExpanded_ ? SW_SHOW : SW_HIDE);
        if (coordinator_)
        {
            RenderEngineStatus(*coordinator_->Status());
        }
    }

    void AppWindow::SetPlaybackControlsEnabled(
        const bool selectable)
    {
        for (const HWND control : {
                 frontCombo_, rearCombo_, patternCombo_, modeCombo_,
                 rearFillCombo_, frontLevel_, rearLevel_, refreshButton_,
                 saveButton_})
        {
            EnableWindow(control, selectable ? TRUE : FALSE);
        }
        EnableWindow(frontDelay_, TRUE);
        EnableWindow(rearDelay_, TRUE);
        EnableWindow(backLevelLabel_, TRUE);
        EnableWindow(backLevel_, TRUE);
        EnableWindow(backLevelValue_, TRUE);
        const bool sideAvailable =
            ComboBox_GetCurSel(modeCombo_) == 1 ||
            detectedVirtualFormat_ ==
                audio::VirtualSurroundFormat::SevenPointOne;
        EnableWindow(sideLevelLabel_, TRUE);
        EnableWindow(sideLevel_, sideAvailable ? TRUE : FALSE);
        EnableWindow(sideLevelValue_, TRUE);
        EnableWindow(startButton_, selectable ? TRUE : FALSE);
        EnableWindow(stopButton_, selectable ? FALSE : TRUE);
        ShowWindow(startButton_, selectable ? SW_SHOW : SW_HIDE);
        ShowWindow(stopButton_, selectable ? SW_HIDE : SW_SHOW);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void AppWindow::SetStatus(
        const std::wstring& text,
        const audio::UiSeverity severity)
    {
        // Called every status tick; repaint only on an actual change.
        const bool severityChanged = statusSeverity_ != severity;
        statusSeverity_ = severity;
        const bool textChanged = SetTextIfChanged(status_, text);
        if (severityChanged && !textChanged)
        {
            InvalidateRect(status_, nullptr, FALSE);
        }
    }

    COLORREF AppWindow::SeverityColor(
        const audio::UiSeverity severity) const noexcept
    {
        switch (severity)
        {
        case audio::UiSeverity::Healthy:
            return theme_->Colors().healthy;
        case audio::UiSeverity::Warning:
            return theme_->Colors().warning;
        case audio::UiSeverity::Fault:
            return theme_->Colors().fault;
        case audio::UiSeverity::Neutral:
        default:
            return theme_->Colors().secondary;
        }
    }

    int AppWindow::FindEndpoint(const std::wstring& id) const
    {
        if (id.empty()) return -1;
        for (std::size_t index = 0; index < endpoints_.size(); ++index)
        {
            if (endpoints_[index].id == id) return static_cast<int>(index);
        }
        return -1;
    }

    int AppWindow::SelectedEndpointIndex(const HWND combo) const
    {
        if (combo == nullptr) return -1;
        const LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (selected == CB_ERR) return -1;
        const LRESULT endpoint = SendMessageW(
            combo, CB_GETITEMDATA, selected, 0);
        return endpoint == CB_ERR || endpoint < 0
            ? -1 : static_cast<int>(endpoint);
    }

    int AppWindow::ReadDelay(const HWND edit) const
    {
        BOOL valid = FALSE;
        const UINT value = GetDlgItemInt(window_, GetDlgCtrlID(edit), &valid, FALSE);
        if (valid == FALSE) return 0;
        return std::clamp(static_cast<int>(value), 0, 2000);
    }

    int AppWindow::ReadLevel(const HWND edit) const
    {
        BOOL valid = FALSE;
        const UINT value = GetDlgItemInt(
            window_, GetDlgCtrlID(edit), &valid, FALSE);
        return valid == FALSE
            ? 100
            : audio::ClampLevelPercent(static_cast<int>(value));
    }

    int AppWindow::ReadSurroundLevel(const HWND trackbar) const
    {
        if (trackbar == nullptr) return 100;
        return audio::ClampLevelPercent(static_cast<int>(
            SendMessageW(trackbar, TBM_GETPOS, 0, 0)));
    }

    void AppWindow::ApplyFont(const HWND control, const HFONT font) const
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}
