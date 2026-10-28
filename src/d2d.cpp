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
    width_ = height_ = 0.0f;
    scale_ = 1.0f;
    if (!pDWriteFactory || !pTextFormat || history.empty())
        return;
    const float unit = ::KeycastConfig.fontSize + 28.0f;
    height_ = unit + 2.0f * kPadding;
    float x = kPadding;
    for (size_t group = 0; group < history.size(); ++group)
    {
        const auto &stroke = history[group];
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
            if (!caps_.empty())
                x += i == 0 ? kGroupGap : kGap;
            caps_.push_back({layout, x, width, size.first, size.second, group, group + 1 == history.size(), home});
            x += width;
        }
    }
    width_ = caps_.empty() ? 0.0f : x + kPadding;
    // Keep the newest complete strokes; long histories must not grow off screen.
    while (width_ > maxWidth && caps_.size() > 1 && caps_.front().group != caps_.back().group)
    {
        const size_t oldest = caps_.front().group;
        const auto next = std::find_if(caps_.begin(), caps_.end(), [oldest](const Cap &cap) { return cap.group != oldest; });
        const float shift = next->x - kPadding;
        caps_.erase(caps_.begin(), next);
        for (auto &cap : caps_)
            cap.x -= shift;
        width_ -= shift;
    }
    if (width_ > maxWidth && maxWidth > 0.0f)
    {
        scale_ = maxWidth / width_;
        width_ = maxWidth;
        height_ *= scale_;
    }
}

void OverlayLayout::Draw(ID2D1RenderTarget *target, ID2D1SolidColorBrush *brush, D2D1_POINT_2F origin) const
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
        const float x = origin.x + cap.x;
        const float y = origin.y + kPadding;
        const float faceBottom = y + unit;
        const float opacity = ::KeycastConfig.bgOpacity;
        // A single flat surface keeps every key separate without a raised shell.
        fill(D2D1::RectF(x, y, x + cap.width, faceBottom), radius,
             ColorFromHex(cap.active ? (light ? 0xf1ecd9 : 0x414036) : (light ? 0xe9ecf1 : 0x2e3139), opacity));
        if (::KeycastConfig.borderThickness > 0.1f)
        {
            const float thickness = ::KeycastConfig.borderThickness;
            brush->SetColor(ColorFromHex(cap.active ? (light ? 0xb4a77b : 0xa79b6b) : (light ? 0xbfc5ce : 0x4a4e59), cap.active ? 0.65f : 1.0f));
            target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + thickness / 2, y + thickness / 2, x + cap.width - thickness / 2, faceBottom - thickness / 2), radius, radius), brush, thickness);
        }
        auto ink = ColorFromHex(cap.active ? (light ? 0x827346 : 0xd5c78e) : (light ? 0x414752 : 0xc3c7cf));
        if (::KeycastConfig.rainbowText && !cap.active)
            ink = colors[cap.group % colors.size()];
        brush->SetColor(ink);
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
    const auto renderTargetProps = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
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
    if (!IsWindowVisible(hwnd))
    {
        ValidateRect(hwnd, nullptr);
        return;
    }

    EnsureCache();
    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    g_layout.Draw(pRenderTarget, pBrush, D2D1::Point2F(0.0f, 0.0f));

    HRESULT hr = pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        InvalidateOverlayCache();
        InitD2DRenderTarget(hwnd);
    }
}

void GetOverlayContentDipSize(float &width, float &height)
{
    EnsureCache();
    width = (std::max)(32.0f, g_layout.Width());
    height = (std::max)(32.0f, g_layout.Height());
}

void RequestOverlayRepaint()
{
    if (!::D2DHwnd)
    {
        return;
    }
    UpdateOverlayWindowPlacement(::D2DHwnd);
    InvalidateRect(::D2DHwnd, nullptr, FALSE);
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
