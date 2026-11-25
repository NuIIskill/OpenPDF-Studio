#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>

#include <utility>
#include <vector>

/// Works on a scan's pixels: takes ink off its paper and finds how it printed its text.
namespace ScanPaper {

struct Plane {
    int w { 0 }, h { 0 };
    std::vector<float> v;

    bool isValid() const { return w > 0 && h > 0; }
    float at(int x, int y) const { return v[size_t(y) * size_t(w) + size_t(x)]; }
};

/// How a scan printed its text: blurred, its strokes thicker or thinner than the face draws them, its ink faded, or in black and white.
struct Printed {
    bool   found    { false };
    double blurPx   { 0.7 };
    double gainPx   { 0.0 };
    double contrast { -1.0 };
    bool   bilevel  { false };
    double cut      { 0.5 };
};

inline constexpr double kGainsPx[] = { -1.6, -1.2, -0.8, -0.4, 0.0, 0.4, 0.8, 1.2, 1.7 };
inline constexpr int kPlainGain = 4;

Plane grayOf(const QImage &image);

void blur(Plane &plane, double sigma);

bool bilevel(const Plane &gray);

QImage withoutInk(const QImage &around, const QRect &area, int reach, const QPoint &at, uint seed, const QList<QRect> &rules = {});

QImage inkAlone(const QImage &piece, const QRect &area, const QPoint &at = {}, const QList<QRect> &rules = {});

QList<std::pair<int, int>> inkRuns(const QImage &word, int above = 0, int below = 0, const QPoint &at = {},
                                   const QList<QRect> &rules = {});

Printed printedLike(const QList<Plane> &scans, const QList<QList<Plane>> &drawn, int reach);

QImage printed(const Plane &ink, const Printed &like, const QColor &fill, const QColor &paper, QRect *inked);

QImage tinted(const QImage &ink, const QColor &colour, double opacity);

}
