#include "CommandDeckTheme.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace soundstage::ui
{
    namespace
    {
        RECT MakeRect(const int left, const int top,
                      const int right, const int bottom) noexcept
        {
            return RECT{left, top, right, bottom};
        }

        COLORREF Darken(const COLORREF color, const int percent) noexcept
        {
            return RGB(
                GetRValue(color) * percent / 100,
                GetGValue(color) * percent / 100,
                GetBValue(color) * percent / 100);
        }
    }

    int ScaleCommandDeckDip(const int value, const UINT dpi) noexcept
    {
        const std::int64_t effectiveDpi = dpi == 0 ? 96 : dpi;
        return static_cast<int>(
            (static_cast<std::int64_t>(value) * effectiveDpi + 48) / 96);
    }

    bool HasValidPhysicalRouteSelection(
        const int frontIndex, const int rearIndex) noexcept
    {
        return frontIndex >= 0 && rearIndex >= 0 &&
               frontIndex != rearIndex;
    }

    bool IsCommandDeckFormatChange(
        const bool virtualCaptureFault,
        const audio::VirtualSurroundFormat routedFormat,
        const audio::VirtualSurroundFormat detectedFormat) noexcept
    {
        const auto supported = [](const audio::VirtualSurroundFormat format) {
            return format == audio::VirtualSurroundFormat::FivePointOne ||
                   format == audio::VirtualSurroundFormat::SevenPointOne;
        };
        return virtualCaptureFault && supported(routedFormat) &&
               supported(detectedFormat) && routedFormat != detectedFormat;
    }

    bool IsCommandDeckUnsupportedFormatFault(
        const bool virtualCaptureFault,
        const bool hasSingleVirtualEndpoint,
        const audio::VirtualSurroundFormat routedFormat,
        const audio::VirtualSurroundFormat detectedFormat) noexcept
    {
        const bool routedFormatWasSupported =
            routedFormat == audio::VirtualSurroundFormat::FivePointOne ||
            routedFormat == audio::VirtualSurroundFormat::SevenPointOne;
        return virtualCaptureFault && hasSingleVirtualEndpoint &&
               routedFormatWasSupported &&
               detectedFormat == audio::VirtualSurroundFormat::Unsupported;
    }

    bool ShouldAttemptBackgroundStart(
        const bool armed,
        const audio::PlaybackState state,
        const unsigned long long nowMs,
        const unsigned long long lastAttemptMs,
        const unsigned intervalMs) noexcept
    {
        if (!armed)
        {
            return false;
        }
        if (state != audio::PlaybackState::Stopped &&
            state != audio::PlaybackState::Faulted)
        {
            return false;
        }
        return nowMs - lastAttemptMs >= intervalMs;
    }

    CommandDeckLayout ComputeCommandDeckLayout(
        const int clientWidth,
        const int clientHeight,
        const UINT dpi,
        const bool technicalDetailsExpanded) noexcept
    {
        const auto scale = [dpi](const int value) {
            return ScaleCommandDeckDip(value, dpi);
        };
        const bool stacked = clientWidth < scale(960);
        const int margin = scale(stacked ? 16 : 32);
        const int gap = scale(stacked ? 12 : 24);
        const int headerHeight = scale(stacked ? 56 : 72);
        const int actionHeight = scale(stacked ? 108 : 88);
        const int detailsHeight = scale(240);
        const int minMainHeight = scale(stacked ? 534 : 448);

        CommandDeckLayout layout;
        layout.stacked = stacked;
        layout.detailsVisible = technicalDetailsExpanded;
        layout.minimumClientSize = {scale(720), scale(480)};
        layout.preferredClientSize = {
            scale(1240),
            scale(780 + (technicalDetailsExpanded ? 264 : 0))};

        const int designHeight = margin + headerHeight + gap + minMainHeight +
            (technicalDetailsExpanded ? detailsHeight + gap : 0) +
            gap + actionHeight + margin;
        const int effectiveHeight = std::max(clientHeight, designHeight);
        layout.virtualHeight = effectiveHeight;

        const int right = std::max(margin, clientWidth - margin);
        layout.header = MakeRect(
            margin, margin, right, margin + headerHeight);
        layout.actionBar = MakeRect(
            margin,
            effectiveHeight - margin - actionHeight,
            right,
            effectiveHeight - margin);

        int mainBottom = layout.actionBar.top - gap;
        if (technicalDetailsExpanded)
        {
            layout.detailsCard = MakeRect(
                margin,
                mainBottom - detailsHeight,
                right,
                mainBottom);
            mainBottom = layout.detailsCard.top - gap;
        }

        const int mainTop = layout.header.bottom + gap;
        const int contentWidth = std::max(0, right - margin);
        if (!stacked)
        {
            const int availableWidth = std::max(0, contentWidth - gap);
            const int leftWidth = availableWidth * 2 / 3;
            const int splitHeight = std::max(0, mainBottom - mainTop - gap);
            const int frontHeight = splitHeight / 2;
            layout.frontCard = MakeRect(
                margin, mainTop, margin + leftWidth,
                mainTop + frontHeight);
            layout.chairCard = MakeRect(
                margin, layout.frontCard.bottom + gap,
                margin + leftWidth, mainBottom);
            layout.mixCard = MakeRect(
                layout.frontCard.right + gap, mainTop,
                right, mainBottom);
        }
        else
        {
            const int deviceHeight = scale(132);
            layout.frontCard = MakeRect(
                margin, mainTop, right, mainTop + deviceHeight);
            layout.chairCard = MakeRect(
                margin, layout.frontCard.bottom + gap,
                right, layout.frontCard.bottom + gap + deviceHeight);
            layout.mixCard = MakeRect(
                margin, layout.chairCard.bottom + gap,
                right, mainBottom);
        }

        return layout;
    }

    CommandDeckTheme::CommandDeckTheme(const UINT dpi)
    {
        SetDpi(dpi);
    }

    CommandDeckTheme::~CommandDeckTheme()
    {
        ReleaseObjects();
    }

    void CommandDeckTheme::SetDpi(const UINT dpi)
    {
        const UINT effectiveDpi = dpi == 0 ? 96 : dpi;
        if (dpi_ == effectiveDpi && windowBrush_ != nullptr)
        {
            return;
        }

        ReleaseObjects();
        dpi_ = effectiveDpi;
        titleFont_ = CreateFontW(
            -Scale(30), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Display");
        headingFont_ = CreateFontW(
            -Scale(18), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
        bodyFont_ = CreateFontW(
            -Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
        smallFont_ = CreateFontW(
            -Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");

        windowBrush_ = CreateSolidBrush(colors_.window);
        cardBrush_ = CreateSolidBrush(colors_.card);
        cardRaisedBrush_ = CreateSolidBrush(colors_.cardRaised);
        accentBrush_ = CreateSolidBrush(colors_.accent);
        healthyBrush_ = CreateSolidBrush(colors_.healthy);
        warningBrush_ = CreateSolidBrush(colors_.warning);
        faultBrush_ = CreateSolidBrush(colors_.fault);
        borderPen_ = CreatePen(PS_SOLID, std::max(1, Scale(1)), colors_.border);
        accentPen_ = CreatePen(PS_SOLID, std::max(1, Scale(2)), colors_.accent);
        faultPen_ = CreatePen(PS_SOLID, std::max(1, Scale(2)), colors_.fault);

        if (titleFont_ == nullptr || headingFont_ == nullptr ||
            bodyFont_ == nullptr || smallFont_ == nullptr ||
            windowBrush_ == nullptr || cardBrush_ == nullptr ||
            cardRaisedBrush_ == nullptr || accentBrush_ == nullptr ||
            healthyBrush_ == nullptr || warningBrush_ == nullptr ||
            faultBrush_ == nullptr || borderPen_ == nullptr ||
            accentPen_ == nullptr || faultPen_ == nullptr)
        {
            ReleaseObjects();
            throw std::runtime_error("Unable to create Command Deck theme resources.");
        }
    }

    int CommandDeckTheme::Scale(const int value) const noexcept
    {
        return ScaleCommandDeckDip(value, dpi_);
    }

    void CommandDeckTheme::EraseBackground(
        const HDC dc, const RECT& bounds) const noexcept
    {
        FillRect(dc, &bounds, windowBrush_);
    }

    void CommandDeckTheme::PaintCard(
        const HDC dc, const RECT& bounds, const COLORREF fill,
        const bool faulted) const noexcept
    {
        HBRUSH brush = BrushFor(fill);
        bool ownsBrush = false;
        if (brush == nullptr)
        {
            brush = CreateSolidBrush(fill);
            ownsBrush = brush != nullptr;
        }
        if (brush == nullptr)
        {
            return;
        }

        const int saved = SaveDC(dc);
        SelectObject(dc, brush);
        SelectObject(dc, faulted ? faultPen_ : borderPen_);
        const int radius = Scale(16);
        RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom,
                  radius, radius);
        RestoreDC(dc, saved);
        if (ownsBrush)
        {
            DeleteObject(brush);
        }
    }

    void CommandDeckTheme::PaintButton(
        const DRAWITEMSTRUCT& item,
        const bool primary,
        const bool active) const
    {
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool focused = (item.itemState & ODS_FOCUS) != 0;
        COLORREF fill = primary ? colors_.accent : colors_.cardRaised;
        COLORREF text = primary ? colors_.window : colors_.text;
        if (disabled)
        {
            fill = colors_.cardRaised;
            text = colors_.secondary;
        }
        else if (pressed)
        {
            fill = Darken(fill, 78);
        }

        HBRUSH fillBrush = CreateSolidBrush(fill);
        if (fillBrush == nullptr)
        {
            return;
        }
        const int saved = SaveDC(item.hDC);
        SelectObject(item.hDC, fillBrush);
        SelectObject(item.hDC, active ? accentPen_ : borderPen_);
        const int radius = Scale(10);
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
                  item.rcItem.right, item.rcItem.bottom, radius, radius);
        const int labelLength = std::max(
            0, GetWindowTextLengthW(item.hwndItem));
        std::wstring label(
            static_cast<std::size_t>(labelLength + 1), L'\0');
        if (labelLength > 0)
        {
            GetWindowTextW(item.hwndItem, label.data(),
                           static_cast<int>(label.size()));
        }
        label.resize(static_cast<std::size_t>(labelLength));
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, text);
        SelectObject(item.hDC, bodyFont_);
        RECT textRect = item.rcItem;
        DrawTextW(item.hDC, label.c_str(), static_cast<int>(label.size()),
                  &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (focused)
        {
            RECT focusRect = item.rcItem;
            InflateRect(&focusRect, -Scale(4), -Scale(4));
            DrawFocusRect(item.hDC, &focusRect);
        }
        RestoreDC(item.hDC, saved);
        DeleteObject(fillBrush);
    }

    HBRUSH CommandDeckTheme::PrepareControl(
        const UINT message,
        const HDC dc,
        const HWND control,
        const COLORREF textColor,
        const COLORREF backgroundColor) const noexcept
    {
        SetTextColor(dc, IsWindowEnabled(control) ? textColor : colors_.secondary);
        if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ||
            message == WM_CTLCOLORBTN)
        {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, colors_.cardRaised);
            return cardRaisedBrush_;
        }

        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, backgroundColor);
        const HBRUSH brush = BrushFor(backgroundColor);
        return brush == nullptr ? cardBrush_ : brush;
    }

    void CommandDeckTheme::ReleaseObjects() noexcept
    {
        for (const HGDIOBJ object : {
                 reinterpret_cast<HGDIOBJ>(titleFont_),
                 reinterpret_cast<HGDIOBJ>(headingFont_),
                 reinterpret_cast<HGDIOBJ>(bodyFont_),
                 reinterpret_cast<HGDIOBJ>(smallFont_),
                 reinterpret_cast<HGDIOBJ>(windowBrush_),
                 reinterpret_cast<HGDIOBJ>(cardBrush_),
                 reinterpret_cast<HGDIOBJ>(cardRaisedBrush_),
                 reinterpret_cast<HGDIOBJ>(accentBrush_),
                 reinterpret_cast<HGDIOBJ>(healthyBrush_),
                 reinterpret_cast<HGDIOBJ>(warningBrush_),
                 reinterpret_cast<HGDIOBJ>(faultBrush_),
                 reinterpret_cast<HGDIOBJ>(borderPen_),
                 reinterpret_cast<HGDIOBJ>(accentPen_),
                 reinterpret_cast<HGDIOBJ>(faultPen_)})
        {
            if (object != nullptr)
            {
                DeleteObject(object);
            }
        }
        titleFont_ = nullptr;
        headingFont_ = nullptr;
        bodyFont_ = nullptr;
        smallFont_ = nullptr;
        windowBrush_ = nullptr;
        cardBrush_ = nullptr;
        cardRaisedBrush_ = nullptr;
        accentBrush_ = nullptr;
        healthyBrush_ = nullptr;
        warningBrush_ = nullptr;
        faultBrush_ = nullptr;
        borderPen_ = nullptr;
        accentPen_ = nullptr;
        faultPen_ = nullptr;
    }

    HBRUSH CommandDeckTheme::BrushFor(const COLORREF color) const noexcept
    {
        if (color == colors_.window) return windowBrush_;
        if (color == colors_.card) return cardBrush_;
        if (color == colors_.cardRaised) return cardRaisedBrush_;
        if (color == colors_.accent) return accentBrush_;
        if (color == colors_.healthy) return healthyBrush_;
        if (color == colors_.warning) return warningBrush_;
        if (color == colors_.fault) return faultBrush_;
        return nullptr;
    }
}
