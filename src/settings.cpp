#include "settings.h"
#include "config.h"
#include "globals.h"
#include "msimeui_host.h"
#include "settings_controls.h"
#include "theme.h"
#include "tray_menu.h"
#include "version.h"
#include "window.h"

#include "msimeui/Layout.h"
#include "msimeui/Scene.h"
#include "msimeui/Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <shellapi.h>
#include <string>
#include <vector>

namespace
{
constexpr int kWindowWidthDip = 940;
constexpr int kWindowHeightDip = 680;
constexpr int kMinWidthDip = 760;
constexpr int kMinHeightDip = 520;
constexpr float kSidebarWidth = 200.0f;
constexpr float kContentMaxWidth = 900.0f;
constexpr const wchar_t *kWindowTitle = L"Settings";
constexpr const wchar_t *kProjectUrl = L"https://github.com/fanlusky/DirectKeycast";

enum class Page
{
    Appearance = 0,
    Overlay,
    Shortcut,
    About,
};

struct PageEntry
{
    std::shared_ptr<dkui::NavItem> nav;
    std::shared_ptr<msimeui::Visual> view;
};

using VisualPtr = std::shared_ptr<msimeui::Visual>;

std::unique_ptr<msimeui::Window> g_window;
std::shared_ptr<dkui::TitleBar> g_titleBar;
std::shared_ptr<msimeui::Container> g_content;
std::vector<PageEntry> g_pages;
Page g_page = Page::Appearance;
// Pushes the current config into each control, e.g. after the tray menu or a
// hotkey toggled the overlay, or after "restore defaults".
std::vector<std::function<void()>> g_syncers;
bool g_syncing = false;

void CommitConfig()
{
    if (g_syncing)
    {
        return;
    }
    SaveAppConfig();
    ApplyConfigToRuntime();
}

//
// Control factories bound to AppConfig fields
//
std::shared_ptr<dkui::Dropdown> MakeChoice(std::vector<std::wstring> labels, std::function<size_t()> current, std::function<void(size_t)> apply)
{
    auto dropdown = std::make_shared<dkui::Dropdown>(std::move(labels), current());
    dropdown->SetOnChanged([apply](size_t index) {
        apply(index);
        CommitConfig();
    });
    std::weak_ptr<dkui::Dropdown> weak = dropdown;
    g_syncers.push_back([weak, current]() {
        if (auto control = weak.lock())
        {
            control->SetSelectedIndex(current());
        }
    });
    return dropdown;
}

std::shared_ptr<dkui::Dropdown> MakeStringChoice(std::vector<std::wstring> labels, std::vector<std::wstring> values, std::wstring AppConfig::*field)
{
    auto current = [values, field]() {
        const auto it = std::find(values.begin(), values.end(), ::KeycastConfig.*field);
        return it == values.end() ? size_t{0} : static_cast<size_t>(it - values.begin());
    };
    return MakeChoice(std::move(labels), current, [values, field](size_t index) { ::KeycastConfig.*field = values[index]; });
}

// Values hand-edited into config.toml may sit between the offered steps; show the nearest one.
template <typename T> size_t NearestIndex(const std::vector<T> &values, T value)
{
    size_t best = 0;
    for (size_t i = 1; i < values.size(); ++i)
    {
        if (std::abs(static_cast<double>(values[i] - value)) < std::abs(static_cast<double>(values[best] - value)))
        {
            best = i;
        }
    }
    return best;
}

template <typename T> std::shared_ptr<dkui::Dropdown> MakeNumberChoice(std::vector<std::wstring> labels, std::vector<T> values, T AppConfig::*field)
{
    return MakeChoice(std::move(labels), [values, field]() { return NearestIndex(values, ::KeycastConfig.*field); }, [values, field](size_t index) { ::KeycastConfig.*field = values[index]; });
}

std::shared_ptr<dkui::Dropdown> MakeFontChoice(std::wstring AppConfig::*field)
{
    std::vector<std::wstring> fonts = GetSystemFontFamilies();
    // Keep the configured and default families selectable even when they are not installed.
    for (const std::wstring &extra : {::KeycastConfig.*field, AppConfig{}.*field})
    {
        if (!extra.empty() && std::find(fonts.begin(), fonts.end(), extra) == fonts.end())
        {
            fonts.insert(fonts.begin(), extra);
        }
    }
    auto dropdown = MakeStringChoice(fonts, fonts, field);
    dropdown->SetMinWidth(168.0f);
    dropdown->SetMaxWidth(200.0f);
    return dropdown;
}

std::shared_ptr<dkui::ToggleSwitch> MakeToggle(bool AppConfig::*field)
{
    auto toggle = std::make_shared<dkui::ToggleSwitch>(::KeycastConfig.*field);
    toggle->SetOnChanged([field](bool on) {
        ::KeycastConfig.*field = on;
        CommitConfig();
    });
    std::weak_ptr<dkui::ToggleSwitch> weak = toggle;
    g_syncers.push_back([weak, field]() {
        if (auto control = weak.lock())
        {
            control->SetChecked(::KeycastConfig.*field);
        }
    });
    return toggle;
}

std::shared_ptr<dkui::ActionButton> MakeButton(const std::wstring &text, std::function<void()> onClick)
{
    auto button = std::make_shared<dkui::ActionButton>(text);
    button->SetOnClick(std::move(onClick));
    return button;
}

//
// Layout helpers
//
VisualPtr Row(const std::wstring &title, const std::wstring &description, VisualPtr control)
{
    return std::make_shared<dkui::SettingRow>(title, description, std::move(control));
}

std::shared_ptr<dkui::Section> MakeSection(const std::vector<VisualPtr> &rows)
{
    auto stack = std::make_shared<msimeui::StackPanel>(0.0f);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (i > 0)
        {
            stack->AddChild(std::make_shared<dkui::Divider>());
        }
        stack->AddChild(rows[i]);
    }
    return std::make_shared<dkui::Section>(stack);
}

VisualPtr MakePage(const std::wstring &title, const std::vector<VisualPtr> &sections)
{
    auto stack = std::make_shared<msimeui::StackPanel>(0.0f);
    stack->SetPadding({12.0f, 14.0f, 12.0f, 12.0f});
    auto header = std::make_shared<dkui::Label>(title, 18.0f, DWRITE_FONT_WEIGHT_MEDIUM);
    header->SetMargin({7.0f, 0.0f, 24.0f, 20.0f});
    stack->AddChild(header);
    for (const auto &section : sections)
    {
        section->SetMargin({0.0f, 0.0f, 0.0f, 18.0f});
        stack->AddChild(section);
    }
    auto column = std::make_shared<dkui::CenteredColumn>(stack, kContentMaxWidth);
    column->SetPadding({16.0f, 0.0f, 24.0f, 0.0f});
    auto scroll = std::make_shared<dkui::ScrollViewer>(column);
    scroll->SetVerticalAlignment(msimeui::VerticalAlignment::Stretch);
    return scroll;
}

//
// Pages
//
VisualPtr BuildAppearancePage()
{
    auto previewStack = std::make_shared<msimeui::StackPanel>(0.0f);
    previewStack->AddChild(std::make_shared<dkui::Label>(L"按键浮层预览", 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    auto preview = std::make_shared<dkui::OverlayPreview>();
    preview->SetMargin({0.0f, 16.0f, 0.0f, 0.0f});
    previewStack->AddChild(preview);
    g_syncers.push_back([weak = std::weak_ptr<dkui::OverlayPreview>(preview)]() {
        if (auto control = weak.lock()) control->InvalidateMeasure();
    });
    auto previewSection = std::make_shared<dkui::Section>(previewStack);

    std::vector<std::wstring> sizeLabels;
    std::vector<float> sizeValues;
    for (int size = 12; size <= 32; ++size)
    {
        sizeLabels.push_back(std::to_wstring(size));
        sizeValues.push_back(static_cast<float>(size));
    }

    auto textSection = MakeSection({
        Row(L"主题模式", L"设置窗口、托盘菜单与按键浮层的配色", MakeStringChoice({L"深色", L"浅色", L"跟随系统"}, {L"dark", L"light", L"system"}, &AppConfig::themeMode)),
        Row(L"浮层字体", L"", MakeFontChoice(&AppConfig::font)),
        Row(L"缺省字体", L"浮层字体未安装时使用", MakeFontChoice(&AppConfig::defaultFont)),
        Row(L"字体大小", L"", MakeNumberChoice(sizeLabels, sizeValues, &AppConfig::fontSize)),
    });

    auto lookSection = MakeSection({
        Row(L"彩虹文字", L"历史键帽使用不同颜色，最新按键以黄色高亮", MakeToggle(&AppConfig::rainbowText)),
        Row(L"键帽不透明度", L"", MakeNumberChoice({L"35%", L"50%", L"65%", L"75%", L"90%", L"100%"}, {0.35f, 0.50f, 0.65f, 0.75f, 0.90f, 1.00f}, &AppConfig::bgOpacity)),
        Row(L"键帽圆角", L"", MakeNumberChoice({L"8", L"12", L"16"}, {8.0f, 12.0f, 16.0f}, &AppConfig::cornerRadius)),
        Row(L"键帽描边", L"", MakeNumberChoice({L"无", L"1", L"2", L"3"}, {0.0f, 1.0f, 2.0f, 3.0f}, &AppConfig::borderThickness)),
    });

    return MakePage(L"外观", {previewSection, textSection, lookSection});
}

VisualPtr BuildOverlayPage()
{
    auto section = MakeSection({
        Row(L"显示按键浮层", L"关闭后可在托盘菜单或按 Ctrl + Alt + Shift + F11 重新开启", MakeToggle(&AppConfig::enabled)),
        Row(L"显示鼠标按键", L"同时显示鼠标点击与滚轮", MakeToggle(&AppConfig::showMouse)),
        Row(L"自动隐藏", L"每排停止输入后等待此时长，再用 0.3 秒淡出", MakeNumberChoice({L"0.5 秒", L"1 秒", L"1.5 秒", L"2 秒", L"3 秒", L"5 秒"}, {500, 1000, 1500, 2000, 3000, 5000}, &AppConfig::fadeMs)),
        Row(L"显示排数", L"当前排放满后上移，超过排数时移除最上面的一排", MakeNumberChoice({L"1 排", L"2 排", L"3 排", L"4 排", L"5 排"}, {1, 2, 3, 4, 5}, &AppConfig::rows)),
        Row(L"浮层位置", L"显示在当前屏幕工作区的哪个角落", MakeStringChoice({L"右下", L"左下", L"右上", L"左上"}, {L"bottom-right", L"bottom-left", L"top-right", L"top-left"}, &AppConfig::position)),
    });
    return MakePage(L"浮层", {section});
}

VisualPtr BuildShortcutPage()
{
    const std::wstring busy = L"快捷键已被其他程序占用，当前不可用";
    auto hotkeys = MakeSection({
        Row(L"显示 / 隐藏按键浮层", ::ToggleHotkeyRegistered ? L"" : busy, std::make_shared<dkui::KeyCaps>(std::vector<std::wstring>{L"Ctrl", L"Alt", L"Shift", L"F11"})),
        Row(L"退出 DirectKeycast", ::ExitHotkeyRegistered ? L"" : busy, std::make_shared<dkui::KeyCaps>(std::vector<std::wstring>{L"Ctrl", L"Alt", L"Shift", L"F12"})),
    });

    auto trayHint = [](const wchar_t *text) {
        auto label = std::make_shared<dkui::Label>(text, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, dkui::TextRole::Muted);
        return label;
    };
    auto tray = MakeSection({
        Row(L"打开设置", L"", trayHint(L"单击托盘图标")),
        Row(L"托盘菜单", L"", trayHint(L"右键单击托盘图标")),
    });
    return MakePage(L"快捷键", {hotkeys, tray});
}

VisualPtr BuildAboutPage()
{
    auto intro = std::make_shared<msimeui::HorizontalStackPanel>(16.0f);
    intro->SetVerticalContentAlignment(msimeui::VerticalAlignment::Center);
    intro->AddChild(std::make_shared<dkui::AppLogo>(56.0f));
    auto introText = std::make_shared<msimeui::StackPanel>(0.0f);
    introText->AddChild(std::make_shared<dkui::Label>(L"DirectKeycast", 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    auto version = std::make_shared<dkui::Label>(std::wstring(L"版本 ") + DK_VERSION_WSTRING, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, dkui::TextRole::Muted);
    version->SetMargin({0.0f, 2.0f, 0.0f, 0.0f});
    introText->AddChild(version);
    auto tagline = std::make_shared<dkui::Label>(L"轻量的按键显示工具，适合录屏、演示与教学。", 13.0f, DWRITE_FONT_WEIGHT_NORMAL, dkui::TextRole::Secondary);
    tagline->SetMargin({0.0f, 6.0f, 0.0f, 0.0f});
    introText->AddChild(tagline);
    intro->AddChild(introText);
    auto introSection = std::make_shared<dkui::Section>(intro);

    auto files = MakeSection({
        Row(L"配置文件", GetConfigFilePath().wstring(), MakeButton(L"打开", []() { OpenConfigFile(); })),
        Row(L"配置目录", GetConfigDirectory().wstring(), MakeButton(L"打开", []() { OpenConfigDirectory(); })),
        Row(L"恢复默认设置", L"将所有设置恢复为初始值",
            MakeButton(L"恢复",
                       []() {
                           if (MessageBoxW(g_window ? g_window->GetHandle() : nullptr, L"确定要将所有设置恢复为默认值吗？", L"DirectKeycast", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
                           {
                               ResetAppConfig();
                           }
                       })),
        Row(L"项目主页", L"github.com/fanlusky/DirectKeycast", MakeButton(L"访问", []() { ShellExecuteW(nullptr, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL); })),
    });
    return MakePage(L"关于", {introSection, files});
}

//
// Window
//
void ShowPage(Page page)
{
    g_page = page;
    const size_t index = static_cast<size_t>(page);
    if (index >= g_pages.size() || !g_window)
    {
        return;
    }
    if (msimeui::Scene *scene = g_window->GetScene())
    {
        scene->ClearPopups(true);
    }
    for (size_t i = 0; i < g_pages.size(); ++i)
    {
        g_pages[i].nav->SetSelected(i == index);
    }
    g_content->SetChild(g_pages[index].view);
    g_window->Invalidate();
}

VisualPtr BuildSidebar()
{
    auto sidebar = std::make_shared<msimeui::StackPanel>(0.0f);
    sidebar->SetPadding({0.0f, 12.0f, 0.0f, 12.0f});

    auto header = std::make_shared<msimeui::HorizontalStackPanel>(9.0f);
    header->SetVerticalContentAlignment(msimeui::VerticalAlignment::Center);
    // `.sidebar-header { padding: 0 12px 12px; margin-bottom: 8px }`
    header->SetMargin({12.0f, 0.0f, 12.0f, 20.0f});
    header->AddChild(std::make_shared<dkui::AppLogo>(25.0f));
    header->AddChild(std::make_shared<dkui::Label>(L"DirectKeycast", 19.0f, DWRITE_FONT_WEIGHT_MEDIUM));
    sidebar->AddChild(header);

    for (size_t i = 0; i < g_pages.size(); ++i)
    {
        sidebar->AddChild(g_pages[i].nav);
        g_pages[i].nav->SetOnClick([i]() { ShowPage(static_cast<Page>(i)); });
    }
    return sidebar;
}

void BuildSettingsScene()
{
    g_syncers.clear();
    g_pages = {
        {std::make_shared<dkui::NavItem>(SvgIcon::Appearance, L"外观"), BuildAppearancePage()},
        {std::make_shared<dkui::NavItem>(SvgIcon::Keyboard, L"浮层"), BuildOverlayPage()},
        {std::make_shared<dkui::NavItem>(SvgIcon::Shortcut, L"快捷键"), BuildShortcutPage()},
        {std::make_shared<dkui::NavItem>(SvgIcon::About, L"关于"), BuildAboutPage()},
    };

    auto root = std::make_shared<dkui::WheelRoutingGrid>();
    root->AddColumn({msimeui::GridUnitType::Pixel, kSidebarWidth});
    root->AddColumn({msimeui::GridUnitType::Star, 1.0f});
    root->AddRow({msimeui::GridUnitType::Auto, 0.0f});
    root->AddRow({msimeui::GridUnitType::Star, 1.0f});

    g_titleBar = std::make_shared<dkui::TitleBar>(kWindowTitle);
    g_content = std::make_shared<msimeui::Container>();
    g_content->SetVerticalAlignment(msimeui::VerticalAlignment::Stretch);
    root->AddChild(g_titleBar, 0, 0, 1, 2);
    root->AddChild(BuildSidebar(), 1, 0);
    root->AddChild(g_content, 1, 1);
    root->AddChild(g_titleBar->GetToolTip(), 0, 0, 2, 2);

    auto scene = std::make_unique<msimeui::Scene>();
    scene->SetRoot(root);
    g_window->SetScene(std::move(scene));
    ShowPage(g_page);
}

void ApplyWindowChrome()
{
    if (!g_window || !g_window->GetHandle())
    {
        return;
    }
    const ThemeColors &theme = GetTheme();
    ApplyImmersiveDarkMode(g_window->GetHandle(), !IsLightThemeResolved());
    ApplyCaptionColors(g_window->GetHandle(), ToColorRef(theme.chromeBg), ToColorRef(theme.text));
}

bool EnsureSettingsWindow()
{
    if (g_window && g_window->GetHandle())
    {
        return true;
    }

    POINT origin{0, 0};
    const UINT dpi = MonitorDpiFromPoint(origin);
    g_window = std::make_unique<msimeui::Window>(L"DirectKeycastSettingsWindow", kWindowTitle, DipToPxForDpi(static_cast<float>(kWindowWidthDip), dpi), DipToPxForDpi(static_cast<float>(kWindowHeightDip), dpi));
    g_window->SetWindowStyle(WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW);
    g_window->SetRoundedCorners(true);
    g_window->SetInitialPlacement(msimeui::WindowInitialPlacement::Center);
    if (!g_window->Create())
    {
        g_window.reset();
        return false;
    }

    HWND hwnd = g_window->GetHandle();
    AttachMsimeuiHost(hwnd, DkMsimeuiHideOnClose | DkMsimeuiMinSize, kMinWidthDip, kMinHeightDip);
    DkCustomCaption caption;
    caption.hitTest = [](POINT clientPt) -> LRESULT { return g_titleBar && g_window ? g_titleBar->HitTestCaption(g_window->ClientPixelsToDips(clientPt)) : HTCLIENT; };
    caption.setButtonState = [](LRESULT hot, LRESULT pressed) {
        if (g_titleBar)
        {
            g_titleBar->SetButtonState(hot, pressed);
        }
    };
    AttachCustomCaption(hwnd, std::move(caption));
    ApplyWindowChrome();
    // Built once per window: msimeui::Window holds raw pointers to hovered/focused
    // visuals, so the tree must outlive the HWND instead of being rebuilt on changes.
    BuildSettingsScene();
    ::SettingsHwnd = hwnd;
    return true;
}
} // namespace

bool OpenSettingsWindow(bool about)
{
    HideTrayMenu();
    if (!EnsureSettingsWindow())
    {
        return false;
    }
    ShowPage(about ? Page::About : g_page);
    HWND hwnd = g_window->GetHandle();
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
    g_window->Invalidate();
    return true;
}

void CloseSettingsWindow()
{
    if (g_window && g_window->GetHandle())
    {
        DestroyWindow(g_window->GetHandle());
    }
    g_window.reset();
    g_titleBar.reset();
    g_content.reset();
    g_pages.clear();
    g_syncers.clear();
    ::SettingsHwnd = nullptr;
}

bool IsSettingsWindowOpen()
{
    return g_window && g_window->GetHandle() && IsWindowVisible(g_window->GetHandle());
}

void RefreshSettingsWindow()
{
    if (!g_window || !g_window->GetHandle())
    {
        return;
    }
    ApplyWindowChrome();
    g_syncing = true;
    for (const auto &sync : g_syncers)
    {
        sync();
    }
    g_syncing = false;
    g_window->Invalidate();
}

bool IsPointInSettingsWindow(POINT screenPt)
{
    if (!g_window)
    {
        return false;
    }
    return IsPointInWindow(g_window->GetHandle(), screenPt);
}
