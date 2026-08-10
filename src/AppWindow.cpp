#include "AppWindow.h"
#include "audio/WasapiEndpointSession.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
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
    constexpr UINT_PTR StatusTimerId = 1;
    constexpr UINT StatusTimerPeriodMs = 250;

    HMENU ControlId(const int value)
    {
        return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value));
    }

    HWND CreateLabel(HWND parent, const wchar_t* text, DWORD style = SS_LEFT)
    {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                               parent, nullptr, GetModuleHandleW(nullptr), nullptr);
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
    AppWindow::AppWindow(const HINSTANCE instance)
        : instance_(instance),
          settings_(settingsStore_.Load()),
          coordinator_(std::make_unique<audio::AudioEngineCoordinator>())
    {
        backgroundBrush_ = CreateSolidBrush(RGB(246, 247, 250));
        titleFont_ = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        bodyFont_ = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        smallFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hbrBackground = backgroundBrush_;
        windowClass.lpszClassName = WindowClassName;
        windowClass.style = CS_HREDRAW | CS_VREDRAW;

        if (RegisterClassExW(&windowClass) == 0)
        {
            throw std::runtime_error("Unable to register the application window.");
        }

        window_ = CreateWindowExW(0, WindowClassName, L"SoundStage Router",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  980, 820, nullptr, nullptr, instance_, this);
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
        if (titleFont_ != nullptr) DeleteObject(titleFont_);
        if (bodyFont_ != nullptr) DeleteObject(bodyFont_);
        if (smallFont_ != nullptr) DeleteObject(smallFont_);
        if (backgroundBrush_ != nullptr) DeleteObject(backgroundBrush_);
        UnregisterClassW(WindowClassName, instance_);
    }

    int AppWindow::Run(const int showCommand)
    {
        ShowWindow(window_, showCommand);
        UpdateWindow(window_);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
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
            CreateControls();
            RefreshDevices();
            SetTimer(window_, StatusTimerId, StatusTimerPeriodMs, nullptr);
            return 0;
        case WM_SIZE:
            LayoutControls(LOWORD(lParam), HIWORD(lParam));
            return 0;
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
                RefreshDevices();
                return 0;
            case SaveButtonId:
                SaveSettings();
                return 0;
            case StartButtonId:
                StartTest();
                return 0;
            case StopButtonId:
                if (coordinator_) coordinator_->PostStop();
                return 0;
            default:
                break;
            }
            break;
        case WM_TIMER:
            if (wParam == StatusTimerId && coordinator_)
            {
                const auto status = coordinator_->Status();
                RenderEngineStatus(*status);
                const bool selectable =
                    status->state == audio::PlaybackState::Stopped ||
                    status->state == audio::PlaybackState::Faulted;
                SetPlaybackControlsEnabled(selectable);
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(246, 247, 250));
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        case WM_DESTROY:
            KillTimer(window_, StatusTimerId);
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
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void AppWindow::CreateControls()
    {
        title_ = CreateLabel(window_, L"SoundStage Router");
        subtitle_ = CreateLabel(window_,
            L"Assign physical Windows outputs to surround positions and compensate for device latency.");

        deviceList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, window_, ControlId(DeviceListId), instance_, nullptr);
        ListView_SetExtendedListViewStyle(deviceList_,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        InsertColumn(deviceList_, 0, 350, L"Output device");
        InsertColumn(deviceList_, 1, 280, L"Windows mix format");
        InsertColumn(deviceList_, 2, 110, L"Role");

        frontLabel_ = CreateLabel(
            window_, L"Front output (soundbar / monitor speaker)");
        frontCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
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
            window_, L"Rear output (Bluetooth headrest)");
        rearCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
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

        patternLabel_ = CreateLabel(window_, L"Test pattern");
        patternCombo_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(PatternComboId),
            instance_, nullptr);
        modeLabel_ = CreateLabel(window_, L"Source mode");
        modeCombo_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(ModeComboId),
            instance_, nullptr);
        ComboBox_AddString(modeCombo_, L"System audio (virtual 5.1)");
        ComboBox_AddString(modeCombo_, L"Test signals");
        ComboBox_SetCurSel(
            modeCombo_,
            settings_.mode == audio::PlaybackMode::SystemAudio ? 0 : 1);
        rearFillLabel_ = CreateLabel(window_, L"Rear fill");
        rearFillCombo_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
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
            L"SoundStage Router 5.1.");
        for (const wchar_t* pattern : {
                 L"Paired clicks", L"Alternating clicks",
                 L"Front tone", L"Rear tone"})
        {
            ComboBox_AddString(patternCombo_, pattern);
        }
        ComboBox_SetCurSel(
            patternCombo_, static_cast<int>(settings_.lastPattern));

        refreshButton_ = CreateWindowExW(0, L"BUTTON", L"Refresh devices",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_, ControlId(RefreshButtonId), instance_, nullptr);
        saveButton_ = CreateWindowExW(0, L"BUTTON", L"Save layout",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, window_, ControlId(SaveButtonId), instance_, nullptr);
        startButton_ = CreateWindowExW(
            0, L"BUTTON", L"Start Routing",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, window_, ControlId(StartButtonId),
            instance_, nullptr);
        stopButton_ = CreateWindowExW(
            0, L"BUTTON", L"Stop Routing",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_, ControlId(StopButtonId),
            instance_, nullptr);
        EnableWindow(stopButton_, FALSE);
        frontStatus_ = CreateLabel(window_, L"Front: stopped");
        rearStatus_ = CreateLabel(window_, L"Rear: stopped");
        syncStatus_ = CreateLabel(
            window_, L"Clock correction: settling");
        status_ = CreateLabel(window_, L"", SS_LEFT | SS_ENDELLIPSIS);

        for (const HWND child : {
                 title_, subtitle_, deviceList_,
                 frontLabel_, frontCombo_, frontDelayLabel_, frontDelay_,
                 frontLevelLabel_, frontLevel_,
                 rearLabel_, rearCombo_, rearDelayLabel_, rearDelay_,
                 rearLevelLabel_, rearLevel_,
                 modeLabel_, modeCombo_, rearFillLabel_, rearFillCombo_,
                 virtualStatus_,
                 patternLabel_, patternCombo_, refreshButton_, saveButton_,
                 startButton_, stopButton_, frontStatus_, rearStatus_,
                 syncStatus_, status_})
        {
            ApplyFont(child, bodyFont_);
        }
        ApplyFont(title_, titleFont_);
        ApplyFont(subtitle_, smallFont_);
        ApplyFont(status_, smallFont_);
        ApplyFont(frontStatus_, smallFont_);
        ApplyFont(rearStatus_, smallFont_);
        ApplyFont(syncStatus_, smallFont_);
        ApplyFont(virtualStatus_, smallFont_);
        UpdateModeControls();
    }

    void AppWindow::LayoutControls(const int width, const int height) const
    {
        const int margin = 28;
        const int contentWidth = std::max(300, width - margin * 2);
        MoveWindow(title_, margin, 22, contentWidth, 36, TRUE);
        MoveWindow(subtitle_, margin, 59, contentWidth, 24, TRUE);
        MoveWindow(deviceList_, margin, 94, contentWidth,
                   std::max(120, height - 590), TRUE);

        const int formTop = std::max(230, height - 480);
        const int delayWidth = 100;
        const int levelWidth = 100;
        const int gap = 16;
        const int comboWidth =
            contentWidth - delayWidth - levelWidth - gap * 2;

        MoveWindow(modeLabel_, margin, formTop, 240, 23, TRUE);
        MoveWindow(modeCombo_, margin, formTop + 24, 300, 180, TRUE);
        MoveWindow(rearFillLabel_, margin + 320, formTop, 240, 23, TRUE);
        MoveWindow(rearFillCombo_, margin + 320, formTop + 24, 260, 180, TRUE);
        MoveWindow(virtualStatus_, margin, formTop + 57, contentWidth, 23, TRUE);

        const int routesTop = formTop + 88;
        MoveWindow(frontLabel_, margin, routesTop, comboWidth, 23, TRUE);
        MoveWindow(frontDelayLabel_, margin + comboWidth + gap, routesTop,
                   delayWidth, 23, TRUE);
        MoveWindow(frontLevelLabel_,
                   margin + comboWidth + delayWidth + gap * 2,
                   routesTop, levelWidth, 23, TRUE);
        MoveWindow(rearLabel_, margin, routesTop + 62, comboWidth, 23, TRUE);
        MoveWindow(rearDelayLabel_, margin + comboWidth + gap, routesTop + 62,
                   delayWidth, 23, TRUE);
        MoveWindow(rearLevelLabel_,
                   margin + comboWidth + delayWidth + gap * 2,
                   routesTop + 62, levelWidth, 23, TRUE);

        MoveWindow(frontCombo_, margin, routesTop + 25, comboWidth, 220, TRUE);
        MoveWindow(frontDelay_, margin + comboWidth + gap, routesTop + 25, delayWidth, 30, TRUE);
        MoveWindow(frontLevel_,
                   margin + comboWidth + delayWidth + gap * 2,
                   routesTop + 25, levelWidth, 30, TRUE);
        MoveWindow(rearCombo_, margin, routesTop + 87, comboWidth, 220, TRUE);
        MoveWindow(rearDelay_, margin + comboWidth + gap, routesTop + 87,
                   delayWidth, 30, TRUE);
        MoveWindow(rearLevel_,
                   margin + comboWidth + delayWidth + gap * 2,
                   routesTop + 87, levelWidth, 30, TRUE);
        MoveWindow(patternLabel_, margin, routesTop + 126, 240, 23, TRUE);
        MoveWindow(patternCombo_, margin, routesTop + 151, 260, 180, TRUE);
        MoveWindow(refreshButton_, margin, routesTop + 198, 150, 36, TRUE);
        MoveWindow(saveButton_, margin + 162, routesTop + 198, 140, 36, TRUE);
        MoveWindow(startButton_, margin + 314, routesTop + 198, 140, 36, TRUE);
        MoveWindow(stopButton_, margin + 466, routesTop + 198, 130, 36, TRUE);
        MoveWindow(frontStatus_, margin, routesTop + 246, contentWidth, 22, TRUE);
        MoveWindow(rearStatus_, margin, routesTop + 270, contentWidth, 22, TRUE);
        MoveWindow(syncStatus_, margin, routesTop + 294, contentWidth, 22, TRUE);
        MoveWindow(status_, margin, routesTop + 322, contentWidth, 25, TRUE);
    }

    void AppWindow::RefreshDevices()
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
            if (virtualCount == 0)
            {
                SetWindowTextW(
                    virtualStatus_,
                    L"Driver status: missing. Install the virtual driver, "
                    L"then set Windows default output to SoundStage Router 5.1.");
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
                    L"Driver status: wrong virtual format; expected "
                    L"48 kHz float32 6-channel FL/FR/FC/LFE/BL/BR.");
            }
            else
            {
                SetWindowTextW(
                    virtualStatus_,
                    L"Driver status: ready. Windows default output must be "
                    L"SoundStage Router 5.1; keep this app running.");
            }
            SetStatus(status);
        }
        catch (const std::exception& error)
        {
            const std::string message(error.what());
            SetStatus(L"Unable to enumerate audio devices.");
            MessageBoxA(window_, message.c_str(), "Audio device error", MB_OK | MB_ICONERROR);
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
                rearCombo_, L"Saved Rear output unavailable");
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
    }

    void AppWindow::SaveSettings()
    {
        const int front = SelectedEndpointIndex(frontCombo_);
        const int rear = SelectedEndpointIndex(rearCombo_);
        if (front < 0 || rear < 0)
        {
            MessageBoxW(window_, L"Select both a front and rear output device.",
                        L"Incomplete layout", MB_OK | MB_ICONWARNING);
            return;
        }
        if (front == rear)
        {
            MessageBoxW(window_, L"Front and rear must use different Windows output devices.",
                        L"Duplicate output", MB_OK | MB_ICONWARNING);
            return;
        }

        settings_.frontEndpointId = endpoints_[front].id;
        settings_.rearEndpointId = endpoints_[rear].id;
        settings_.frontDelayMs = ReadDelay(frontDelay_);
        settings_.rearDelayMs = ReadDelay(rearDelay_);
        settings_.frontLevelPercent = ReadLevel(frontLevel_);
        settings_.rearLevelPercent = ReadLevel(rearLevel_);
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
        settings_.lastPattern = configuration->pattern;
        settings_.mode = configuration->mode;
        settings_.rearFill = configuration->rearFill;
        try
        {
            settingsStore_.Save(settings_);
        }
        catch (const std::exception& error)
        {
            MessageBoxA(window_, error.what(), "Settings error",
                        MB_OK | MB_ICONERROR);
            return;
        }
        SetPlaybackControlsEnabled(false);
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
            MessageBoxA(window_, error.what(), "Playback error",
                        MB_OK | MB_ICONERROR);
        }
    }

    std::optional<audio::RunConfiguration>
    AppWindow::BuildRunConfiguration() const
    {
        const int frontIndex = SelectedEndpointIndex(frontCombo_);
        const int rearIndex = SelectedEndpointIndex(rearCombo_);
        if (frontIndex < 0 || rearIndex < 0)
        {
            MessageBoxW(
                window_,
                L"Both saved outputs must be active before starting.",
                L"Output unavailable", MB_OK | MB_ICONWARNING);
            return std::nullopt;
        }
        if (frontIndex == rearIndex)
        {
            MessageBoxW(
                window_,
                L"Front and rear must use different Windows output devices.",
                L"Duplicate output", MB_OK | MB_ICONWARNING);
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
            MessageBoxW(
                window_, L"The virtual endpoint cannot be a physical output.",
                L"Feedback prevented", MB_OK | MB_ICONWARNING);
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
                MessageBoxW(
                    window_,
                    L"SoundStage Router 5.1 is missing, duplicated, or has "
                    L"the wrong 6-channel 48 kHz float format.",
                    L"Virtual driver unavailable",
                    MB_OK | MB_ICONWARNING);
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
        const audio::EngineStatus& engineStatus) const
    {
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
        SetWindowTextW(
            frontStatus_,
            endpointText(
                L"Front", engineStatus.endpoints[0], false).c_str());
        SetWindowTextW(
            rearStatus_,
            endpointText(
                L"Rear", engineStatus.endpoints[1], true).c_str());

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
        SetWindowTextW(syncStatus_, sync.str().c_str());
    }

    void AppWindow::UpdateModeControls() const
    {
        const bool system =
            ComboBox_GetCurSel(modeCombo_) != 1;
        ShowWindow(patternLabel_, system ? SW_HIDE : SW_SHOW);
        ShowWindow(patternCombo_, system ? SW_HIDE : SW_SHOW);
        ShowWindow(rearFillLabel_, system ? SW_SHOW : SW_HIDE);
        ShowWindow(rearFillCombo_, system ? SW_SHOW : SW_HIDE);
        ShowWindow(virtualStatus_, system ? SW_SHOW : SW_HIDE);
        SetWindowTextW(
            startButton_, system ? L"Start Routing" : L"Start Test");
    }

    void AppWindow::SetPlaybackControlsEnabled(
        const bool selectable) const
    {
        EnableWindow(frontCombo_, selectable);
        EnableWindow(rearCombo_, selectable);
        EnableWindow(patternCombo_, selectable);
        EnableWindow(modeCombo_, selectable);
        EnableWindow(rearFillCombo_, selectable);
        EnableWindow(frontLevel_, selectable);
        EnableWindow(rearLevel_, selectable);
        EnableWindow(refreshButton_, selectable);
        EnableWindow(saveButton_, selectable);
        EnableWindow(startButton_, selectable);
        EnableWindow(stopButton_, !selectable);
    }

    void AppWindow::SetStatus(const std::wstring& text) const
    {
        SetWindowTextW(status_, text.c_str());
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

    void AppWindow::ApplyFont(const HWND control, const HFONT font) const
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}
