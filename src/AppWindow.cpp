#include "AppWindow.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
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
        : instance_(instance), settings_(settingsStore_.Load())
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
                                  980, 700, nullptr, nullptr, instance_, this);
        if (window_ == nullptr)
        {
            throw std::runtime_error("Unable to create the application window.");
        }
    }

    AppWindow::~AppWindow()
    {
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
            return 0;
        case WM_SIZE:
            LayoutControls(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case RefreshButtonId:
                RefreshDevices();
                return 0;
            case SaveButtonId:
                SaveSettings();
                return 0;
            default:
                break;
            }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(246, 247, 250));
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        case WM_DESTROY:
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

        CreateLabel(window_, L"Front output (soundbar / monitor speaker)");
        frontCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(FrontComboId), instance_, nullptr);
        CreateLabel(window_, L"Delay (ms)");
        frontDelay_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | WS_TABSTOP,
            0, 0, 0, 0, window_, ControlId(FrontDelayId), instance_, nullptr);

        CreateLabel(window_, L"Rear output (Bluetooth headrest)");
        rearCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, window_, ControlId(RearComboId), instance_, nullptr);
        CreateLabel(window_, L"Delay (ms)");
        rearDelay_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT | WS_TABSTOP,
            0, 0, 0, 0, window_, ControlId(RearDelayId), instance_, nullptr);

        refreshButton_ = CreateWindowExW(0, L"BUTTON", L"Refresh devices",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_, ControlId(RefreshButtonId), instance_, nullptr);
        saveButton_ = CreateWindowExW(0, L"BUTTON", L"Save layout",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, window_, ControlId(SaveButtonId), instance_, nullptr);
        status_ = CreateLabel(window_, L"", SS_LEFT | SS_ENDELLIPSIS);

        for (const HWND child : {title_, subtitle_, deviceList_, frontCombo_, frontDelay_,
                                 rearCombo_, rearDelay_, refreshButton_, saveButton_, status_})
        {
            ApplyFont(child, bodyFont_);
        }
        ApplyFont(title_, titleFont_);
        ApplyFont(subtitle_, smallFont_);
        ApplyFont(status_, smallFont_);
    }

    void AppWindow::LayoutControls(const int width, const int height) const
    {
        const int margin = 28;
        const int contentWidth = std::max(300, width - margin * 2);
        MoveWindow(title_, margin, 22, contentWidth, 36, TRUE);
        MoveWindow(subtitle_, margin, 59, contentWidth, 24, TRUE);
        MoveWindow(deviceList_, margin, 94, contentWidth, std::max(170, height - 380), TRUE);

        const int formTop = std::max(285, height - 270);
        const int delayWidth = 100;
        const int gap = 16;
        const int comboWidth = contentWidth - delayWidth - gap;

        const HWND frontLabel = GetWindow(window_, GW_CHILD);
        HWND child = frontLabel;
        std::vector<HWND> labels;
        while (child != nullptr)
        {
            wchar_t className[32]{};
            GetClassNameW(child, className, static_cast<int>(std::size(className)));
            if (wcscmp(className, L"Static") == 0 && child != title_ && child != subtitle_ &&
                child != status_)
            {
                labels.push_back(child);
            }
            child = GetWindow(child, GW_HWNDNEXT);
        }

        if (labels.size() >= 4)
        {
            MoveWindow(labels[0], margin, formTop, comboWidth, 23, TRUE);
            MoveWindow(labels[1], margin + comboWidth + gap, formTop, delayWidth, 23, TRUE);
            MoveWindow(labels[2], margin, formTop + 72, comboWidth, 23, TRUE);
            MoveWindow(labels[3], margin + comboWidth + gap, formTop + 72, delayWidth, 23, TRUE);
        }

        MoveWindow(frontCombo_, margin, formTop + 25, comboWidth, 220, TRUE);
        MoveWindow(frontDelay_, margin + comboWidth + gap, formTop + 25, delayWidth, 30, TRUE);
        MoveWindow(rearCombo_, margin, formTop + 97, comboWidth, 220, TRUE);
        MoveWindow(rearDelay_, margin + comboWidth + gap, formTop + 97, delayWidth, 30, TRUE);
        MoveWindow(refreshButton_, margin, formTop + 148, 150, 36, TRUE);
        MoveWindow(saveButton_, margin + 162, formTop + 148, 140, 36, TRUE);
        MoveWindow(status_, margin, formTop + 196, contentWidth, 25, TRUE);
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

            ComboBox_AddString(frontCombo_, endpoint.name.c_str());
            ComboBox_AddString(rearCombo_, endpoint.name.c_str());
        }

        int frontSelection = FindEndpoint(settings_.frontEndpointId);
        if (frontSelection < 0)
        {
            const auto defaultDevice = std::find_if(endpoints_.begin(), endpoints_.end(),
                [](const AudioEndpoint& endpoint) { return endpoint.isDefault; });
            frontSelection = defaultDevice == endpoints_.end()
                ? (endpoints_.empty() ? -1 : 0)
                : static_cast<int>(std::distance(endpoints_.begin(), defaultDevice));
        }

        int rearSelection = FindEndpoint(settings_.rearEndpointId);
        if (rearSelection < 0)
        {
            for (std::size_t index = 0; index < endpoints_.size(); ++index)
            {
                if (static_cast<int>(index) != frontSelection)
                {
                    rearSelection = static_cast<int>(index);
                    break;
                }
            }
        }

        ComboBox_SetCurSel(frontCombo_, frontSelection);
        ComboBox_SetCurSel(rearCombo_, rearSelection);
        SetWindowTextW(frontDelay_, std::to_wstring(settings_.frontDelayMs).c_str());
        SetWindowTextW(rearDelay_, std::to_wstring(settings_.rearDelayMs).c_str());
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
        return selected == CB_ERR ? -1 : static_cast<int>(selected);
    }

    int AppWindow::ReadDelay(const HWND edit) const
    {
        BOOL valid = FALSE;
        const UINT value = GetDlgItemInt(window_, GetDlgCtrlID(edit), &valid, FALSE);
        if (valid == FALSE) return 0;
        return std::clamp(static_cast<int>(value), 0, 2000);
    }

    void AppWindow::ApplyFont(const HWND control, const HFONT font) const
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}
