#include "d2d.h"
#include "globals.h"
#include "theme.h"
#include "utils.h"
#include "window.h"
#include <algorithm>
#include <d2d1.h>
#include <vector>

namespace
{
constexpr float kPadding = 5.0f;
constexpr float kGap = 6.0f;
constexpr float kGroupGap = 14.0f;
constexpr float kRowGap = 10.0f;
OverlayLayout g_layout;
bool g_layoutValid = false;
float g_maxWidth = 0.0f;

std::pair<float, float> MeasureLayout(IDWriteTextLayout *layout)
{
    DWRITE_TEXT_METRICS metrics{};
    if (layout)
        layout->GetMetrics(&metrics);
    return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

void EnsureCache()
{
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfo(MonitorFromWindow(::D2DHwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const float maxWidth = (monitor.rcWork.right - monitor.rcWork.left) * 0.5f / GetWindowScale(::D2DHwnd);
    if (!g_layoutValid || g_maxWidth != maxWidth)
    {
        g_layout.Build(::OverlayHistory, maxWidth);
        // Drop complete expired rows so the next rebuild preserves row boundaries.
        const size_t discarded = g_layout.DiscardedStrokeCount();
        if (discarded > 0)
        {
            ::OverlayHistory.erase(::OverlayHistory.begin(), ::OverlayHistory.begin() + discarded);
            g_layout.Build(::OverlayHistory, maxWidth);
        }
        // Preserve row identity when older rows expire or the surface is redrawn.
        for (const auto &row : g_layout.Rows())
        {
            ::OverlayHistory[row.begin].startsRow = true;
            for (size_t i = row.begin; i < row.end; ++i)
                ::OverlayHistory[i].updatedAt = row.updatedAt;
        }
        g_maxWidth = maxWidth;
        g_layoutValid = true;
    }
}

std::wstring KeyLegend(std::wstring key)
{
    if (key.size() == 1 && key[0] >= L'a' && key[0] <= L'z')
        key[0] -= L'a' - L'A';
    if (key == L"Hom")
        return L"Home";
    if (key == L"Pgu")
        return L"PgUp";
    if (key == L"Pgd")
        return L"PgDn";
    if (key == L"App")
        return L"Menu";
    return key;
}
} // namespace

void OverlayLayout::Build(const std::vector<OverlayStroke> &history, float maxWidth)
{
    caps_.clear();
    rows_.clear();
    width_ = height_ = 0.0f;
    scale_ = 1.0f;
    discardedStrokes_ = rowCount_ = 0;
    if (!pDWriteFactory || !pTextFormat || history.empty())
        return;
    const float unit = ::KeycastConfig.fontSize + 28.0f;
    struct Group
    {
        size_t begin, end, stroke;
        float width;
    };
    std::vector<Group> groups;
    float widestGroup = 0.0f;
    for (size_t group = 0; group < history.size(); ++group)
    {
        const auto &stroke = history[group];
        const size_t begin = caps_.size();
        float x = 0.0f;
        for (size_t i = 0; i < stroke.keys.size(); ++i)
        {
            std::wstring legend = KeyLegend(stroke.keys[i]);
            const bool home = legend == L"F" || legend == L"J";
            if (i + 1 == stroke.keys.size() && stroke.repeat > 1)
                legend += L" ×" + std::to_wstring(stroke.repeat);
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            if (FAILED(pDWriteFactory->CreateTextLayout(legend.c_str(), static_cast<UINT32>(legend.size()), pTextFormat, FLT_MAX, FLT_MAX, layout.GetAddressOf())))
                continue;
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            const auto size = MeasureLayout(layout.Get());
            float width = (std::max)(unit, size.first + 24.0f);
            if (stroke.keys[i] == L"Space")
                width = (std::max)(width, unit * 2.5f);
            if (stroke.keys[i] == L"Enter" || stroke.keys[i] == L"Backspace")
                width = (std::max)(width, unit * 1.7f);
            if (caps_.size() > begin)
                x += kGap;
            caps_.push_back({layout, x, 0.0f, width, size.first, size.second, 0, group, group + 1 == history.size(), home});
            x += width;
        }
        if (caps_.size() > begin)
        {
            groups.push_back({begin, caps_.size(), group, x});
            widestGroup = (std::max)(widestGroup, x);
        }
    }
    if (groups.empty())
        return;
    // An oversized chord occupies its own row and scales with the layout.
    const float available = (std::max)(1.0f, maxWidth - 2.0f * kPadding);
    const float rowLimit = (std::max)(available, widestGroup);
    std::vector<float> rowWidths(1, 0.0f);
    std::vector<OverlayRow> allRows(1, {0, 0, 0});
    for (const auto &group : groups)
    {
        float gap = rowWidths.back() > 0.0f ? kGroupGap : 0.0f;
        if (rowWidths.back() > 0.0f && (history[group.stroke].startsRow || rowWidths.back() + gap + group.width > rowLimit))
        {
            rowWidths.push_back(0.0f);
            allRows.push_back({group.stroke, group.stroke, 0});
            gap = 0.0f;
        }
        for (size_t i = group.begin; i < group.end; ++i)
        {
            caps_[i].x += kPadding + rowWidths.back() + gap;
            caps_[i].row = rowWidths.size() - 1;
        }
        rowWidths.back() += gap + group.width;
        allRows.back().end = group.stroke + 1;
        allRows.back().updatedAt = (std::max)(allRows.back().updatedAt, history[group.stroke].updatedAt);
    }
    const size_t maxRows = static_cast<size_t>(std::clamp(::KeycastConfig.rows, 1, 5));
    const size_t firstRow = rowWidths.size() > maxRows ? rowWidths.size() - maxRows : 0;
    const auto firstCap = std::find_if(caps_.begin(), caps_.end(), [firstRow](const Cap &cap) { return cap.row >= firstRow; });
    discardedStrokes_ = firstCap->group;
    caps_.erase(caps_.begin(), firstCap);
    rowCount_ = rowWidths.size() - firstRow;
    rows_.assign(allRows.begin() + firstRow, allRows.end());
    const float contentWidth = *std::max_element(rowWidths.begin() + firstRow, rowWidths.end());
    const bool alignRight = ::KeycastConfig.position == L"bottom-right" || ::KeycastConfig.position == L"top-right";
    for (auto &cap : caps_)
    {
        if (alignRight)
            cap.x += contentWidth - rowWidths[cap.row];
        cap.y = kPadding + (cap.row - firstRow) * (unit + kRowGap);
        cap.row -= firstRow;
    }
    width_ = contentWidth + 2.0f * kPadding;
    height_ = rowCount_ * unit + (rowCount_ - 1) * kRowGap + 2.0f * kPadding;
    if (width_ > maxWidth && maxWidth > 0.0f)
    {
        scale_ = maxWidth / width_;
        width_ = maxWidth;
        height_ *= scale_;
    }
}

void OverlayLayout::Draw(ID2D1RenderTarget *target, ID2D1SolidColorBrush *brush, D2D1_POINT_2F origin, ULONGLONG now) const
{
    if (!target || !brush)
        return;
    D2D1_MATRIX_3X2_F previousTransform;
    target->GetTransform(&previousTransform);
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_, origin) * previousTransform);
    const bool light = IsLightThemeResolved();
    const float unit = ::KeycastConfig.fontSize + 28.0f;
    const float radius = (std::min)(::KeycastConfig.cornerRadius, unit * 0.25f);
    const auto colors = GetOverlayTextColors();
    const auto fill = [&](D2D1_RECT_F rect, float r, D2D1_COLOR_F color) {
        brush->SetColor(color);
        target->FillRoundedRectangle(D2D1::RoundedRect(rect, r, r), brush);
    };
    for (const auto &cap : caps_)
    {
        const float rowOpacity = now == 0 ? 1.0f : GetOverlayRowOpacity(rows_[cap.row].updatedAt, now, ::KeycastConfig.fadeMs);
        const auto fade = [rowOpacity](D2D1_COLOR_F color) {
            color.a *= rowOpacity;
            return color;
        };
        const float x = origin.x + cap.x;
        const float y = origin.y + cap.y;
        const float faceBottom = y + unit;
        const float opacity = ::KeycastConfig.bgOpacity * rowOpacity;
        // A single flat surface keeps every key separate without a raised shell.
        fill(D2D1::RectF(x, y, x + cap.width, faceBottom), radius, ColorFromHex(cap.active ? (light ? 0xf1ecd9 : 0x414036) : (light ? 0xe9ecf1 : 0x2e3139), opacity));
        if (::KeycastConfig.borderThickness > 0.1f)
        {
            const float thickness = ::KeycastConfig.borderThickness;
            brush->SetColor(ColorFromHex(cap.active ? (light ? 0xb4a77b : 0xa79b6b) : (light ? 0xbfc5ce : 0x4a4e59), (cap.active ? 0.65f : 1.0f) * rowOpacity));
            target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + thickness / 2, y + thickness / 2, x + cap.width - thickness / 2, faceBottom - thickness / 2), radius, radius), brush, thickness);
        }
        auto ink = ColorFromHex(cap.active ? (light ? 0x827346 : 0xd5c78e) : (light ? 0x414752 : 0xc3c7cf));
        if (::KeycastConfig.rainbowText && !cap.active)
            ink = colors[cap.group % colors.size()];
        brush->SetColor(fade(ink));
        target->DrawTextLayout(D2D1::Point2F(x + (cap.width - cap.textWidth) / 2, y + (unit - cap.textHeight) / 2 - 1), cap.layout.Get(), brush);
        if (cap.home)
        {
            target->DrawLine(D2D1::Point2F(x + cap.width / 2 - 4, faceBottom - 9), D2D1::Point2F(x + cap.width / 2 + 4, faceBottom - 9), brush, 2.0f);
        }
    }
    target->SetTransform(previousTransform);
}

std::vector<D2D1_COLOR_F> GetOverlayTextColors()
{
    // clang-format off
    return {
        D2D1::ColorF(223 / 255.0f, 84 / 255.0f, 82 / 255.0f),
        D2D1::ColorF(82 / 255.0f, 158 / 255.0f, 114 / 255.0f),
        GetTheme().overlayText,
        D2D1::ColorF(202 / 255.0f, 152 / 255.0f, 72 / 255.0f),
        D2D1::ColorF(186 / 255.0f, 133 / 255.0f, 111 / 255.0f),
        D2D1::ColorF(55 / 255.0f, 154 / 255.0f, 211 / 255.0f),
        D2D1::ColorF(199 / 255.0f, 125 / 255.0f, 72 / 255.0f),
        D2D1::ColorF(157 / 255.0f, 104 / 255.0f, 211 / 255.0f),
        D2D1::ColorF(155 / 255.0f, 155 / 255.0f, 155 / 255.0f),
        D2D1::ColorF(209 / 255.0f, 87 / 255.0f, 150 / 255.0f),
    };
    // clang-format on
}

bool InitD2DAndDWrite()
{
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
    if (FAILED(hr))
    {
        return false;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&pDWriteFactory));
    if (FAILED(hr))
    {
        return false;
    }

    RecreateOverlayTextFormat();
    return pTextFormat != nullptr;
}

void RecreateOverlayTextFormat()
{
    if (!pDWriteFactory)
    {
        return;
    }
    if (pTextFormat)
    {
        pTextFormat->Release();
        pTextFormat = nullptr;
    }
    pDWriteFactory->CreateTextFormat(::fontName.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, ::KeycastConfig.fontSize, L"zh-cn", &pTextFormat);
    InvalidateOverlayCache();
}

bool InitD2DRenderTarget(HWND hwnd)
{
    if (!pD2DFactory)
    {
        return false;
    }

    if (pBrush)
    {
        pBrush->Release();
        pBrush = nullptr;
    }
    if (pRenderTarget)
    {
        pRenderTarget->Release();
        pRenderTarget = nullptr;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    auto renderTargetProps = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
    renderTargetProps.dpiX = renderTargetProps.dpiY = static_cast<float>(GetDpiForWindow(hwnd));
    const auto hwndRenderTargetProps = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top), D2D1_PRESENT_OPTIONS_NONE);
    HRESULT hr = pD2DFactory->CreateHwndRenderTarget(renderTargetProps, hwndRenderTargetProps, &pRenderTarget);
    if (SUCCEEDED(hr))
    {
        hr = pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pBrush);
    }
    return SUCCEEDED(hr);
}

void ResizeOverlayRenderTarget(HWND hwnd)
{
    if (!pRenderTarget)
    {
        InitD2DRenderTarget(hwnd);
        return;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
    pRenderTarget->SetDpi(dpi, dpi);
    pRenderTarget->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
}

void InvalidateOverlayCache()
{
    g_layout = OverlayLayout{};
    g_layoutValid = false;
}

void ReleaseOverlayCache()
{
    g_layout = OverlayLayout{};
    g_layoutValid = false;
}

void ReleaseOverlayD2D()
{
    ReleaseOverlayCache();
    if (pBrush)
    {
        pBrush->Release();
        pBrush = nullptr;
    }
    if (pRenderTarget)
    {
        pRenderTarget->Release();
        pRenderTarget = nullptr;
    }
    if (pTextFormat)
    {
        pTextFormat->Release();
        pTextFormat = nullptr;
    }
    if (pDWriteFactory)
    {
        pDWriteFactory->Release();
        pDWriteFactory = nullptr;
    }
    if (pD2DFactory)
    {
        pD2DFactory->Release();
        pD2DFactory = nullptr;
    }
}

void OnPaint(HWND hwnd)
{
    if (!pRenderTarget || !pBrush)
    {
        return;
    }
    EnsureCache();
    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    const auto surface = pRenderTarget->GetSize();
    const bool right = ::KeycastConfig.position == L"bottom-right" || ::KeycastConfig.position == L"top-right";
    const bool bottom = ::KeycastConfig.position == L"bottom-right" || ::KeycastConfig.position == L"bottom-left";
    g_layout.Draw(pRenderTarget, pBrush, D2D1::Point2F(right ? surface.width - g_layout.Width() : 0.0f, bottom ? surface.height - g_layout.Height() : 0.0f), GetTickCount64());

    HRESULT hr = pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        InvalidateOverlayCache();
        InitD2DRenderTarget(hwnd);
    }
}

void GetOverlayContentDipSize(float &width, float &height)
{
    // Allocate the full capacity once; typing and expiration only repaint within it.
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfo(MonitorFromWindow(::D2DHwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
    width = (monitor.rcWork.right - monitor.rcWork.left) * 0.5f / GetWindowScale(::D2DHwnd);
    const int rows = std::clamp(::KeycastConfig.rows, 1, 5);
    height = rows * (::KeycastConfig.fontSize + 28.0f) + (rows - 1) * kRowGap + 2.0f * kPadding;
}

void RequestOverlayRepaint()
{
    if (!::D2DHwnd)
    {
        return;
    }
    EnsureCache();
    UpdateOverlayWindowPlacement(::D2DHwnd);
    // Present the new contents before exposing a previously hidden surface.
    OnPaint(::D2DHwnd);
    if (::KeycastConfig.enabled && !::OverlayHistory.empty() && !IsWindowVisible(::D2DHwnd))
        ShowWindow(::D2DHwnd, SW_SHOWNOACTIVATE);
}

float GetOverlayRowOpacity(ULONGLONG updatedAt, ULONGLONG now, int waitMs)
{
    if (now <= updatedAt)
        return 1.0f;
    const ULONGLONG elapsed = now - updatedAt;
    const auto wait = static_cast<ULONGLONG>((std::max)(0, waitMs));
    if (elapsed <= wait)
        return 1.0f;
    return 1.0f - (std::min)(1.0f, static_cast<float>(elapsed - wait) / OVERLAY_FADE_DURATION_MS);
}

void TickOverlayAnimation(HWND hwnd, ULONGLONG now)
{
    EnsureCache();
    std::vector<OverlayStroke> retained;
    bool expired = false;
    bool fading = false;
    for (const auto &row : g_layout.Rows())
    {
        const float opacity = GetOverlayRowOpacity(row.updatedAt, now, ::KeycastConfig.fadeMs);
        if (opacity <= 0.0f)
        {
            expired = true;
            continue;
        }
        fading |= opacity < 1.0f;
        retained.insert(retained.end(), ::OverlayHistory.begin() + row.begin, ::OverlayHistory.begin() + row.end);
    }
    if (expired)
    {
        ::OverlayHistory = std::move(retained);
        InvalidateOverlayCache();
    }
    if (::OverlayHistory.empty())
    {
        if (hwnd)
        {
            KillTimer(hwnd, OVERLAY_ANIMATION_TIMER);
            ShowWindow(hwnd, SW_HIDE);
        }
        return;
    }
    if (hwnd && ::KeycastConfig.enabled && (expired || fading))
        RequestOverlayRepaint();
}

bool InitGlobalConfigWithD2D()
{
    if (!pDWriteFactory || !pTextFormat)
    {
        return false;
    }
    const float scale = GetWindowScale(::D2DHwnd ? ::D2DHwnd : nullptr);
    if (scale <= 0.0f)
    {
        return false;
    }

    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    GetMonitorInfo(MonitorFromWindow(::D2DHwnd ? ::D2DHwnd : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY), &monitorInfo);
    const float maxPx = static_cast<float>(monitorInfo.rcWork.right - monitorInfo.rcWork.left) * 0.5f;

    IDWriteTextLayout *layout = nullptr;
    if (FAILED(pDWriteFactory->CreateTextLayout(L"M", 1, pTextFormat, FLT_MAX, FLT_MAX, &layout)) || !layout)
    {
        return false;
    }
    const float mWidth = MeasureLayout(layout).first * scale;
    layout->Release();
    if (mWidth <= 0.1f)
    {
        return false;
    }
    ::KeycastConfig.maxSize = (std::max)(8, static_cast<int>(maxPx / mWidth) - 3);
    return true;
}
