#include "config.h"
#include "d2d.h"
#include "globals.h"
#include "hook.h"
#include "settings.h"
#include "theme.h"
#include "tray_menu.h"
#include "utils.h"
#include "window.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
std::vector<std::wstring> g_fontFamilies;
bool g_fontsLoaded = false;

std::string Trim(const std::string &s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
    {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
    {
        --e;
    }
    return s.substr(b, e - b);
}

std::string Unquote(const std::string &s)
{
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
    {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::string EscapeToml(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '\\' || c == '"')
        {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

bool ParseBool(const std::string &s, bool fallback)
{
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "true" || v == "1")
    {
        return true;
    }
    if (v == "false" || v == "0")
    {
        return false;
    }
    return fallback;
}

int ParseInt(const std::string &s, int fallback)
{
    try
    {
        return std::stoi(s);
    }
    catch (...)
    {
        return fallback;
    }
}

float ParseFloat(const std::string &s, float fallback)
{
    try
    {
        return std::stof(s);
    }
    catch (...)
    {
        return fallback;
    }
}

using TomlTable = std::map<std::string, std::map<std::string, std::string>>;

TomlTable ParseToml(const std::string &text)
{
    TomlTable table;
    std::string section;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            section = Trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string key = Trim(trimmed.substr(0, eq));
        std::string value = Unquote(Trim(trimmed.substr(eq + 1)));
        table[section][key] = value;
    }
    return table;
}

std::string GetValue(const TomlTable &table, const std::string &section, const std::string &key, const std::string &fallback)
{
    auto sit = table.find(section);
    if (sit == table.end())
    {
        return fallback;
    }
    auto kit = sit->second.find(key);
    if (kit == sit->second.end())
    {
        return fallback;
    }
    return kit->second;
}

std::wstring ClampThemeMode(const std::wstring &mode)
{
    if (mode == L"light" || mode == L"system" || mode == L"dark")
    {
        return mode;
    }
    return L"dark";
}

std::wstring ClampPosition(const std::wstring &pos)
{
    if (pos == L"bottom-left" || pos == L"top-right" || pos == L"top-left" || pos == L"bottom-right")
    {
        return pos;
    }
    return L"bottom-right";
}

float ClampFloat(float v, float lo, float hi)
{
    return (std::max)(lo, (std::min)(hi, v));
}

int ClampInt(int v, int lo, int hi)
{
    return (std::max)(lo, (std::min)(hi, v));
}

std::string BuildConfigText()
{
    const auto &c = ::KeycastConfig;
    std::ostringstream out;
    out << "#\n# DirectKeycast\n#\n\n";
    out << "[appearance]\n";
    out << "# 主题模式，可选：\"dark\", \"light\", \"system\"\n";
    out << "theme_mode = \"" << EscapeToml(wstring_to_string(c.themeMode)) << "\"\n";
    out << "# 按键浮层字体\n";
    out << "font = \"" << EscapeToml(wstring_to_string(c.font)) << "\"\n";
    out << "# 缺省字体（指定字体不存在时使用）\n";
    out << "default_font = \"" << EscapeToml(wstring_to_string(c.defaultFont)) << "\"\n";
    out << "# 字体大小（px），可选：12–32\n";
    out << "font_size = " << static_cast<int>(c.fontSize + 0.5f) << "\n";
    out << "# 彩虹文字（历史键帽使用不同颜色，最新按键使用黄色）\n";
    out << "rainbow_text = " << (c.rainbowText ? "true" : "false") << "\n";
    out << "# 键帽背景不透明度，范围 0.35–1.0\n";
    out << "bg_opacity = " << c.bgOpacity << "\n";
    out << "# 圆角半径，可选：8 / 12 / 16\n";
    out << "corner_radius = " << c.cornerRadius << "\n";
    out << "# 描边宽度，可选：0 / 1 / 2 / 3\n";
    out << "border_thickness = " << c.borderThickness << "\n\n";
    out << "[overlay]\n";
    out << "# 显示按键浮层\n";
    out << "enabled = " << (c.enabled ? "true" : "false") << "\n";
    out << "# 同时显示鼠标按键\n";
    out << "show_mouse = " << (c.showMouse ? "true" : "false") << "\n";
    out << "# 无输入后自动隐藏（毫秒）\n";
    out << "fade_ms = " << c.fadeMs << "\n";
    out << "# 浮层位置，可选：\"bottom-right\", \"bottom-left\", \"top-right\", \"top-left\"\n";
    out << "position = \"" << EscapeToml(wstring_to_string(c.position)) << "\"\n";
    return out.str();
}

void ApplyParsed(const TomlTable &table)
{
    auto &c = ::KeycastConfig;
    c.themeMode = ClampThemeMode(string_to_wstring(GetValue(table, "appearance", "theme_mode", "dark")));
    c.font = string_to_wstring(GetValue(table, "appearance", "font", "Cascadia Mono"));
    c.defaultFont = string_to_wstring(GetValue(table, "appearance", "default_font", "Microsoft YaHei"));
    c.fontSize = static_cast<float>(ClampInt(ParseInt(GetValue(table, "appearance", "font_size", "24"), 24), 12, 32));
    c.rainbowText = ParseBool(GetValue(table, "appearance", "rainbow_text", "false"), false);
    c.bgOpacity = ClampFloat(ParseFloat(GetValue(table, "appearance", "bg_opacity", "0.75"), 0.75f), 0.35f, 1.0f);
    c.cornerRadius = ClampFloat(ParseFloat(GetValue(table, "appearance", "corner_radius", "12"), 12.0f), 8.0f, 16.0f);
    c.borderThickness = ClampFloat(ParseFloat(GetValue(table, "appearance", "border_thickness", "3"), 3.0f), 0.0f, 3.0f);
    c.enabled = ParseBool(GetValue(table, "overlay", "enabled", "true"), true);
    c.showMouse = ParseBool(GetValue(table, "overlay", "show_mouse", "true"), true);
    c.fadeMs = ClampInt(ParseInt(GetValue(table, "overlay", "fade_ms", "1500"), 1500), 300, 8000);
    c.position = ClampPosition(string_to_wstring(GetValue(table, "overlay", "position", "bottom-right")));
}

void ApplyDefaults()
{
    // maxSize is derived from the monitor, not a user setting.
    const int maxSize = ::KeycastConfig.maxSize;
    ::KeycastConfig = AppConfig{};
    ::KeycastConfig.maxSize = maxSize;
}

// Picks the first installed family out of font -> default_font -> built-in fallback.
void ResolveOverlayFont()
{
    const auto &c = ::KeycastConfig;
    if (IsFontExists(c.font))
    {
        ::fontName = c.font;
    }
    else if (IsFontExists(c.defaultFont))
    {
        ::fontName = c.defaultFont;
    }
    else
    {
        ::fontName = ::backupFont;
    }
}

int CALLBACK FontEnumProc(const LOGFONTW *lf, const TEXTMETRICW *, DWORD, LPARAM lParam)
{
    auto *out = reinterpret_cast<std::vector<std::wstring> *>(lParam);
    if (lf->lfFaceName[0] == L'@')
    {
        return 1;
    }
    std::wstring name = lf->lfFaceName;
    if (std::find(out->begin(), out->end(), name) == out->end())
    {
        out->push_back(std::move(name));
    }
    return 1;
}
} // namespace

std::filesystem::path GetConfigDirectory()
{
    PWSTR appData = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)) && appData)
    {
        dir = std::filesystem::path(appData) / L"DirectKeycast";
        CoTaskMemFree(appData);
    }
    else
    {
        dir = std::filesystem::current_path() / L"config";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path GetConfigFilePath()
{
    return GetConfigDirectory() / L"config.toml";
}

void InitializeConfig()
{
    ApplyDefaults();
    LoadAppConfig();
}

bool LoadAppConfig()
{
    const auto path = GetConfigFilePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        SaveAppConfig();
        ResolveOverlayFont();
        RefreshTheme();
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        ResolveOverlayFont();
        RefreshTheme();
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    ApplyParsed(ParseToml(buf.str()));
    ResolveOverlayFont();
    RefreshTheme();
    return true;
}

void ResetAppConfig()
{
    ApplyDefaults();
    SaveAppConfig();
    ApplyConfigToRuntime();
}

void SetOverlayEnabled(bool enabled)
{
    if (::KeycastConfig.enabled == enabled)
    {
        return;
    }
    ::KeycastConfig.enabled = enabled;
    SaveAppConfig();
    ApplyConfigToRuntime();
}

bool SaveAppConfig()
{
    const auto path = GetConfigFilePath();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }
    const std::string text = BuildConfigText();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

void ApplyConfigToRuntime()
{
    ResolveOverlayFont();
    RefreshTheme();
    RecreateOverlayTextFormat();
    InvalidateOverlayCache();
    if (::D2DHwnd)
    {
        InitGlobalConfigWithD2D();
        UpdateOverlayWindowPlacement(::D2DHwnd);
        InvalidateRect(::D2DHwnd, nullptr, FALSE);
        if (!::KeycastConfig.enabled)
        {
            ShowWindow(::D2DHwnd, SW_HIDE);
        }
    }
    RefreshTrayMenuTheme();
    RefreshSettingsWindow();
    UpdateMouseRawInput();
}

const std::vector<std::wstring> &GetSystemFontFamilies()
{
    if (g_fontsLoaded)
    {
        return g_fontFamilies;
    }
    g_fontsLoaded = true;
    HDC hdc = GetDC(nullptr);
    if (!hdc)
    {
        return g_fontFamilies;
    }
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &lf, FontEnumProc, reinterpret_cast<LPARAM>(&g_fontFamilies), 0);
    ReleaseDC(nullptr, hdc);
    std::sort(g_fontFamilies.begin(), g_fontFamilies.end());
    return g_fontFamilies;
}

bool OpenConfigFile()
{
    const auto path = GetConfigFilePath();
    if (!std::filesystem::exists(path))
    {
        SaveAppConfig();
    }
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool OpenConfigDirectory()
{
    const auto dir = GetConfigDirectory();
    const HINSTANCE result = ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}
