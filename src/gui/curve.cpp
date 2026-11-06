// ApoLite - response curve drawing for the editor.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "curve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace apolite::gui {

namespace {

constexpr double kMinHz = 20.0;
constexpr double kMaxHz = 20000.0;
constexpr int kGridDb = 6;    // gridline spacing
constexpr int kRangeDb = 12;  // the curve fills the height at +/- this many dB

COLORREF gridColour() { return RGB(60, 64, 72); }
COLORREF axisColour() { return RGB(96, 100, 110); }
COLORREF curveColour() { return RGB(80, 170, 255); }
COLORREF zeroColour() { return RGB(120, 124, 134); }
COLORREF textColour() { return RGB(150, 155, 165); }

// 20 Hz at the left edge, 20 kHz at the right.
double frequencyToX(double hz, const RECT& area) {
    const double t = std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
    return area.left + t * (area.right - area.left);
}

double dbToY(double db, const RECT& area) {
    const double mid = (area.top + area.bottom) * 0.5;
    const double half = (area.bottom - area.top) * 0.5;
    return mid - (db / kRangeDb) * half;
}

}  // namespace

void paintResponseCurve(HDC dc, const RECT& area, const VoiceSettings& settings,
                        double sampleRate) {
    if (area.right <= area.left || area.bottom <= area.top) return;

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);

    // Background.
    HBRUSH background = CreateSolidBrush(RGB(28, 30, 34));
    FillRect(dc, &area, background);
    DeleteObject(background);

    // Horizontal gridlines every 6 dB.
    HPEN gridPen = CreatePen(PS_SOLID, 1, gridColour());
    HPEN zeroPen = CreatePen(PS_SOLID, 1, zeroColour());
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, gridPen));
    for (int db = -kRangeDb; db <= kRangeDb; db += kGridDb) {
        const int y = static_cast<int>(std::lround(dbToY(db, area)));
        if (y <= area.top || y >= area.bottom) continue;
        SelectObject(dc, db == 0 ? zeroPen : gridPen);
        MoveToEx(dc, area.left, y, nullptr);
        LineTo(dc, area.right, y);
    }

    // Decade gridlines with labels.
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColour());
    const double decades[] = {100.0, 1000.0, 10000.0};
    const char* labels[] = {"100", "1k", "10k"};
    for (int i = 0; i < 3; ++i) {
        const int x = static_cast<int>(std::lround(frequencyToX(decades[i], area)));
        SelectObject(dc, gridPen);
        MoveToEx(dc, x, area.top, nullptr);
        LineTo(dc, x, area.bottom);
        RECT label{x + 3, area.bottom - 16, x + 40, area.bottom - 2};
        DrawTextA(dc, labels[i], -1, &label, DT_LEFT | DT_SINGLELINE);
    }

    // Decibel labels at the left edge.
    for (int db = -kRangeDb; db <= kRangeDb; db += kGridDb) {
        const int y = static_cast<int>(std::lround(dbToY(db, area)));
        if (y <= area.top || y >= area.bottom - 2) continue;
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%+d", db);
        RECT label{area.left + 3, y + 1, area.left + 34, y + 15};
        DrawTextA(dc, text, -1, &label, DT_LEFT | DT_SINGLELINE);
    }

    // The response itself, one point per pixel column.
    HPEN curvePen = CreatePen(PS_SOLID, 2, curveColour());
    SelectObject(dc, curvePen);
    const int width = area.right - area.left;
    bool started = false;
    for (int px = 0; px <= width; ++px) {
        const double t = static_cast<double>(px) / std::max(1, width);
        const double hz = kMinHz * std::pow(kMaxHz / kMinHz, t);
        const double db = eqMagnitudeDb(settings, hz, sampleRate);
        const int x = area.left + px;
        const int y = static_cast<int>(std::lround(dbToY(db, area)));
        if (!started) {
            MoveToEx(dc, x, y, nullptr);
            started = true;
        } else {
            LineTo(dc, x, y);
        }
    }

    // Frame.
    HPEN framePen = CreatePen(PS_SOLID, 1, axisColour());
    SelectObject(dc, framePen);
    HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, hollow));
    Rectangle(dc, area.left, area.top, area.right, area.bottom);
    SelectObject(dc, oldBrush);

    SelectObject(dc, oldPen);
    DeleteObject(framePen);
    DeleteObject(curvePen);
    DeleteObject(zeroPen);
    DeleteObject(gridPen);
    RestoreDC(dc, saved);
}

}  // namespace apolite::gui
