#include "utils.h"
#include "globals.h"
#include "theme.h"
#include <algorithm>
#include <minwindef.h>
#include <stdexcept>
#include <winuser.h>

std::wstring string_to_wstring(const std::string &str)
{
    if (str.empty())
        return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    if (size_needed <= 0)
    {
        throw std::runtime_error("MultiByteToWideChar failed");
    }
    std::wstring wstr(size_needed, 0);
    int result = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    if (result <= 0)
    {
        throw std::runtime_error("MultiByteToWideChar failed");
    }
    return wstr;
}

std::string wstring_to_string(const std::wstring &wstr)
{
    if (wstr.empty())
        return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0)
    {
        throw std::runtime_error("WideCharToMultiByte failed");
    }
    std::string str(size_needed, 0);
    int result = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, nullptr, nullptr);
    if (result <= 0)
    {
        throw std::runtime_error("WideCharToMultiByte failed");
    }
    return str;
}

const std::unordered_map<DWORD, std::wstring> &KeyCastMap()
{
    static const std::unordered_map<DWORD, std::wstring> keymap = {
        {VK_ESCAPE, L"<Esc>"},              // 27
        {VK_F1, L"<F1>"},                   // 112
        {VK_F2, L"<F2>"},                   // 113
        {VK_F3, L"<F3>"},                   // 114
        {VK_F4, L"<F4>"},                   // 115
        {VK_F5, L"<F5>"},                   // 116
        {VK_F6, L"<F6>"},                   // 117
        {VK_F7, L"<F7>"},                   // 118
        {VK_F8, L"<F8>"},                   // 119
        {VK_F9, L"<F9>"},                   // 120
        {VK_F10, L"<F10>"},                 // 121
        {VK_F11, L"<F11>"},                 // 122
        {VK_F12, L"<F12>"},                 // 123
        {VK_OEM_3, L"`"},                   // 192 (` ~)
        {0x31, L"1"},                       // 49 (1)
        {0x32, L"2"},                       // 50 (2)
        {0x33, L"3"},                       // 51 (3)
        {0x34, L"4"},                       // 52 (4)
        {0x35, L"5"},                       // 53 (5)
        {0x36, L"6"},                       // 54 (6)
        {0x37, L"7"},                       // 55 (7)
        {0x38, L"8"},                       // 56 (8)
        {0x39, L"9"},                       // 57 (9)
        {0x30, L"0"},                       // 48 (0)
        {VK_OEM_MINUS, L"-"},               // 189 (- _)
        {VK_OEM_PLUS, L"="},                // 187 (= +)
        {VK_BACK, L"<Backspace>"},          // 8 (Backspace)
        {VK_TAB, L"<Tab>"},                 // 9
        {0x51, L"q"},                       // 81 (Q)
        {0x57, L"w"},                       // 87 (W)
        {0x45, L"e"},                       // 69 (E)
        {0x52, L"r"},                       // 82 (R)
        {0x54, L"t"},                       // 84 (T)
        {0x59, L"y"},                       // 89 (Y)
        {0x55, L"u"},                       // 85 (U)
        {0x49, L"i"},                       // 73 (I)
        {0x4F, L"o"},                       // 79 (O)
        {0x50, L"p"},                       // 80 (P)
        {VK_OEM_4, L"["},                   // 219 ([ {)
        {VK_OEM_6, L"]"},                   // 221 (] })
        {VK_OEM_5, L"\\"},                  // 220 (\ |)
        {VK_CAPITAL, L"<Caps>"},            // 20 (Caps Lock)
        {0x41, L"a"},                       // 65 (A)
        {0x53, L"s"},                       // 83 (S)
        {0x44, L"d"},                       // 68 (D)
        {0x46, L"f"},                       // 70 (F)
        {0x47, L"g"},                       // 71 (G)
        {0x48, L"h"},                       // 72 (H)
        {0x4A, L"j"},                       // 74 (J)
        {0x4B, L"k"},                       // 75 (K)
        {0x4C, L"l"},                       // 76 (L)
        {VK_OEM_1, L";"},                   // 186 (; :)
        {VK_OEM_7, L"'"},                   // 222 (' ")
        {VK_RETURN, L"<Enter>"},            // 13 (Enter)
        {VK_LSHIFT, L"<LShift>"},           // 160
        {0x5A, L"z"},                       // 90 (Z)
        {0x58, L"x"},                       // 88 (X)
        {0x43, L"c"},                       // 67 (C)
        {0x56, L"v"},                       // 86 (V)
        {0x42, L"b"},                       // 66 (B)
        {0x4E, L"n"},                       // 78 (N)
        {0x4D, L"m"},                       // 77 (M)
        {VK_OEM_COMMA, L","},               // 188 (, <)
        {VK_OEM_PERIOD, L"."},              // 190 (. >)
        {VK_OEM_2, L"/"},                   // 191 (/ ?)
        {VK_RSHIFT, L"<RShift>"},           // 161
        {VK_LCONTROL, L"<LCtrl>"},          // 162
        {VK_LWIN, L"<LWin>"},               // 91
        {VK_LMENU, L"<LAlt>"},              // 164 (Left Alt)
        {VK_SPACE, L"<Space>"},             // 32
        {VK_RMENU, L"<RAlt>"},              // 165 (Right Alt)
        {VK_APPS, L"<App>"},                // 93 (Application Key)
        {VK_RCONTROL, L"<RCtrl>"},          // 163
        {VK_SNAPSHOT, L"<Prt>"},            // 44 (Print Screen)
        {VK_SCROLL, L"<Scr>"},              // 145 (Scroll Lock)
        {VK_PAUSE, L"<Pause>"},             // 19 (Pause/Break)
        {VK_INSERT, L"<Ins>"},              // 45
        {VK_HOME, L"<Hom>"},                // 36
        {VK_PRIOR, L"<Pgu>"},               // 33 (Page Up)
        {VK_DELETE, L"<Del>"},              // 46
        {VK_END, L"<End>"},                 // 35
        {VK_NEXT, L"<Pgd>"},                // 34 (Page Down)
        {VK_UP, L"↑"},                      // 38
        {VK_LEFT, L"←"},                    // 37
        {VK_DOWN, L"↓"},                    // 40
        {VK_RIGHT, L"→"},                   // 39
        {VK_MEDIA_PREV_TRACK, L"<MPrev>"},  // 177
        {VK_MEDIA_PLAY_PAUSE, L"<MPause>"}, // 179
        {VK_MEDIA_NEXT_TRACK, L"<MNext>"},  // 176
        {VK_NUMLOCK, L"<Num>"},             // 144
        {VK_VOLUME_MUTE, L"<VolMute>"},     // 173
        {VK_VOLUME_DOWN, L"<VolDown>"},     // 174
        {VK_VOLUME_UP, L"<VolUp>"},         // 175
    };
    return keymap;
}

FLOAT GetWindowScale()
{
    return GetWindowScale(nullptr);
}

FLOAT GetWindowScale(HWND hwnd)
{
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    return dpi / 96.0f;
}

int CALLBACK EnumFontFamExProc(ENUMLOGFONTEX *lpelfe, NEWTEXTMETRICEX *lpntme, DWORD FontType, LPARAM lParam)
{
    std::wstring *pFontName = reinterpret_cast<std::wstring *>(lParam);
    if (lpelfe->elfLogFont.lfFaceName == *pFontName)
    {
        // Found
        *pFontName = L"";
        return 0;
    }
    return 1;
}

bool IsFontExists(const std::wstring &fontName)
{
    HDC hdc = GetDC(NULL);
    if (!hdc)
    {
        // TODO: log error
        return false;
    }

    LOGFONT lf = {0};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, fontName.c_str());

    std::wstring targetFontName = fontName;

    EnumFontFamiliesEx(hdc, &lf, (FONTENUMPROC)EnumFontFamExProc, (LPARAM)&targetFontName, 0);
    ReleaseDC(NULL, hdc);

    return targetFontName.empty();
}

int GetTaskbarHeight()
{
    MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
    GetMonitorInfo(MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY), &monitorInfo);

    RECT fullRect = monitorInfo.rcMonitor;
    RECT workRect = monitorInfo.rcWork;

    int taskbarHeight = fullRect.bottom - workRect.bottom;

    return taskbarHeight;
}

int GetPrimaryMonitorHeight()
{
    MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
    GetMonitorInfo(MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY), &monitorInfo);

    int height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    return height;
}

void DrawToggle(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush, D2D1_RECT_F rect, bool on)
{
    if (!rt || !brush)
    {
        return;
    }
    const ThemeColors &theme = GetTheme();
    const float h = rect.bottom - rect.top;
    const float radius = h * 0.5f;
    D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(rect, radius, radius);
    brush->SetColor(on ? theme.accent : theme.toggleOff);
    rt->FillRoundedRectangle(pill, brush);

    const float knob = (std::max)(2.0f, h - 2.0f);
    const float x = on ? rect.right - 1.0f - knob : rect.left + 1.0f;
    const D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(x + knob * 0.5f, (rect.top + rect.bottom) * 0.5f), knob * 0.5f, knob * 0.5f);
    brush->SetColor(theme.knob);
    rt->FillEllipse(ellipse, brush);
}

bool HitToggle(const D2D1_RECT_F &rect, float x, float y)
{
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}