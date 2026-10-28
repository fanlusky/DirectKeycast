#include "config.h"
#include "d2d.h"
#include "globals.h"
#include "hook.h"
#include "msimeui/Application.h"
#include "settings.h"
#include "tray_menu.h"
#include "window.h"
#include <winuser.h>

namespace
{
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\DirectKeycast.SingleInstance";
constexpr wchar_t kOverlayWindowClass[] = L"DirectKeycastClass";

// A second launch brings up the running instance's settings instead of starting another hook.
void ActivateRunningInstance()
{
    if (HWND existing = FindWindowW(kOverlayWindowClass, nullptr))
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(existing, &pid);
        AllowSetForegroundWindow(pid);
        PostMessageW(existing, WM_APP_OPEN_SETTINGS, 0, 0);
    }
}

void RegisterHotkeys()
{
    ::ExitHotkeyRegistered = RegisterHotKey(nullptr, HOTKEY_ID_EXIT, HOTKEY_MODIFIERS, HOTKEY_VK_EXIT) != FALSE;
    ::ToggleHotkeyRegistered = RegisterHotKey(nullptr, HOTKEY_ID_TOGGLE, HOTKEY_MODIFIERS, HOTKEY_VK_TOGGLE) != FALSE;
}

void UnregisterHotkeys()
{
    UnregisterHotKey(nullptr, HOTKEY_ID_EXIT);
    UnregisterHotKey(nullptr, HOTKEY_ID_TOGGLE);
}
} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        ActivateRunningInstance();
        CloseHandle(instanceMutex);
        return 0;
    }

    const bool msimeuiReady = msimeui::Application::Initialize();
    if (!msimeuiReady)
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    // Not fatal: the tray menu can still toggle the overlay and exit.
    RegisterHotkeys();

    ::InitializeConfig();

    if (!InitD2DAndDWrite())
    {
        return -1;
    }

    HWND hwnd = CreateTransparentWindow(hInstance);
    if (!hwnd)
    {
        return -1;
    }
    D2DHwnd = hwnd;

    CreateTrayMenuWindow(hInstance);
    ShowWindow(hwnd, SW_HIDE);
    AddTrayIcon(hwnd);
    StartInputCapture(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID_EXIT)
        {
            break;
        }
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID_TOGGLE)
        {
            SetOverlayEnabled(!::KeycastConfig.enabled);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotkeys();
    StopInputCapture();
    KillTimer(hwnd, OVERLAY_ANIMATION_TIMER);
    CloseSettingsWindow();
    DestroyTrayMenuWindow();
    RemoveTrayIcon();
    ReleaseOverlayD2D();
    if (msimeuiReady)
    {
        msimeui::Application::Shutdown();
    }
    if (instanceMutex)
    {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
    return (int)msg.wParam;
}
