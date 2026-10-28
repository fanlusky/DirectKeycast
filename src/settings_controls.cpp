#include "settings_controls.h"
#include "d2d.h"
#include "globals.h"
#include "theme.h"
#include "utils.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Scene.h"
#include "msimeui/Window.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using Microsoft::WRL::ComPtr;
using msimeui::PointF;
using msimeui::RectF;
using msimeui::SizeF;

namespace
{
constexpr float kUnboundedWidth = 100000.0f;
constexpr float kControlHeight = 30.0f;
constexpr float kControlFontSize = 14.0f;
constexpr size_t kNone = static_cast<size_t>(-1);

struct FormatCacheEntry
{
    std::wstring family;
    float size = 0.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    ComPtr<IDWriteTextFormat> format;
};

IDWriteTextFormat *GetFormat(const std::wstring &family, float size, DWRITE_FONT_WEIGHT weight)
{
    static std::vector<FormatCacheEntry> cache;
    for (const auto &entry : cache)
    {
        if (entry.size == size && entry.weight == weight && entry.family == family)
        {
            return entry.format.Get();
        }
    }
    if (!::pDWriteFactory)
    {
        return nullptr;
    }
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(::pDWriteFactory->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-cn", format.GetAddressOf())))
    {
        return nullptr;
    }
    cache.push_back({family, size, weight, format});
    return format.Get();
}

ComPtr<IDWriteTextLayout> CreateLayout(const std::wstring &text, const std::wstring &family, float size, DWRITE_FONT_WEIGHT weight, float maxWidth, bool wrap)
{
    ComPtr<IDWriteTextLayout> layout;
    IDWriteTextFormat *format = GetFormat(family, size, weight);
    if (!format || !::pDWriteFactory)
    {
        return layout;
    }
    ::pDWriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, (std::max)(maxWidth, 1.0f), kUnboundedWidth, layout.GetAddressOf());
    if (layout)
    {
        layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return layout;
}

SizeF LayoutSize(IDWriteTextLayout *layout)
{
    if (!layout)
    {
        return {};
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

float TextWidth(const std::wstring &text, const std::wstring &family, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL)
{
    const auto layout = CreateLayout(text, family, size, weight, kUnboundedWidth, false);
    return std::ceil(LayoutSize(layout.Get()).width);
}

D2D1_RECT_F ToD2D(const RectF &rect)
{
    return D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
}

RectF Inflate(const RectF &rect, float amount)
{
    return {rect.x - amount, rect.y - amount, rect.width + amount * 2.0f, rect.height + amount * 2.0f};
}

bool Contains(const RectF &rect, const PointF &point)
{
    return point.x >= rect.x && point.x < rect.x + rect.width && point.y >= rect.y && point.y < rect.y + rect.height;
}

void FillRounded(msimeui::DeviceResources &dr, const RectF &rect, float radius, const D2D1_COLOR_F &color)
{
    ID2D1HwndRenderTarget *target = dr.GetRenderTarget();
    ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(color);
    if (target && brush && color.a > 0.0f)
    {
        target->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(rect), radius, radius), brush);
    }
}

void StrokeRounded(msimeui::DeviceResources &dr, const RectF &rect, float radius, const D2D1_COLOR_F &color, float width)
{
    ID2D1HwndRenderTarget *target = dr.GetRenderTarget();
    ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(color);
    if (target && brush && color.a > 0.0f)
    {
        const float inset = width * 0.5f;
        const RectF inner = {rect.x + inset, rect.y + inset, rect.width - width, rect.height - width};
        target->DrawRoundedRectangle(D2D1::RoundedRect(ToD2D(inner), radius, radius), brush, width);
    }
}

void DrawLayout(msimeui::DeviceResources &dr, IDWriteTextLayout *layout, float x, float y, const D2D1_COLOR_F &color)
{
    ID2D1HwndRenderTarget *target = dr.GetRenderTarget();
    ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(color);
    if (target && brush && layout)
    {
        target->DrawTextLayout(D2D1::Point2F(x, y), layout, brush);
    }
}

// Draws single-line text vertically centered in `rect`, clipped to it.
void DrawTextInRect(msimeui::DeviceResources &dr, const std::wstring &text, const std::wstring &family, float size, DWRITE_FONT_WEIGHT weight, const RectF &rect, const D2D1_COLOR_F &color, bool center = false)
{
    ID2D1HwndRenderTarget *target = dr.GetRenderTarget();
    const auto layout = CreateLayout(text, family, size, weight, kUnboundedWidth, false);
    if (!target || !layout)
    {
        return;
    }
    const SizeF textSize = LayoutSize(layout.Get());
    const float x = center ? rect.x + (rect.width - textSize.width) * 0.5f : rect.x;
    const float y = rect.y + (rect.height - textSize.height) * 0.5f;
    target->PushAxisAlignedClip(ToD2D(rect), D2D1_ANTIALIAS_MODE_ALIASED);
    DrawLayout(dr, layout.Get(), x, y, color);
    target->PopAxisAlignedClip();
}

D2D1_COLOR_F RoleColor(dkui::TextRole role)
{
    const ThemeColors &theme = GetTheme();
    switch (role)
    {
    case dkui::TextRole::Secondary:
        return theme.textSecondary;
    case dkui::TextRole::Muted:
        return theme.textMuted;
    case dkui::TextRole::Sidebar:
        return theme.sidebarItem;
    case dkui::TextRole::Primary:
    default:
        return theme.text;
    }
}

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha)
{
    color.a *= alpha;
    return color;
}
} // namespace

namespace dkui
{
//
// Label
//
Label::Label(std::wstring text, float fontSize, DWRITE_FONT_WEIGHT weight, TextRole role) : text_(std::move(text)), fontSize_(fontSize), weight_(weight), role_(role)
{
}

void Label::SetText(std::wstring text)
{
    if (text == text_)
    {
        return;
    }
    text_ = std::move(text);
    layout_.Reset();
    InvalidateMeasure();
    InvalidateVisual();
}

void Label::SetWrap(bool wrap)
{
    wrap_ = wrap;
    layout_.Reset();
    InvalidateMeasure();
}

void Label::SetFontFamily(std::wstring family)
{
    family_ = std::move(family);
    layout_.Reset();
    InvalidateMeasure();
}

IDWriteTextLayout *Label::EnsureLayout(float maxWidth)
{
    const float width = wrap_ ? (std::max)(maxWidth, 1.0f) : kUnboundedWidth;
    if (!layout_ || layoutWidth_ != width)
    {
        layout_ = CreateLayout(text_, family_.empty() ? GetUiFontFamily() : family_, fontSize_, weight_, width, wrap_);
        layoutWidth_ = width;
    }
    return layout_.Get();
}

SizeF Label::Measure(const SizeF &availableSize)
{
    const SizeF size = LayoutSize(EnsureLayout(availableSize.width));
    return {(std::min)(std::ceil(size.width), availableSize.width), std::ceil(size.height)};
}

void Label::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void Label::Render(msimeui::DeviceResources &deviceResources)
{
    ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
    IDWriteTextLayout *layout = EnsureLayout(bounds_.width);
    if (!target || !layout)
    {
        return;
    }
    const float height = LayoutSize(layout).height;
    const float y = bounds_.y + (std::max)(0.0f, (bounds_.height - height) * 0.5f);
    target->PushAxisAlignedClip(ToD2D(bounds_), D2D1_ANTIALIAS_MODE_ALIASED);
    DrawLayout(deviceResources, layout, bounds_.x, y, RoleColor(role_));
    target->PopAxisAlignedClip();
}

//
// CenteredColumn
//
CenteredColumn::CenteredColumn(std::shared_ptr<msimeui::Visual> child, float maxWidth) : msimeui::Container(std::move(child)), maxWidth_(maxWidth)
{
}

SizeF CenteredColumn::Measure(const SizeF &availableSize)
{
    const float innerWidth = (std::max)(0.0f, (std::min)(availableSize.width - padding_.left - padding_.right, maxWidth_));
    const float innerHeight = (std::max)(0.0f, availableSize.height - padding_.top - padding_.bottom);
    const SizeF childSize = child_ ? child_->MeasureInLayout({innerWidth, innerHeight}) : SizeF{};
    return {availableSize.width, childSize.height + padding_.top + padding_.bottom};
}

void CenteredColumn::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
    if (!child_)
    {
        return;
    }
    const float available = (std::max)(0.0f, finalRect.width - padding_.left - padding_.right);
    const float width = (std::min)(available, maxWidth_);
    const float x = finalRect.x + padding_.left + (available - width) * 0.5f;
    child_->ArrangeInLayout({x, finalRect.y + padding_.top, width, (std::max)(0.0f, finalRect.height - padding_.top - padding_.bottom)});
}

//
// Section
//
Section::Section(std::shared_ptr<msimeui::Visual> child) : msimeui::Container(std::move(child))
{
    SetPadding({24.0f, 20.0f, 24.0f, 20.0f});
}

void Section::Render(msimeui::DeviceResources &deviceResources)
{
    const ThemeColors &theme = GetTheme();
    constexpr float kRadius = 8.0f;
    // --card-shadow approximated with a 1px offset underlay.
    FillRounded(deviceResources, {bounds_.x, bounds_.y + 1.0f, bounds_.width, bounds_.height}, kRadius, D2D1::ColorF(0, 0, 0, IsLightThemeResolved() ? 0.05f : 0.18f));
    FillRounded(deviceResources, bounds_, kRadius, theme.cardBg);
    StrokeRounded(deviceResources, bounds_, kRadius, theme.border, 1.0f);
    msimeui::Container::Render(deviceResources);
}

//
// Divider
//
Divider::Divider()
{
    SetMargin({0.0f, 12.0f, 0.0f, 12.0f});
}

SizeF Divider::Measure(const SizeF &availableSize)
{
    return {availableSize.width, 1.0f};
}

void Divider::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void Divider::Render(msimeui::DeviceResources &deviceResources)
{
    ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
    ID2D1SolidColorBrush *brush = deviceResources.GetSolidColorBrush(GetTheme().divider);
    if (target && brush)
    {
        target->FillRectangle(D2D1::RectF(bounds_.x, bounds_.y, bounds_.x + bounds_.width, bounds_.y + 1.0f), brush);
    }
}

//
// SettingRow
//
SettingRow::SettingRow(std::wstring title, std::wstring description, std::shared_ptr<msimeui::Visual> control) : control_(std::move(control))
{
    auto stack = std::make_shared<msimeui::StackPanel>(0.0f);
    stack->AddChild(std::make_shared<Label>(std::move(title), 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    if (!description.empty())
    {
        auto desc = std::make_shared<Label>(std::move(description), 12.0f, DWRITE_FONT_WEIGHT_NORMAL, TextRole::Muted);
        desc->SetWrap(true);
        desc->SetMargin({0.0f, 4.0f, 0.0f, 0.0f});
        stack->AddChild(desc);
    }
    left_ = stack;
    AddChild(left_);
    if (control_)
    {
        AddChild(control_);
    }
}

SizeF SettingRow::Measure(const SizeF &availableSize)
{
    controlSize_ = control_ ? control_->MeasureInLayout(availableSize) : SizeF{};
    const float gap = control_ ? 16.0f : 0.0f;
    leftSize_ = left_->MeasureInLayout({(std::max)(0.0f, availableSize.width - controlSize_.width - gap), availableSize.height});
    return {availableSize.width, (std::max)({leftSize_.height, controlSize_.height, kControlHeight})};
}

void SettingRow::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
    const float gap = control_ ? 16.0f : 0.0f;
    if (control_)
    {
        control_->ArrangeInLayout({finalRect.x + finalRect.width - controlSize_.width, finalRect.y + (finalRect.height - controlSize_.height) * 0.5f, controlSize_.width, controlSize_.height});
    }
    const float leftWidth = (std::max)(0.0f, finalRect.width - controlSize_.width - gap);
    left_->ArrangeInLayout({finalRect.x, finalRect.y + (finalRect.height - leftSize_.height) * 0.5f, leftWidth, leftSize_.height});
}

void SettingRow::Render(msimeui::DeviceResources &deviceResources)
{
    for (const auto &child : children_)
    {
        child->Render(deviceResources);
    }
}

//
// ToggleSwitch — `.ftb-toggle-btn`
//
ToggleSwitch::ToggleSwitch(bool on) : on_(on)
{
}

void ToggleSwitch::SetOnChanged(ChangeHandler handler)
{
    onChanged_ = std::move(handler);
}

void ToggleSwitch::SetChecked(bool on)
{
    if (on_ != on)
    {
        on_ = on;
        InvalidateVisual();
    }
}

bool ToggleSwitch::IsChecked() const
{
    return on_;
}

SizeF ToggleSwitch::Measure(const SizeF &)
{
    return {38.0f, 19.0f};
}

void ToggleSwitch::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void ToggleSwitch::Render(msimeui::DeviceResources &deviceResources)
{
    const ThemeColors &theme = GetTheme();
    const RectF track = {bounds_.x, bounds_.y + (bounds_.height - 19.0f) * 0.5f, 38.0f, 19.0f};
    FillRounded(deviceResources, track, 9.5f, on_ ? theme.toggleOn : theme.toggleOff);

    ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
    ID2D1SolidColorBrush *shadow = deviceResources.GetSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.25f));
    ID2D1SolidColorBrush *knob = deviceResources.GetSolidColorBrush(theme.knob);
    if (!target || !shadow || !knob)
    {
        return;
    }
    const float cx = track.x + 1.0f + 8.0f + (on_ ? 20.0f : 0.0f);
    const float cy = track.y + track.height * 0.5f;
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 1.0f), 8.0f, 8.0f), shadow);
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8.0f, 8.0f), knob);
}

bool ToggleSwitch::IsFocusable() const
{
    return true;
}

bool ToggleSwitch::OnMouseDown(const POINT &point, WPARAM)
{
    pressed_ = window_ && HitTest(window_->ClientPixelsToDips(point));
    return pressed_;
}

bool ToggleSwitch::OnMouseUp(const POINT &point, WPARAM)
{
    if (!pressed_ || !window_)
    {
        return false;
    }
    pressed_ = false;
    if (HitTest(window_->ClientPixelsToDips(point)))
    {
        Toggle();
    }
    return true;
}

bool ToggleSwitch::OnKeyDown(WPARAM key, LPARAM)
{
    if (key == VK_SPACE || key == VK_RETURN)
    {
        Toggle();
        return true;
    }
    return false;
}

void ToggleSwitch::Toggle()
{
    on_ = !on_;
    InvalidateVisual();
    if (onChanged_)
    {
        onChanged_(on_);
    }
}

//
// DropdownMenu — `.dropdown-menu.compact-scroll-menu`
//
class DropdownMenu : public msimeui::Visual
{
  public:
    static constexpr float kItemHeight = 28.0f;
    static constexpr float kItemGap = 2.0f;
    static constexpr float kInset = 2.0f;
    static constexpr size_t kMaxRows = 6;

    explicit DropdownMenu(const std::vector<std::wstring> *items) : items_(items)
    {
    }

    void SetOnSelect(std::function<void(size_t)> handler)
    {
        onSelect_ = std::move(handler);
    }

    void SetOnDismiss(std::function<void()> handler)
    {
        onDismiss_ = std::move(handler);
    }

    void Prepare(size_t selected)
    {
        highlighted_ = selected;
        hovered_ = kNone;
        pressed_ = kNone;
        scroll_ = 0.0f;
        ScrollIntoView(selected);
    }

    float PreferredWidth()
    {
        if (preferredWidth_ < 0.0f)
        {
            float widest = 0.0f;
            for (const auto &item : *items_)
            {
                widest = (std::max)(widest, TextWidth(item, GetUiFontFamily(), kControlFontSize));
            }
            preferredWidth_ = widest + 8.0f * 2.0f + kInset * 2.0f + 6.0f;
        }
        return preferredWidth_;
    }

    SizeF Measure(const SizeF &) override
    {
        const size_t rows = (std::min)(items_->size(), kMaxRows);
        return {PreferredWidth(), HeightForRows(rows)};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
        ClampScroll();
    }

    void Render(msimeui::DeviceResources &deviceResources) override
    {
        ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
        if (!target)
        {
            return;
        }
        const ThemeColors &theme = GetTheme();
        target->PushAxisAlignedClip(ToD2D(bounds_), D2D1_ANTIALIAS_MODE_ALIASED);
        const size_t marked = hovered_ != kNone ? hovered_ : highlighted_;
        for (size_t i = 0; i < items_->size(); ++i)
        {
            const RectF item = ItemRect(i);
            if (item.y + item.height < bounds_.y || item.y > bounds_.y + bounds_.height)
            {
                continue;
            }
            if (i == marked)
            {
                FillRounded(deviceResources, item, 8.0f, theme.dropdownItemHover);
            }
            DrawTextInRect(deviceResources, (*items_)[i], GetUiFontFamily(), kControlFontSize, DWRITE_FONT_WEIGHT_NORMAL, {item.x + 8.0f, item.y, item.width - 16.0f, item.height}, theme.text);
        }
        target->PopAxisAlignedClip();

        const float overflow = ContentHeight() - bounds_.height;
        if (overflow > 0.5f)
        {
            const float trackHeight = bounds_.height - 8.0f;
            const float thumbHeight = (std::max)(24.0f, trackHeight * bounds_.height / ContentHeight());
            const float thumbY = bounds_.y + 4.0f + (trackHeight - thumbHeight) * (scroll_ / overflow);
            FillRounded(deviceResources, {bounds_.x + bounds_.width - 6.0f, thumbY, 4.0f, thumbHeight}, 2.0f, theme.scrollbarThumb);
        }
    }

    bool IsFocusable() const override
    {
        return true;
    }

    bool OnMouseMove(const POINT &point, WPARAM) override
    {
        if (!window_)
        {
            return false;
        }
        const size_t index = HitTestItem(window_->ClientPixelsToDips(point));
        if (index != hovered_)
        {
            hovered_ = index;
            InvalidateVisual();
        }
        return true;
    }

    void OnMouseLeave() override
    {
        if (hovered_ != kNone)
        {
            hovered_ = kNone;
            InvalidateVisual();
        }
    }

    bool OnMouseDown(const POINT &point, WPARAM) override
    {
        pressed_ = window_ ? HitTestItem(window_->ClientPixelsToDips(point)) : kNone;
        return true;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override
    {
        const size_t index = window_ ? HitTestItem(window_->ClientPixelsToDips(point)) : kNone;
        const bool clicked = index != kNone && index == pressed_;
        pressed_ = kNone;
        if (clicked && onSelect_)
        {
            onSelect_(index);
        }
        return true;
    }

    bool OnMouseWheel(const POINT &, short delta, WPARAM) override
    {
        scroll_ -= static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA) * (kItemHeight + kItemGap) * 2.0f;
        ClampScroll();
        InvalidateVisual();
        return true;
    }

    bool OnKeyDown(WPARAM key, LPARAM) override
    {
        const size_t count = items_->size();
        if (count == 0)
        {
            return false;
        }
        size_t next = highlighted_ < count ? highlighted_ : 0;
        switch (key)
        {
        case VK_UP:
            next = next > 0 ? next - 1 : 0;
            break;
        case VK_DOWN:
            next = (std::min)(next + 1, count - 1);
            break;
        case VK_PRIOR:
            next = next > kMaxRows ? next - kMaxRows : 0;
            break;
        case VK_NEXT:
            next = (std::min)(next + kMaxRows, count - 1);
            break;
        case VK_HOME:
            next = 0;
            break;
        case VK_END:
            next = count - 1;
            break;
        case VK_RETURN:
        case VK_SPACE:
            if (onSelect_ && highlighted_ < count)
            {
                onSelect_(highlighted_);
            }
            return true;
        case VK_ESCAPE:
            if (onDismiss_)
            {
                onDismiss_();
            }
            return true;
        default:
            return false;
        }
        highlighted_ = next;
        hovered_ = kNone;
        ScrollIntoView(next);
        InvalidateVisual();
        return true;
    }

  private:
    static float HeightForRows(size_t rows)
    {
        if (rows == 0)
        {
            return kInset * 2.0f;
        }
        return kInset * 2.0f + static_cast<float>(rows) * kItemHeight + static_cast<float>(rows - 1) * kItemGap;
    }

    float ContentHeight() const
    {
        return HeightForRows(items_->size());
    }

    RectF ItemRect(size_t index) const
    {
        return {bounds_.x + kInset, bounds_.y + kInset + static_cast<float>(index) * (kItemHeight + kItemGap) - scroll_, bounds_.width - kInset * 2.0f, kItemHeight};
    }

    size_t HitTestItem(const PointF &point) const
    {
        if (!Contains(bounds_, point))
        {
            return kNone;
        }
        for (size_t i = 0; i < items_->size(); ++i)
        {
            if (Contains(ItemRect(i), point))
            {
                return i;
            }
        }
        return kNone;
    }

    void ClampScroll()
    {
        const float visible = bounds_.height > 0.0f ? bounds_.height : HeightForRows((std::min)(items_->size(), kMaxRows));
        scroll_ = (std::clamp)(scroll_, 0.0f, (std::max)(0.0f, ContentHeight() - visible));
    }

    void ScrollIntoView(size_t index)
    {
        if (index >= items_->size())
        {
            return;
        }
        const float visible = HeightForRows((std::min)(items_->size(), kMaxRows));
        const float top = kInset + static_cast<float>(index) * (kItemHeight + kItemGap);
        if (top - kInset < scroll_)
        {
            scroll_ = top - kInset;
        }
        else if (top + kItemHeight + kInset > scroll_ + visible)
        {
            scroll_ = top + kItemHeight + kInset - visible;
        }
        ClampScroll();
    }

    const std::vector<std::wstring> *items_ = nullptr;
    std::function<void(size_t)> onSelect_;
    std::function<void()> onDismiss_;
    size_t highlighted_ = kNone;
    size_t hovered_ = kNone;
    size_t pressed_ = kNone;
    float scroll_ = 0.0f;
    float preferredWidth_ = -1.0f;
};

// msimeui::Popup does not route the wheel to its content; the menu needs it to scroll.
class DropdownPopup : public msimeui::Popup
{
  public:
    explicit DropdownPopup(const std::shared_ptr<DropdownMenu> &menu) : msimeui::Popup(menu), menu_(menu.get())
    {
        SetPadding(0.0f);
        SetPlacement(msimeui::PopupPlacement::BelowLeading);
        SetCornerRadius(10.0f);
        SetShadowEnabled(true);
        SetConstrainToViewport(true);
    }

    bool OnMouseWheel(const POINT &point, short delta, WPARAM keyState) override
    {
        if (!window_ || !HitTest(window_->ClientPixelsToDips(point)))
        {
            return false;
        }
        menu_->OnMouseWheel(point, delta, keyState);
        return true;
    }

  private:
    DropdownMenu *menu_ = nullptr;
};

//
// Dropdown — `.dropdown-toggle`
//
Dropdown::Dropdown(std::vector<std::wstring> items, size_t selected) : items_(std::move(items)), selected_(selected < items_.size() ? selected : 0)
{
    menu_ = std::make_shared<DropdownMenu>(&items_);
    menu_->SetOnSelect([this](size_t index) { Commit(index); });
    menu_->SetOnDismiss([this]() { Close(); });
    popup_ = std::make_shared<DropdownPopup>(menu_);
}

void Dropdown::SetOnChanged(ChangeHandler handler)
{
    onChanged_ = std::move(handler);
}

void Dropdown::SetSelectedIndex(size_t index)
{
    if (index < items_.size() && index != selected_)
    {
        selected_ = index;
        InvalidateVisual();
    }
}

size_t Dropdown::GetSelectedIndex() const
{
    return selected_;
}

SizeF Dropdown::Measure(const SizeF &availableSize)
{
    if (maxTextWidth_ < 0.0f)
    {
        maxTextWidth_ = 0.0f;
        for (const auto &item : items_)
        {
            maxTextWidth_ = (std::max)(maxTextWidth_, TextWidth(item, GetUiFontFamily(), kControlFontSize));
        }
    }
    // padding: 4px 6px 4px 12px, 18px arrow, 8px breathing room between text and arrow
    const float width = (std::max)(100.0f, maxTextWidth_ + 12.0f + 8.0f + 18.0f + 6.0f);
    return {(std::min)(width, availableSize.width), kControlHeight};
}

void Dropdown::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
    popup_->SetAnchorRect(bounds_);
}

void Dropdown::Render(msimeui::DeviceResources &deviceResources)
{
    const ThemeColors &theme = GetTheme();
    const bool raised = hovered_ || pressed_ || open_;
    if (open_)
    {
        FillRounded(deviceResources, Inflate(bounds_, 2.0f), 10.0f, theme.accentSoft);
    }
    FillRounded(deviceResources, bounds_, 8.0f, raised ? theme.surfaceRaised : theme.dropdownBg);
    StrokeRounded(deviceResources, bounds_, 8.0f, open_ ? theme.accentSoftBorder : (hovered_ ? theme.borderStrong : theme.border), 1.0f);

    const std::wstring &text = selected_ < items_.size() ? items_[selected_] : std::wstring();
    DrawTextInRect(deviceResources, text, GetUiFontFamily(), kControlFontSize, DWRITE_FONT_WEIGHT_NORMAL, {bounds_.x + 12.0f, bounds_.y, (std::max)(0.0f, bounds_.width - 12.0f - 18.0f - 6.0f - 4.0f), bounds_.height}, theme.text);

    const float arrowX = bounds_.x + bounds_.width - 6.0f - 18.0f;
    const float arrowY = bounds_.y + (bounds_.height - 18.0f) * 0.5f;
    DrawSvgIcon(deviceResources.GetRenderTarget(), deviceResources.GetSolidColorBrush(theme.text), SvgIcon::Arrow, D2D1::RectF(arrowX, arrowY, arrowX + 18.0f, arrowY + 18.0f));
}

void Dropdown::Attach(msimeui::Window *window)
{
    msimeui::Visual::Attach(window);
    popup_->Attach(window);
}

bool Dropdown::HitTest(const PointF &point) const
{
    return Contains(bounds_, point);
}

bool Dropdown::IsFocusable() const
{
    return true;
}

bool Dropdown::OnMouseDown(const POINT &point, WPARAM)
{
    pressed_ = window_ && HitTest(window_->ClientPixelsToDips(point));
    InvalidateVisual();
    return pressed_;
}

bool Dropdown::OnMouseUp(const POINT &point, WPARAM)
{
    if (!pressed_ || !window_)
    {
        return false;
    }
    pressed_ = false;
    if (HitTest(window_->ClientPixelsToDips(point)))
    {
        if (open_)
        {
            Close();
        }
        else
        {
            Open();
        }
    }
    InvalidateVisual();
    return true;
}

void Dropdown::OnMouseEnter()
{
    hovered_ = true;
    InvalidateVisual();
}

void Dropdown::OnMouseLeave()
{
    hovered_ = false;
    pressed_ = false;
    InvalidateVisual();
}

bool Dropdown::OnKeyDown(WPARAM key, LPARAM)
{
    if (key == VK_SPACE || key == VK_RETURN || key == VK_DOWN)
    {
        Open();
        return true;
    }
    return false;
}

bool Dropdown::KeepsPopupsOpenOnClick() const
{
    return true;
}

void Dropdown::Open()
{
    msimeui::Scene *scene = window_ ? window_->GetScene() : nullptr;
    if (open_ || !scene || items_.empty())
    {
        return;
    }
    const ThemeColors &theme = GetTheme();
    popup_->SetBackgroundFill(theme.dropdownMenuBg);
    popup_->SetBorderColor(theme.dropdownMenuBorder);
    menu_->Prepare(selected_);

    // `.dropdown-menu { right: 0 }`: a menu wider than the toggle grows to the left.
    const float menuWidth = menu_->PreferredWidth();
    popup_->SetAnchorRect(bounds_);
    popup_->SetMatchAnchorWidth(true);
    popup_->SetOffset(menuWidth > bounds_.width ? bounds_.width - menuWidth : 0.0f, 2.0f);

    scene->AddPopup(popup_, [this]() {
        open_ = false;
        InvalidateVisual();
    });
    open_ = true;
    window_->FocusVisual(menu_.get());
    window_->Invalidate();
}

void Dropdown::Close()
{
    if (!open_)
    {
        return;
    }
    if (msimeui::Scene *scene = window_ ? window_->GetScene() : nullptr)
    {
        scene->RemovePopup(popup_.get(), false);
    }
    open_ = false;
    if (window_)
    {
        window_->FocusVisual(this);
        window_->Invalidate();
    }
}

void Dropdown::Commit(size_t index)
{
    Close();
    if (index >= items_.size() || index == selected_)
    {
        return;
    }
    selected_ = index;
    InvalidateVisual();
    if (onChanged_)
    {
        onChanged_(index);
    }
}

//
// ActionButton
//
ActionButton::ActionButton(std::wstring text, Style style) : text_(std::move(text)), style_(style)
{
}

void ActionButton::SetOnClick(ClickHandler handler)
{
    onClick_ = std::move(handler);
}

SizeF ActionButton::Measure(const SizeF &availableSize)
{
    float width = TextWidth(text_, GetUiFontFamily(), kControlFontSize) + 24.0f;
    if (style_ == Style::Secondary)
    {
        width = (std::max)(width, 88.0f);
    }
    return {(std::min)(width, availableSize.width), kControlHeight};
}

void ActionButton::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void ActionButton::Render(msimeui::DeviceResources &deviceResources)
{
    const ThemeColors &theme = GetTheme();
    if (style_ == Style::Soft)
    {
        FillRounded(deviceResources, bounds_, 6.0f, (hovered_ || pressed_) ? theme.accentSoftBorder : theme.accentSoft);
        StrokeRounded(deviceResources, bounds_, 6.0f, theme.accentSoftBorder, 1.0f);
        DrawTextInRect(deviceResources, text_, GetUiFontFamily(), kControlFontSize, DWRITE_FONT_WEIGHT_NORMAL, bounds_, theme.text, true);
        return;
    }
    FillRounded(deviceResources, bounds_, 8.0f, (hovered_ || pressed_) ? theme.buttonSecondaryHover : theme.buttonSecondaryBg);
    StrokeRounded(deviceResources, bounds_, 8.0f, theme.border, 1.0f);
    DrawTextInRect(deviceResources, text_, GetUiFontFamily(), kControlFontSize, DWRITE_FONT_WEIGHT_NORMAL, bounds_, theme.buttonSecondaryText, true);
}

bool ActionButton::IsFocusable() const
{
    return true;
}

bool ActionButton::OnMouseDown(const POINT &point, WPARAM)
{
    pressed_ = window_ && HitTest(window_->ClientPixelsToDips(point));
    InvalidateVisual();
    return pressed_;
}

bool ActionButton::OnMouseUp(const POINT &point, WPARAM)
{
    if (!pressed_ || !window_)
    {
        return false;
    }
    pressed_ = false;
    InvalidateVisual();
    if (HitTest(window_->ClientPixelsToDips(point)) && onClick_)
    {
        onClick_();
    }
    return true;
}

void ActionButton::OnMouseEnter()
{
    hovered_ = true;
    InvalidateVisual();
}

void ActionButton::OnMouseLeave()
{
    hovered_ = false;
    pressed_ = false;
    InvalidateVisual();
}

//
// KeyCaps
//
namespace
{
constexpr float kCapHeight = 25.0f;
constexpr float kCapFontSize = 12.0f;
constexpr float kPlusFontSize = 11.0f;
constexpr float kCapGap = 5.0f;
const wchar_t *const kCapFont = L"Segoe UI";
} // namespace

KeyCaps::KeyCaps(std::vector<std::wstring> keys) : keys_(std::move(keys))
{
}

SizeF KeyCaps::Measure(const SizeF &availableSize)
{
    capWidths_.clear();
    float width = 0.0f;
    plusWidth_ = TextWidth(L"+", GetUiFontFamily(), kPlusFontSize) + 6.0f;
    for (size_t i = 0; i < keys_.size(); ++i)
    {
        const float cap = (std::max)(28.0f, TextWidth(keys_[i], kCapFont, kCapFontSize) + 16.0f);
        capWidths_.push_back(cap);
        width += cap;
        if (i + 1 < keys_.size())
        {
            width += plusWidth_ + kCapGap * 2.0f;
        }
    }
    return {(std::min)(width, availableSize.width), kCapHeight};
}

void KeyCaps::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void KeyCaps::Render(msimeui::DeviceResources &deviceResources)
{
    const ThemeColors &theme = GetTheme();
    float x = bounds_.x;
    const float y = bounds_.y + (bounds_.height - kCapHeight) * 0.5f;
    for (size_t i = 0; i < keys_.size() && i < capWidths_.size(); ++i)
    {
        const RectF cap = {x, y, capWidths_[i], kCapHeight};
        // border-bottom is a shade darker than the other three sides
        FillRounded(deviceResources, cap, 5.0f, theme.kbdBorderBottom);
        FillRounded(deviceResources, {cap.x, cap.y, cap.width, cap.height - 1.0f}, 5.0f, theme.kbdBorder);
        FillRounded(deviceResources, {cap.x + 1.0f, cap.y + 1.0f, cap.width - 2.0f, cap.height - 2.5f}, 4.0f, theme.kbdBg);
        DrawTextInRect(deviceResources, keys_[i], kCapFont, kCapFontSize, DWRITE_FONT_WEIGHT_NORMAL, {cap.x, cap.y, cap.width, cap.height - 1.0f}, theme.kbdText, true);
        x += cap.width;
        if (i + 1 < keys_.size())
        {
            x += kCapGap;
            DrawTextInRect(deviceResources, L"+", GetUiFontFamily(), kPlusFontSize, DWRITE_FONT_WEIGHT_NORMAL, {x, y, plusWidth_, kCapHeight}, theme.textMuted, true);
            x += plusWidth_ + kCapGap;
        }
    }
}

//
// NavItem — `.sidebar .item`
//
NavItem::NavItem(SvgIcon icon, std::wstring text) : icon_(icon), text_(std::move(text))
{
    SetMargin({8.0f, 2.0f, 8.0f, 2.0f});
}

void NavItem::SetSelected(bool selected)
{
    if (selected_ == selected)
    {
        return;
    }
    selected_ = selected;
    // The indicator is drawn outside our bounds, so repaint everything.
    if (window_)
    {
        window_->Invalidate();
    }
}

void NavItem::SetOnClick(ClickHandler handler)
{
    onClick_ = std::move(handler);
}

SizeF NavItem::Measure(const SizeF &availableSize)
{
    return {availableSize.width, 36.0f};
}

void NavItem::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void NavItem::Render(msimeui::DeviceResources &deviceResources)
{
    const ThemeColors &theme = GetTheme();
    if (hovered_ || pressed_)
    {
        FillRounded(deviceResources, bounds_, 5.0f, theme.sidebarHover);
    }
    else if (selected_)
    {
        FillRounded(deviceResources, bounds_, 5.0f, theme.sidebarActive);
    }

    const bool emphasized = selected_ || hovered_;
    const float iconX = bounds_.x + 12.0f + 1.0f;
    const float iconY = bounds_.y + (bounds_.height - 22.0f) * 0.5f + 1.0f;
    DrawSvgIcon(deviceResources.GetRenderTarget(), deviceResources.GetSolidColorBrush(WithAlpha(theme.sidebarIcon, emphasized ? 1.0f : 0.9f)), icon_, D2D1::RectF(iconX, iconY, iconX + 22.0f, iconY + 22.0f));

    const float textX = bounds_.x + 12.0f + 24.0f + 15.0f;
    DrawTextInRect(deviceResources, text_, GetUiFontFamily(), 15.0f, DWRITE_FONT_WEIGHT_NORMAL, {textX, bounds_.y, (std::max)(0.0f, bounds_.x + bounds_.width - textX - 8.0f), bounds_.height}, emphasized ? theme.text : theme.sidebarItem);
}

bool NavItem::IsFocusable() const
{
    return true;
}

bool NavItem::OnMouseDown(const POINT &point, WPARAM)
{
    pressed_ = window_ && HitTest(window_->ClientPixelsToDips(point));
    return pressed_;
}

bool NavItem::OnMouseUp(const POINT &point, WPARAM)
{
    if (!pressed_ || !window_)
    {
        return false;
    }
    pressed_ = false;
    InvalidateVisual();
    if (HitTest(window_->ClientPixelsToDips(point)) && onClick_)
    {
        onClick_();
    }
    return true;
}

void NavItem::OnMouseEnter()
{
    hovered_ = true;
    InvalidateVisual();
}

void NavItem::OnMouseLeave()
{
    hovered_ = false;
    pressed_ = false;
    InvalidateVisual();
}

//
// Sidebar — shared sliding active indicator, matching the IME settings window.
//
namespace
{

double SidebarNowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// CSS cubic-bezier(0.22, 1, 0.36, 1): solve x first, then sample y.
float SidebarEase(float progress)
{
    float low = 0.0f, high = 1.0f;
    for (int i = 0; i < 18; ++i)
    {
        const float t = (low + high) * 0.5f;
        const float inverse = 1.0f - t;
        const float x = 3.0f * inverse * inverse * t * 0.22f + 3.0f * inverse * t * t * 0.36f + t * t * t;
        if (x < progress)
            low = t;
        else
            high = t;
    }
    const float inverse = 1.0f - (low + high) * 0.5f;
    return 1.0f - inverse * inverse * inverse;
}
} // namespace

Sidebar::~Sidebar()
{
    StopAnimation();
}

void Sidebar::StopAnimation()
{
    if (frameTimer_)
    {
        // The callback only posts a paint, so waiting here cannot block on the UI thread.
        DeleteTimerQueueTimer(nullptr, frameTimer_, INVALID_HANDLE_VALUE);
        frameTimer_ = nullptr;
    }
    durationMs_ = 0.0;
    from_ = to_;
}

float Sidebar::TargetOffset() const
{
    const auto &item = selected_->GetBounds();
    return item.y - bounds_.y + (item.height - 20.0f) * 0.5f;
}

float Sidebar::Offset(double now) const
{
    if (durationMs_ <= 0.0 || now >= startedMs_ + durationMs_)
        return to_;
    const float progress = static_cast<float>(std::clamp((now - startedMs_) / durationMs_, 0.0, 1.0));
    return from_ + (to_ - from_) * SidebarEase(progress);
}

void Sidebar::Select(std::shared_ptr<NavItem> item)
{
    if (selected_ == item)
        return;
    const double now = SidebarNowMs();
    const float from = Offset(now);
    selected_ = std::move(item);
    if (!selected_)
    {
        StopAnimation();
        return;
    }
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    if (!positioned_ && selected_->GetBounds().height > 0.0f)
    {
        to_ = TargetOffset();
        StopAnimation();
        positioned_ = true;
    }
    else if (positioned_)
    {
        to_ = TargetOffset();
        if (animations && window_ && IsWindowVisible(window_->GetHandle()) && std::fabs(to_ - from) > 0.1f)
        {
            StopAnimation();
            from_ = from;
            startedMs_ = now;
            durationMs_ = std::round(std::clamp(110.0 + std::fabs(to_ - from) * 0.9, 140.0, 280.0));
            // Post explicit paint messages so frames also arrive while other window
            // messages are busy; the worker never accesses the control tree.
            if (!CreateTimerQueueTimer(&frameTimer_, nullptr, [](void *context, BOOLEAN) {
                    const HWND hwnd = static_cast<HWND>(context);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    PostMessageW(hwnd, WM_PAINT, 0, 0);
                }, window_->GetHandle(), 16, 16, WT_EXECUTEDEFAULT))
                StopAnimation();
        }
        else
            StopAnimation();
    }
    if (window_)
        window_->Invalidate();
}

void Sidebar::Arrange(const RectF &finalRect)
{
    msimeui::StackPanel::Arrange(finalRect);
    if (selected_ && (!positioned_ || std::fabs(TargetOffset() - to_) > 0.1f))
    {
        to_ = TargetOffset();
        StopAnimation();
        positioned_ = true;
    }
}

void Sidebar::Render(msimeui::DeviceResources &deviceResources)
{
    msimeui::StackPanel::Render(deviceResources);
    if (!positioned_ || !selected_)
        return;
    const double now = SidebarNowMs();
    const float y = bounds_.y + Offset(now);
    if (durationMs_ > 0.0 && now >= startedMs_ + durationMs_) StopAnimation();
    FillRounded(deviceResources, {selected_->GetBounds().x - 2.0f, y, 4.0f, 20.0f}, 2.0f, GetTheme().accent);
}

//
// WheelRoutingGrid
//
bool WheelRoutingGrid::OnMouseWheel(const POINT &point, short delta, WPARAM keyState)
{
    if (!window_)
    {
        return false;
    }
    for (msimeui::Visual *visual = FindVisualAt(window_->ClientPixelsToDips(point)); visual && visual != this; visual = visual->GetParentVisual())
    {
        if (visual->OnMouseWheel(point, delta, keyState))
        {
            return true;
        }
    }
    return false;
}

//
// ScrollViewer
//
namespace
{
constexpr float kScrollbarHitWidth = 12.0f;
constexpr float kScrollbarThumbWidth = 4.0f;
constexpr float kScrollbarInset = 2.0f;
constexpr float kScrollbarMinThumb = 36.0f;
constexpr float kWheelStep = 72.0f;
} // namespace

ScrollViewer::ScrollViewer(std::shared_ptr<msimeui::Visual> content) : content_(std::move(content))
{
    AdoptChild(content_);
}

SizeF ScrollViewer::Measure(const SizeF &availableSize)
{
    measuredContent_ = content_ ? content_->MeasureInLayout({availableSize.width, kUnboundedWidth}) : SizeF{};
    return availableSize;
}

void ScrollViewer::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
    offset_ = (std::clamp)(offset_, 0.0f, MaxOffset());
    UpdateContentLayout();
}

void ScrollViewer::Render(msimeui::DeviceResources &deviceResources)
{
    ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
    if (!target || !content_)
    {
        return;
    }
    target->PushAxisAlignedClip(ToD2D(bounds_), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    content_->Render(deviceResources);
    target->PopAxisAlignedClip();

    if (HasScrollbar())
    {
        const ThemeColors &theme = GetTheme();
        FillRounded(deviceResources, ThumbRect(), 1.0f, (hovered_ || dragging_) ? theme.scrollbarThumbHover : theme.scrollbarThumb);
    }
}

void ScrollViewer::Attach(msimeui::Window *window)
{
    Visual::Attach(window);
    if (content_)
    {
        content_->Attach(window);
    }
}

msimeui::Visual *ScrollViewer::FindVisualAt(const PointF &point)
{
    if (!HitTest(point))
    {
        return nullptr;
    }
    if (HasScrollbar() && Contains(TrackRect(), point))
    {
        return this;
    }
    if (content_)
    {
        if (msimeui::Visual *hit = content_->FindVisualAt(point))
        {
            return hit;
        }
    }
    return this;
}

msimeui::Visual *ScrollViewer::FindFocusableAt(const PointF &point)
{
    if (!HitTest(point) || (HasScrollbar() && Contains(TrackRect(), point)))
    {
        return nullptr;
    }
    return content_ ? content_->FindFocusableAt(point) : nullptr;
}

msimeui::Visual *ScrollViewer::FindFirstFocusableDescendant()
{
    return content_ ? content_->FindFirstFocusableDescendant() : nullptr;
}

bool ScrollViewer::OnMouseDown(const POINT &point, WPARAM)
{
    if (!window_ || !HasScrollbar())
    {
        return false;
    }
    const PointF dip = window_->ClientPixelsToDips(point);
    if (!Contains(TrackRect(), dip))
    {
        return false;
    }
    const RectF thumb = ThumbRect();
    if (dip.y < thumb.y || dip.y >= thumb.y + thumb.height)
    {
        // Clicking the track jumps there, then keeps dragging from the thumb's center.
        ScrollToThumbTop(dip.y - thumb.height * 0.5f);
    }
    dragging_ = true;
    dragOffsetY_ = dip.y - ThumbRect().y;
    InvalidateVisual();
    return true;
}

bool ScrollViewer::OnMouseUp(const POINT &, WPARAM)
{
    if (!dragging_)
    {
        return false;
    }
    dragging_ = false;
    InvalidateVisual();
    return true;
}

bool ScrollViewer::OnMouseMove(const POINT &point, WPARAM keyState)
{
    if (!window_ || !HasScrollbar())
    {
        return false;
    }
    const PointF dip = window_->ClientPixelsToDips(point);
    const bool hovered = Contains(TrackRect(), dip);
    if (hovered_ != hovered)
    {
        hovered_ = hovered;
        InvalidateVisual();
    }
    if (dragging_ && (keyState & MK_LBUTTON))
    {
        ScrollToThumbTop(dip.y - dragOffsetY_);
        return true;
    }
    return hovered;
}

void ScrollViewer::OnMouseLeave()
{
    if (hovered_)
    {
        hovered_ = false;
        InvalidateVisual();
    }
}

bool ScrollViewer::OnMouseWheel(const POINT &point, short delta, WPARAM)
{
    if (!window_ || !HitTest(window_->ClientPixelsToDips(point)) || MaxOffset() <= 0.0f)
    {
        return false;
    }
    ScrollTo(offset_ - static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA) * kWheelStep);
    return true;
}

float ScrollViewer::MaxOffset() const
{
    return (std::max)(0.0f, measuredContent_.height - bounds_.height);
}

bool ScrollViewer::HasScrollbar() const
{
    return MaxOffset() > 0.5f;
}

RectF ScrollViewer::TrackRect() const
{
    return {bounds_.x + bounds_.width - kScrollbarHitWidth, bounds_.y, kScrollbarHitWidth, bounds_.height};
}

RectF ScrollViewer::ThumbRect() const
{
    const float trackHeight = (std::max)(0.0f, bounds_.height - kScrollbarInset * 2.0f);
    const float visibleRatio = measuredContent_.height > 0.0f ? (std::min)(1.0f, bounds_.height / measuredContent_.height) : 1.0f;
    const float thumbHeight = (std::min)(trackHeight, (std::max)(kScrollbarMinThumb, trackHeight * visibleRatio));
    const float maxOffset = MaxOffset();
    const float ratio = maxOffset > 0.0f ? offset_ / maxOffset : 0.0f;
    const float x = bounds_.x + bounds_.width - kScrollbarInset - kScrollbarThumbWidth;
    return {x, bounds_.y + kScrollbarInset + (trackHeight - thumbHeight) * ratio, kScrollbarThumbWidth, thumbHeight};
}

void ScrollViewer::ScrollTo(float offset)
{
    offset = (std::clamp)(offset, 0.0f, MaxOffset());
    if (offset == offset_)
    {
        return;
    }
    offset_ = offset;
    UpdateContentLayout();
    if (window_)
    {
        if (msimeui::Scene *scene = window_->GetScene())
        {
            scene->RelayoutPopups();
        }
    }
    InvalidateVisual();
}

void ScrollViewer::ScrollToThumbTop(float thumbTop)
{
    const RectF thumb = ThumbRect();
    const float travel = bounds_.height - kScrollbarInset * 2.0f - thumb.height;
    if (travel <= 0.0f)
    {
        return;
    }
    ScrollTo((thumbTop - bounds_.y - kScrollbarInset) / travel * MaxOffset());
}

void ScrollViewer::UpdateContentLayout()
{
    if (content_)
    {
        content_->ArrangeInLayout({bounds_.x, bounds_.y - offset_, bounds_.width, measuredContent_.height});
    }
}

//
// CaptionToolTip
//
namespace
{
constexpr float kToolTipFontSize = 12.0f;
constexpr float kToolTipPaddingX = 8.0f;
constexpr float kToolTipHeight = 26.0f;
constexpr float kToolTipRadius = 2.0f;
constexpr float kToolTipEdgeMargin = 4.0f;
const wchar_t *const kToolTipFont = L"Segoe UI";
} // namespace

void CaptionToolTip::Show(std::wstring text, const PointF &anchor)
{
    text_ = std::move(text);
    anchor_ = anchor;
    visible_ = true;
    if (window_)
    {
        window_->Invalidate();
    }
}

void CaptionToolTip::Hide()
{
    if (!visible_)
    {
        return;
    }
    visible_ = false;
    if (window_)
    {
        window_->Invalidate();
    }
}

SizeF CaptionToolTip::Measure(const SizeF &availableSize)
{
    return availableSize;
}

void CaptionToolTip::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void CaptionToolTip::Render(msimeui::DeviceResources &deviceResources)
{
    if (!visible_ || text_.empty())
    {
        return;
    }
    // Windows 11 tooltip palette for the current app mode.
    const bool light = IsSystemLightTheme();
    const D2D1_COLOR_F background = light ? ColorFromHex(0xF9F9F9) : ColorFromHex(0x2B2B2B);
    const D2D1_COLOR_F border = light ? ColorFromHex(0xE0E0E0) : ColorFromHex(0x404040);
    const D2D1_COLOR_F text = light ? ColorFromHex(0x1B1B1B) : ColorFromHex(0xFFFFFF);

    const float width = TextWidth(text_, kToolTipFont, kToolTipFontSize) + kToolTipPaddingX * 2.0f;
    const float maxX = bounds_.x + bounds_.width - kToolTipEdgeMargin - width;
    const float x = (std::max)(bounds_.x + kToolTipEdgeMargin, (std::min)(anchor_.x, maxX));
    const float maxY = bounds_.y + bounds_.height - kToolTipEdgeMargin - kToolTipHeight;
    const float y = (std::max)(bounds_.y + kToolTipEdgeMargin, (std::min)(anchor_.y, maxY));
    const RectF rect = {std::round(x), std::round(y), std::ceil(width), kToolTipHeight};

    FillRounded(deviceResources, {rect.x, rect.y + 1.0f, rect.width, rect.height}, kToolTipRadius, D2D1::ColorF(0, 0, 0, light ? 0.08f : 0.3f));
    FillRounded(deviceResources, rect, kToolTipRadius, background);
    StrokeRounded(deviceResources, rect, kToolTipRadius, border, 1.0f);
    DrawTextInRect(deviceResources, text_, kToolTipFont, kToolTipFontSize, DWRITE_FONT_WEIGHT_NORMAL, rect, text, true);
}

msimeui::Visual *CaptionToolTip::FindVisualAt(const PointF &)
{
    return nullptr;
}

msimeui::Visual *CaptionToolTip::FindFocusableAt(const PointF &)
{
    return nullptr;
}

//
// TitleBar
//
namespace
{
constexpr float kTitleBarHeight = 32.0f;
constexpr float kCaptionButtonWidth = 46.0f;
constexpr float kCaptionGlyphSize = 10.0f;

// Segoe MDL2 Assets (Windows 10) has the same code points.
const std::wstring &CaptionIconFont()
{
    static const std::wstring family = IsFontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return family;
}
constexpr UINT kToolTipDelayMs = 600;
// Below the arrow cursor, like the native tooltip.
constexpr float kToolTipCursorOffset = 20.0f;
} // namespace

TitleBar::TitleBar(std::wstring title) : title_(std::move(title)), toolTip_(std::make_shared<CaptionToolTip>())
{
}

const std::shared_ptr<CaptionToolTip> &TitleBar::GetToolTip() const
{
    return toolTip_;
}

void CALLBACK TitleBar::ToolTipTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD)
{
    KillTimer(hwnd, id);
    reinterpret_cast<TitleBar *>(id)->ShowToolTip();
}

void TitleBar::ShowToolTip()
{
    const HWND hwnd = window_ ? window_->GetHandle() : nullptr;
    if (!hwnd || pressed_ != HTNOWHERE)
    {
        return;
    }
    const wchar_t *text = nullptr;
    switch (hot_)
    {
    case HTMINBUTTON:
        text = L"最小化";
        break;
    case HTMAXBUTTON:
        text = IsZoomed(hwnd) ? L"向下还原" : L"最大化";
        break;
    case HTCLOSE:
        text = L"关闭";
        break;
    default:
        return;
    }
    POINT cursor = {};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    const PointF dip = window_->ClientPixelsToDips(cursor);
    toolTip_->Show(text, {dip.x, dip.y + kToolTipCursorOffset});
}

LRESULT TitleBar::HitTestCaption(const PointF &point) const
{
    if (!Contains(bounds_, point) || Contains(ButtonRect(HTMINBUTTON), point) || Contains(ButtonRect(HTCLOSE), point))
    {
        return HTCLIENT;
    }
    return Contains(ButtonRect(HTMAXBUTTON), point) ? HTMAXBUTTON : HTCAPTION;
}

void TitleBar::SetButtonState(LRESULT hot, LRESULT pressed)
{
    // The host only tracks the maximize button; leaving it must not clear a
    // client button that the mouse has already moved onto.
    if (hot == HTNOWHERE && pressed == HTNOWHERE && IsClientButton(hot_))
    {
        return;
    }
    UpdateButtons(hot, pressed);
}

bool TitleBar::IsClientButton(LRESULT button)
{
    return button == HTMINBUTTON || button == HTCLOSE;
}

LRESULT TitleBar::ClientButtonAt(const POINT &point) const
{
    if (!window_)
    {
        return HTNOWHERE;
    }
    const PointF dip = window_->ClientPixelsToDips(point);
    for (const LRESULT button : {HTMINBUTTON, HTCLOSE})
    {
        if (Contains(ButtonRect(button), dip))
        {
            return button;
        }
    }
    return HTNOWHERE;
}

bool TitleBar::OnMouseDown(const POINT &point, WPARAM)
{
    const LRESULT button = ClientButtonAt(point);
    if (button == HTNOWHERE)
    {
        return false;
    }
    UpdateButtons(button, button);
    return true;
}

bool TitleBar::OnMouseUp(const POINT &point, WPARAM)
{
    if (!IsClientButton(pressed_))
    {
        return false;
    }
    const LRESULT button = ClientButtonAt(point);
    const bool clicked = button == pressed_;
    UpdateButtons(button, HTNOWHERE);
    if (clicked && window_)
    {
        PostMessageW(window_->GetHandle(), WM_SYSCOMMAND, button == HTMINBUTTON ? SC_MINIMIZE : SC_CLOSE, 0);
    }
    return true;
}

bool TitleBar::OnMouseMove(const POINT &point, WPARAM)
{
    // Keep the press while dragging off the button, like a native one.
    UpdateButtons(ClientButtonAt(point), IsClientButton(pressed_) ? pressed_ : HTNOWHERE);
    return true;
}

void TitleBar::OnMouseLeave()
{
    if (IsClientButton(hot_))
    {
        UpdateButtons(HTNOWHERE, HTNOWHERE);
    }
}

void TitleBar::UpdateButtons(LRESULT hot, LRESULT pressed)
{
    if (hot_ == hot && pressed_ == pressed)
    {
        return;
    }
    const bool hotChanged = hot_ != hot;
    hot_ = hot;
    pressed_ = pressed;
    if (hotChanged || pressed != HTNOWHERE)
    {
        toolTip_->Hide();
        const HWND hwnd = window_ ? window_->GetHandle() : nullptr;
        const auto timerId = reinterpret_cast<UINT_PTR>(this);
        if (hwnd)
        {
            KillTimer(hwnd, timerId);
            if (hot != HTNOWHERE && pressed == HTNOWHERE)
            {
                SetTimer(hwnd, timerId, kToolTipDelayMs, &TitleBar::ToolTipTimerProc);
            }
        }
    }
    InvalidateVisual();
}

SizeF TitleBar::Measure(const SizeF &availableSize)
{
    return {availableSize.width, kTitleBarHeight};
}

void TitleBar::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

RectF TitleBar::ButtonRect(LRESULT button) const
{
    const float slot = button == HTCLOSE ? 1.0f : button == HTMAXBUTTON ? 2.0f : 3.0f;
    return {bounds_.x + bounds_.width - kCaptionButtonWidth * slot, bounds_.y, kCaptionButtonWidth, bounds_.height};
}

void TitleBar::Render(msimeui::DeviceResources &deviceResources)
{
    const ThemeColors &theme = GetTheme();
    const HWND hwnd = window_ ? window_->GetHandle() : nullptr;
    const bool active = hwnd && GetForegroundWindow() == hwnd;
    FillRounded(deviceResources, bounds_, 0.0f, theme.chromeBg);

    const float textX = bounds_.x + 12.0f;
    const float textRight = ButtonRect(HTMINBUTTON).x - 8.0f;
    DrawTextInRect(deviceResources, title_, L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, {textX, bounds_.y, (std::max)(0.0f, textRight - textX), bounds_.height}, active ? theme.text : theme.textMuted);

    for (const LRESULT button : {HTMINBUTTON, HTMAXBUTTON, HTCLOSE})
    {
        RenderButton(deviceResources, button, active);
    }
}

void TitleBar::RenderButton(msimeui::DeviceResources &deviceResources, LRESULT button, bool windowActive)
{
    const ThemeColors &theme = GetTheme();
    const RectF rect = ButtonRect(button);
    const bool hot = hot_ == button;
    const bool pressed = pressed_ == button && hot;
    const bool closeHighlighted = button == HTCLOSE && hot;
    if (closeHighlighted)
    {
        FillRounded(deviceResources, rect, 0.0f, pressed ? WithAlpha(theme.closeHover, 0.9f) : theme.closeHover);
    }
    else if (hot)
    {
        FillRounded(deviceResources, rect, 0.0f, pressed ? WithAlpha(theme.titlebarBtnHover, 0.6f) : theme.titlebarBtnHover);
    }

    D2D1_COLOR_F glyphColor = windowActive ? theme.text : theme.textMuted;
    if (closeHighlighted)
    {
        glyphColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, pressed ? 0.7f : 1.0f);
    }
    // Same glyphs as the native Windows 11 caption buttons (Segoe Fluent Icons code points).
    wchar_t glyph[2] = {0xE8BB, 0}; // ChromeClose
    if (button == HTMINBUTTON)
    {
        glyph[0] = 0xE921; // ChromeMinimize
    }
    else if (button == HTMAXBUTTON)
    {
        const HWND hwnd = window_ ? window_->GetHandle() : nullptr;
        glyph[0] = hwnd && IsZoomed(hwnd) ? 0xE923 : 0xE922; // ChromeRestore : ChromeMaximize
    }
    // Grayscale like the native glyphs; ClearType leaves color fringes on these thin strokes.
    ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
    const D2D1_TEXT_ANTIALIAS_MODE previousMode = target ? target->GetTextAntialiasMode() : D2D1_TEXT_ANTIALIAS_MODE_DEFAULT;
    if (target)
    {
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }
    DrawTextInRect(deviceResources, glyph, CaptionIconFont(), kCaptionGlyphSize, DWRITE_FONT_WEIGHT_NORMAL, rect, glyphColor, true);
    if (target)
    {
        target->SetTextAntialiasMode(previousMode);
    }
}

//
// AppLogo
//
AppLogo::AppLogo(float size) : size_(size)
{
}

SizeF AppLogo::Measure(const SizeF &)
{
    return {size_, size_};
}

void AppLogo::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void AppLogo::Render(msimeui::DeviceResources &deviceResources)
{
    ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
    if (!target)
    {
        return;
    }
    const float s = size_;
    const float x = bounds_.x;
    const float y = bounds_.y + (bounds_.height - s) * 0.5f;

    // Tile: accent gradient, same stops as assets/icons/directkeycast.ico.
    D2D1_GRADIENT_STOP stops[] = {{0.0f, ColorFromHex(0x9C9AE6)}, {1.0f, ColorFromHex(0x6B69D6)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    if (SUCCEEDED(target->CreateGradientStopCollection(stops, 2, collection.GetAddressOf())))
    {
        target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x, y), D2D1::Point2F(x + s, y + s)), collection.Get(), gradient.GetAddressOf());
    }
    const D2D1_ROUNDED_RECT tile = D2D1::RoundedRect(D2D1::RectF(x, y, x + s, y + s), s * 0.24f, s * 0.24f);
    if (gradient)
    {
        target->FillRoundedRectangle(tile, gradient.Get());
    }

    // Keycap: white face over a translucent lip.
    const float capL = x + s * 0.20f;
    const float capR = x + s * 0.80f;
    const float capT = y + s * 0.20f;
    const float capB = y + s * 0.74f;
    ID2D1SolidColorBrush *lip = deviceResources.GetSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.45f));
    ID2D1SolidColorBrush *face = deviceResources.GetSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    if (lip && face)
    {
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(capL, capT + s * 0.06f, capR, capB + s * 0.06f), s * 0.10f, s * 0.10f), lip);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(capL, capT, capR, capB), s * 0.10f, s * 0.10f), face);
    }
    DrawTextInRect(deviceResources, L"K", L"Segoe UI", s * 0.36f, DWRITE_FONT_WEIGHT_BOLD, {capL, capT, capR - capL, capB - capT}, ColorFromHex(0x5B57B8), true);
}

//
// OverlayPreview
//
namespace
{
const std::vector<OverlayStroke> kPreviewSample = {{{L"A"}}, {{L"S"}}, {{L"D"}}, {{L"F"}}, {{L"Space"}}, {{L"LCtrl", L"C"}}};
constexpr float kPreviewHeight = 150.0f;
} // namespace

OverlayPreview::OverlayPreview() = default;

SizeF OverlayPreview::Measure(const SizeF &availableSize)
{
    const float rows = static_cast<float>(std::clamp(::KeycastConfig.rows, 1, 5));
    const float height = rows * (::KeycastConfig.fontSize + 28.0f) + (rows - 1) * 10.0f + 42.0f;
    return {availableSize.width, (std::max)(kPreviewHeight, height)};
}

void OverlayPreview::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void OverlayPreview::EnsureLayouts()
{
    auto sample = kPreviewSample;
    if (::KeycastConfig.rows > 1)
    {
        sample.clear();
        const std::wstring keys = L"ASDFJKLQWERTYUIOPZXCVBNM";
        for (size_t i = 0; i < 80; ++i)
            sample.push_back({{std::wstring(1, keys[i % keys.size()])}});
        sample.push_back({{L"LCtrl", L"C"}});
    }
    layout_.Build(sample, (std::max)(1.0f, bounds_.width - 32.0f));
}

void OverlayPreview::Render(msimeui::DeviceResources &deviceResources)
{
    ID2D1HwndRenderTarget *target = deviceResources.GetRenderTarget();
    if (!target)
    {
        return;
    }
    EnsureLayouts();
    const ThemeColors &theme = GetTheme();
    target->PushAxisAlignedClip(ToD2D(bounds_), D2D1_ANTIALIAS_MODE_ALIASED);
    FillRounded(deviceResources, bounds_, 8.0f, ColorFromHex(IsLightThemeResolved() ? 0xe3e6ec : 0x16171b));
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target->CreateSolidColorBrush(theme.overlayText, brush.GetAddressOf())))
    {
        layout_.Draw(target, brush.Get(), D2D1::Point2F(bounds_.x + (bounds_.width - layout_.Width()) / 2, bounds_.y + (bounds_.height - layout_.Height()) / 2));
    }
    target->PopAxisAlignedClip();
}
} // namespace dkui
