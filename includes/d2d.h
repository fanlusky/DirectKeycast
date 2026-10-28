#pragma once
#include "globals.h"
#include "main.h"
#include <vector>
#include <wrl/client.h>

struct OverlayRow
{
    size_t begin, end;
    ULONGLONG updatedAt;
};
float GetOverlayRowOpacity(ULONGLONG updatedAt, ULONGLONG now, int waitMs);
void TickOverlayAnimation(HWND hwnd, ULONGLONG now);

// Shared by the desktop overlay and the settings preview.
class OverlayLayout
{
  public:
    void Build(const std::vector<OverlayStroke> &history, float maxWidth);
    void Draw(ID2D1RenderTarget *target, ID2D1SolidColorBrush *brush, D2D1_POINT_2F origin, ULONGLONG now = 0) const;
    const std::vector<OverlayRow> &Rows() const
    {
        return rows_;
    }
    size_t DiscardedStrokeCount() const
    {
        return discardedStrokes_;
    }
    size_t RowCount() const
    {
        return rowCount_;
    }
    float Width() const
    {
        return width_;
    }
    float Height() const
    {
        return height_;
    }

  private:
    struct Cap
    {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        float x, y, width, textWidth, textHeight;
        size_t row;
        size_t group;
        bool active, home;
    };
    std::vector<Cap> caps_;
    std::vector<OverlayRow> rows_;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float scale_ = 1.0f;
    size_t discardedStrokes_ = 0;
    size_t rowCount_ = 0;
};

// Per-segment colors of the rainbow overlay text (shared with the settings preview).
std::vector<D2D1_COLOR_F> GetOverlayTextColors();

bool InitD2DAndDWrite();
bool InitD2DRenderTarget(HWND hwnd);
bool InitGlobalConfigWithD2D();
void OnPaint(HWND hwnd);
void RecreateOverlayTextFormat();
void InvalidateOverlayCache();
void ReleaseOverlayCache();
void ResizeOverlayRenderTarget(HWND hwnd);
void ReleaseOverlayD2D();
void RequestOverlayRepaint();
void GetOverlayContentDipSize(float &width, float &height);
