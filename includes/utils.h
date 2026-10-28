#pragma once
#include <d2d1.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

std::wstring string_to_wstring(const std::string &str);
std::string wstring_to_string(const std::wstring &wstr);

const std::unordered_map<DWORD, std::wstring> &KeyCastMap();

FLOAT GetWindowScale();
FLOAT GetWindowScale(HWND hwnd);

bool IsFontExists(const std::wstring &fontName);

int GetTaskbarHeight();
int GetPrimaryMonitorHeight();

void DrawToggle(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush, D2D1_RECT_F rect, bool on);
bool HitToggle(const D2D1_RECT_F &rect, float x, float y);
