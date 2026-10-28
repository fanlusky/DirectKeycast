#include "d2d.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

// Layout tests do not create a desktop overlay window.
void UpdateOverlayWindowPlacement(HWND)
{
}

namespace
{
void Check(bool ok, const char *message)
{
    if (!ok)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void CheckRows(float fontSize)
{
    KeycastConfig.fontSize = fontSize;
    RecreateOverlayTextFormat();
    const float unit = fontSize + 28.0f;
    const float threeKeyWidth = unit * 3 + 14 * 2 + 10;
    std::vector<OverlayStroke> history(3, {{L"A"}});
    OverlayLayout layout;
    KeycastConfig.rows = 5;
    layout.Build(history, threeKeyWidth);
    Check(layout.RowCount() == 1, "An exactly full row must not wrap early");
    history.push_back({{L"B"}});
    layout.Build(history, threeKeyWidth);
    Check(layout.RowCount() == 2, "The next key must start a new bottom row");
    Check(std::abs(layout.Height() - (unit * 2 + 20)) < 0.01f, "Row height must include the inter-row gap");

    history.assign(16, {{L"A"}});
    for (int rows = 1; rows <= 5; ++rows)
    {
        KeycastConfig.rows = rows;
        layout.Build(history, threeKeyWidth);
        Check(layout.RowCount() == rows, "The selected row limit must be respected");
        Check(layout.DiscardedStrokeCount() == static_cast<size_t>((6 - rows) * 3), "Only complete oldest rows may be discarded");
        const float height = layout.Height();
        auto retained = history;
        retained.erase(retained.begin(), retained.begin() + layout.DiscardedStrokeCount());
        layout.Build(retained, threeKeyWidth);
        Check(layout.DiscardedStrokeCount() == 0 && std::abs(layout.Height() - height) < 0.01f, "Pruning expired rows must preserve the remaining row boundaries");
        Check(layout.Width() <= threeKeyWidth + 0.01f, "Rows must fit the available width");
    }

    // A shortcut must move as one group instead of splitting its modifier caps.
    KeycastConfig.rows = 5;
    const std::vector<OverlayStroke> chord = {{{L"LCtrl", L"C"}}};
    layout.Build(chord, 10000);
    const float chordWidth = layout.Width();
    layout.Build({{{L"A"}}, chord[0]}, chordWidth);
    Check(layout.RowCount() == 2, "A chord that does not fit must move intact to a new row");
    layout.Build(chord, 40);
    Check(layout.RowCount() == 1 && layout.Width() <= 40, "An oversized chord must scale intact");
    layout.Build({}, threeKeyWidth);
    Check(layout.RowCount() == 0 && layout.Width() == 0 && layout.Height() == 0, "Empty history must clear the layout");
}
void CheckRowTiming()
{
    KeycastConfig.rows = 5;
    KeycastConfig.fadeMs = 1000;
    KeycastConfig.fontSize = 24;
    RecreateOverlayTextFormat();
    Check(GetOverlayRowOpacity(1000, 2000, 1000) == 1.0f, "A row must stay opaque for its configured wait");
    Check(std::abs(GetOverlayRowOpacity(1000, 2150, 1000) - 0.5f) < 0.001f, "Fade opacity must decrease linearly");
    Check(GetOverlayRowOpacity(1000, 2300, 1000) == 0.0f, "A row must be fully transparent at the fade deadline");
    Check(GetOverlayRowOpacity(3000, 2000, 1000) == 1.0f, "Clock bounds must not underflow");

    OverlayHistory = {{{L"A"}, 1, false, 1000, true}, {{L"B"}, 1, false, 1800, true}, {{L"C"}, 1, false, 2200, true}};
    InvalidateOverlayCache();
    TickOverlayAnimation(nullptr, 2150);
    Check(OverlayHistory.size() == 3 && OverlayHistory[0].updatedAt == 1000, "Moving a row upward must not reset its deadline");
    TickOverlayAnimation(nullptr, 2300);
    Check(OverlayHistory.size() == 2 && OverlayHistory.front().keys[0] == L"B", "Only the expired row must be removed");
    OverlayLayout layout;
    layout.Build(OverlayHistory, 2000);
    Check(layout.RowCount() == 2, "Surviving rows must stay separate after expiration");
    Check(layout.Rows()[0].updatedAt == 1800 && layout.Rows()[1].updatedAt == 2200, "Every row must retain its own clock");

    // New input refreshes only its own row; the row above keeps fading.
    OverlayHistory.push_back({{L"D"}, 1, false, 2850, false});
    InvalidateOverlayCache();
    TickOverlayAnimation(nullptr, 2950);
    Check(OverlayHistory.size() == 3, "New bottom-row input must not prematurely remove a fading older row");
    layout.Build(OverlayHistory, 2000);
    Check(layout.Rows()[0].updatedAt == 1800 && layout.Rows()[1].updatedAt == 2850, "Input must update only the current row timer");
    TickOverlayAnimation(nullptr, 3100);
    Check(OverlayHistory.size() == 2 && OverlayHistory.front().keys[0] == L"C", "Input on the bottom row must not prolong the older row");
    TickOverlayAnimation(nullptr, 4150);
    Check(OverlayHistory.empty(), "All rows must eventually expire");
    TickOverlayAnimation(nullptr, 5000);
    Check(OverlayHistory.empty(), "An empty animation tick must be safe");

    OverlayHistory = {{{L"A"}, 1, false, 1000, true}};
    InvalidateOverlayCache();
    float width1, height1, width2, height2;
    GetOverlayContentDipSize(width1, height1);
    OverlayHistory.assign(100, {{L"B"}, 1, false, 1500});
    InvalidateOverlayCache();
    GetOverlayContentDipSize(width2, height2);
    Check(width1 == width2 && height1 == height2, "Typing must not resize the transparent window surface");
    OverlayHistory.clear();
    InvalidateOverlayCache();
}
} // namespace

int main()
{
    fontName = L"Cascadia Mono";
    Check(InitD2DAndDWrite(), "DirectWrite initialization failed");
    for (float size : {12.0f, 24.0f, 32.0f})
        CheckRows(size);
    CheckRowTiming();
    ReleaseOverlayD2D();
    std::cout << "PASS: overflow, 1-5 rows, complete-row pruning, intact chords, 12/24/32 px fonts, independent row clocks, linear fade, and stable surface size\n";
}
