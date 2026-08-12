#pragma once

#include "../audio/VirtualSurroundContract.h"

#include <windows.h>

namespace soundstage::ui
{
    struct CommandDeckColors
    {
        COLORREF window = RGB(7, 13, 23);
        COLORREF card = RGB(13, 23, 38);
        COLORREF cardRaised = RGB(16, 29, 46);
        COLORREF border = RGB(34, 51, 74);
        COLORREF text = RGB(246, 249, 255);
        COLORREF secondary = RGB(143, 163, 189);
        COLORREF accent = RGB(69, 216, 255);
        COLORREF healthy = RGB(79, 224, 173);
        COLORREF warning = RGB(255, 201, 107);
        COLORREF fault = RGB(255, 99, 125);
    };

    struct CommandDeckLayout
    {
        RECT header{};
        RECT frontCard{};
        RECT chairCard{};
        RECT mixCard{};
        RECT detailsCard{};
        RECT actionBar{};
        SIZE minimumClientSize{};
        SIZE preferredClientSize{};
        // Height of the laid-out canvas. Never less than the design height,
        // so a client shorter than the design scrolls instead of overlapping.
        int virtualHeight = 0;
        bool stacked = false;
        bool detailsVisible = false;
    };

    [[nodiscard]] int ScaleCommandDeckDip(int value, UINT dpi) noexcept;

    [[nodiscard]] bool HasValidPhysicalRouteSelection(
        int frontIndex, int rearIndex) noexcept;

    [[nodiscard]] bool IsCommandDeckFormatChange(
        bool virtualCaptureFault,
        audio::VirtualSurroundFormat routedFormat,
        audio::VirtualSurroundFormat detectedFormat) noexcept;

    [[nodiscard]] bool IsCommandDeckUnsupportedFormatFault(
        bool virtualCaptureFault,
        bool hasSingleVirtualEndpoint,
        audio::VirtualSurroundFormat routedFormat,
        audio::VirtualSurroundFormat detectedFormat) noexcept;

    // Background launches retry routing until the saved outputs (typically
    // a late-connecting Bluetooth device) come back; attempts are rate
    // limited and stop while the engine is starting, running, or stopping.
    [[nodiscard]] bool ShouldAttemptBackgroundStart(
        bool armed,
        audio::PlaybackState state,
        unsigned long long nowMs,
        unsigned long long lastAttemptMs,
        unsigned intervalMs) noexcept;

    [[nodiscard]] CommandDeckLayout ComputeCommandDeckLayout(
        int clientWidth,
        int clientHeight,
        UINT dpi,
        bool technicalDetailsExpanded) noexcept;

    class CommandDeckTheme final
    {
    public:
        explicit CommandDeckTheme(UINT dpi = 96);
        ~CommandDeckTheme();

        CommandDeckTheme(const CommandDeckTheme&) = delete;
        CommandDeckTheme& operator=(const CommandDeckTheme&) = delete;

        void SetDpi(UINT dpi);

        [[nodiscard]] UINT Dpi() const noexcept { return dpi_; }
        [[nodiscard]] int Scale(int value) const noexcept;
        [[nodiscard]] const CommandDeckColors& Colors() const noexcept
        {
            return colors_;
        }

        [[nodiscard]] HFONT TitleFont() const noexcept { return titleFont_; }
        [[nodiscard]] HFONT HeadingFont() const noexcept { return headingFont_; }
        [[nodiscard]] HFONT BodyFont() const noexcept { return bodyFont_; }
        [[nodiscard]] HFONT SmallFont() const noexcept { return smallFont_; }
        [[nodiscard]] HBRUSH WindowBrush() const noexcept
        {
            return windowBrush_;
        }

        void EraseBackground(HDC dc, const RECT& bounds) const noexcept;
        void PaintCard(HDC dc, const RECT& bounds, COLORREF fill,
                       bool faulted = false) const noexcept;
        void PaintButton(const DRAWITEMSTRUCT& item, bool primary,
                         bool active) const;

        [[nodiscard]] HBRUSH PrepareControl(
            UINT message,
            HDC dc,
            HWND control,
            COLORREF textColor,
            COLORREF backgroundColor) const noexcept;

    private:
        void ReleaseObjects() noexcept;
        [[nodiscard]] HBRUSH BrushFor(COLORREF color) const noexcept;

        CommandDeckColors colors_{};
        UINT dpi_ = 0;
        HFONT titleFont_ = nullptr;
        HFONT headingFont_ = nullptr;
        HFONT bodyFont_ = nullptr;
        HFONT smallFont_ = nullptr;
        HBRUSH windowBrush_ = nullptr;
        HBRUSH cardBrush_ = nullptr;
        HBRUSH cardRaisedBrush_ = nullptr;
        HBRUSH accentBrush_ = nullptr;
        HBRUSH healthyBrush_ = nullptr;
        HBRUSH warningBrush_ = nullptr;
        HBRUSH faultBrush_ = nullptr;
        HPEN borderPen_ = nullptr;
        HPEN accentPen_ = nullptr;
        HPEN faultPen_ = nullptr;
    };
}
