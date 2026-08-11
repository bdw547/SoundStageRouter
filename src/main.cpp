#include "AppWindow.h"

#include <commctrl.h>
#include <objbase.h>
#include <windows.h>

#include <exception>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, const int showCommand)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC =
        ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&commonControls);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        MessageBoxW(nullptr, L"Windows COM initialization failed.", L"SoundStage Router",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    int result = 1;
    try
    {
        soundstage::AppWindow app(instance);
        result = app.Run(showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "SoundStage Router", MB_OK | MB_ICONERROR);
    }

    CoUninitialize();
    return result;
}
