#include "window.h"
#include "config.h"
#include "d2d.h"
#include "globals.h"
#include "hook.h"
#include "resource.h"
#include "settings.h"
#include "theme.h"
#include "tray_menu.h"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <dwmapi.h>

namespace
{
constexpr int kMinOverlayPx = 32;
constexpr int kWorkMarginPx = 8;

UINT TaskbarCreatedMessage()
{
    static const UINT message = RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

void PlaceOverlayOnMonitor(HWND hwnd)
{
    HMONITOR hMonitor = MonitorFromWindow(hwnd ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(hMonitor, &monitorInfo);

    float dipW = 32.0f;
    float dipH = 32.0f;
    GetOverlayContentDipSize(dipW, dipH);
    const float scale = GetWindowScale(hwnd);
    int windowWidth = (std::max)(kMinOverlayPx, static_cast<int>(std::ceil(dipW * scale)));
    int windowHeight = (std::max)(kMinOverlayPx, static_cast<int>(std::ceil(dipH * scale)));

    const int workLeft = monitorInfo.rcWork.left;
    const int workTop = monitorInfo.rcWork.top;
    const int workRight = monitorInfo.rcWork.right;
    const int workBottom = monitorInfo.rcWork.bottom;
    int windowX = workRight - windowWidth - kWorkMarginPx;
    int windowY = workBottom - windowHeight - kWorkMarginPx;
    const auto &pos = ::KeycastConfig.position;
    if (pos == L"bottom-left")
    {
        windowX = workLeft + kWorkMarginPx;
        windowY = workBottom - windowHeight - kWorkMarginPx;
    }
    else if (pos == L"top-right")
    {
        windowX = workRight - windowWidth - kWorkMarginPx;
        windowY = workTop + kWorkMarginPx;
    }
    else if (pos == L"top-left")
    {
        windowX = workLeft + kWorkMarginPx;
        windowY = workTop + kWorkMarginPx;
    }

    ::WindowArea = {windowX, windowY, windowX + windowWidth, windowY + windowHeight};
    if (hwnd)
    {
        RECT current{};
        GetWindowRect(hwnd, &current);
        if (current.left != windowX || current.top != windowY || current.right - current.left != windowWidth || current.bottom - current.top != windowHeight)
        {
            SetWindowPos(hwnd, HWND_TOPMOST, windowX, windowY, windowWidth, windowHeight, SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
    }
}
} // namespace

HWND CreateTransparentWindow(HINSTANCE hInstance)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DirectKeycastClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClass(&wc))
    {
        return nullptr;
    }

    const int windowWidth = 64;
    const int windowHeight = 48;
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfo(MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY), &monitorInfo);
    const int windowX = monitorInfo.rcWork.right - windowWidth - kWorkMarginPx;
    const int windowY = monitorInfo.rcWork.bottom - windowHeight - kWorkMarginPx;
    ::WindowArea = {windowX, windowY, windowX + windowWidth, windowY + windowHeight};

    const UINT exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    HWND hwnd = CreateWindowEx(exStyle, wc.lpszClassName, L"DirectKeycastWindow", WS_POPUP, windowX, windowY, windowWidth, windowHeight, nullptr, nullptr, hInstance, nullptr);
    if (hwnd)
    {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        MARGINS mar = {-1};
        DwmExtendFrameIntoClientArea(hwnd, &mar);
    }
    return hwnd;
}

void UpdateOverlayWindowPlacement(HWND hwnd)
{
    PlaceOverlayOnMonitor(hwnd);
}

bool IsPointInWindow(HWND hwnd, POINT screenPt)
{
    if (!hwnd || !IsWindowVisible(hwnd))
    {
        return false;
    }
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    return PtInRect(&rc, screenPt) != FALSE;
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        InitD2DRenderTarget(hwnd);
        InitGlobalConfigWithD2D();
        return 0;

    case WM_PAINT:
        OnPaint(hwnd);
        ValidateRect(hwnd, nullptr);
        return 0;

    case WM_DISPLAYCHANGE:
        UpdateOverlayWindowPlacement(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_SIZE:
        ResizeOverlayRenderTarget(hwnd);
        return 0;

    case WM_DPICHANGED: {
        RecreateOverlayTextFormat();
        InitGlobalConfigWithD2D();
        UpdateOverlayWindowPlacement(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_INPUT:
        HandleRawMouseInput(reinterpret_cast<HRAWINPUT>(lParam));
        return 0;

    case WM_APP_LLKEY:
        HandlePostedKeyEvent(wParam, static_cast<DWORD>(lParam));
        return 0;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA)
        {
            UpdateOverlayWindowPlacement(hwnd);
        }
        // Only "follow system" reacts to the OS light/dark switch.
        if (lParam && lstrcmpiW(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0 && ::KeycastConfig.themeMode == L"system")
        {
            RefreshTheme();
            InvalidateOverlayCache();
            InvalidateRect(hwnd, nullptr, FALSE);
            RefreshTrayMenuTheme();
            RefreshSettingsWindow();
        }
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;

    case WM_APP_HIDE_OVERLAY:
        ShowWindow(hwnd, SW_HIDE);
        ::OverlayHistory.clear();
        InvalidateOverlayCache();
        return 0;

    case WM_APP_SHOW_OVERLAY:
        if (::KeycastConfig.enabled)
        {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
        return 0;

    case WM_APP_HIDE_TRAY_MENU:
        HideTrayMenu();
        return 0;

    case WM_APP_OPEN_SETTINGS:
        HideTrayMenu();
        OpenSettingsWindow(false);
        return 0;

    case WM_APP_OPEN_ABOUT:
        HideTrayMenu();
        OpenSettingsWindow(true);
        return 0;

    case WM_TRAYICON: {
        const UINT notify = LOWORD(lParam);
        if (notify == WM_RBUTTONUP || notify == WM_CONTEXTMENU)
        {
            ShowTrayMenu(hwnd);
        }
        else if (notify == WM_LBUTTONUP)
        {
            OpenSettingsWindow(false);
        }
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam))
        {
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        case ID_TRAY_SETTINGS:
            OpenSettingsWindow(false);
            break;
        case ID_TRAY_ABOUT:
            OpenSettingsWindow(true);
            break;
        default:
            break;
        }
        break;
    }

    default:
        // Explorer restarted: the notification area is new and our icon is gone.
        if (message == TaskbarCreatedMessage() && message != 0)
        {
            AddTrayIcon(hwnd);
            return 0;
        }
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

void AddTrayIcon(HWND hwnd)
{
    nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!nid.hIcon)
    {
        nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(nid.szTip, L"DirectKeycast");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &nid);
}
