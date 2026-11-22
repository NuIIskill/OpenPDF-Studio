#include "engine/edit/ScanLike.hpp"

#include <QPainter>

#include <cmath>
#include <vector>

namespace ScanLike {

namespace {

// Every shape is held in one frame, stretched to it: so a face's wider or narrower letters compare.
constexpr int kWide = 20, kHigh = 28;
constexpr int kInk = 48;

// A frame's tones about their mean, at length one: two of them multiplied tell how alike they are.
std::vector<float> levelled(const QImage &frame)
{
    std::vector<float> v(size_t(kWide) * size_t(kHigh));
    double mean = 0.0;
    for (int y = 0; y < kHigh; ++y)
        for (int x = 0; x < kWide; ++x) mean += v[size_t(y) * kWide + size_t(x)] = frame.constScanLine(y)[x];
    mean /= double(v.size());
    double length = 0.0;
    for (float &t : v) { t -= float(mean); length += double(t) * double(t); }
    length = std::sqrt(length);
    for (float &t : v) t = length > 0.0 ? float(t / length) : 0.0f;
    return v;
}

QImage framed(const QPainterPath &shape)
{
    QImage frame(kWide, kHigh, QImage::Format_Grayscale8);
    frame.fill(Qt::black);
    const QRectF box = shape.boundingRect();
    if (box.width() <= 0.0 || box.height() <= 0.0) return frame;
    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(kWide / box.width(), kHigh / box.height());
    p.translate(-box.topLeft());
    p.fillPath(shape, Qt::white);
    return frame;
}

}

QList<double> scores(const QImage &ink, const QList<QPainterPath> &shapes)
{
    if (ink.isNull() || shapes.isEmpty()) return {};
    const QImage from = ink.convertToFormat(QImage::Format_ARGB32);
    const int w = from.width(), h = from.height();
    QImage alpha(w, h, QImage::Format_Grayscale8);
    std::vector<double> columns(size_t(w), 0.0);
    double all = 0.0;
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(from.constScanLine(y));
        uchar *to = alpha.scanLine(y);
        for (int x = 0; x < w; ++x) {
            to[x] = uchar(qAlpha(row[x]));
            if (to[x] > kInk) { columns[size_t(x)] += to[x]; all += to[x]; }
        }
    }
    // The letter is the piece's one run of inked columns: ink beside it is a neighbour's or a smudge.
    const int join = qMax(2, h / 12);
    int bestFrom = -1, bestTo = -1;
    double most = 0.0;
    for (int x = 0; x < w;) {
        if (columns[size_t(x)] <= 0.0) { ++x; continue; }
        int to = x, blank = 0;
        double mass = 0.0;
        for (int k = x; k < w && blank <= join; ++k) {
            if (columns[size_t(k)] > 0.0) { to = k; blank = 0; mass += columns[size_t(k)]; }
            else ++blank;
        }
        if (mass > most) { most = mass; bestFrom = x; bestTo = to; }
        x = to + 1;
    }
    if (bestFrom < 0 || all - most > all * 0.08) return {};
    int top = h, bottom = -1;
    for (int y = 0; y < h; ++y)
        for (int x = bestFrom; x <= bestTo; ++x)
            if (alpha.constScanLine(y)[x] > kInk) { top = qMin(top, y); bottom = qMax(bottom, y); }
    if (bottom - top < 5 || bestTo - bestFrom < 1) return {};
    const std::vector<float> seen = levelled(alpha.copy(bestFrom, top, bestTo - bestFrom + 1, bottom - top + 1)
                                                 .scaled(kWide, kHigh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    QList<double> out;
    for (const QPainterPath &path : shapes) {
        const std::vector<float> shape = levelled(framed(path));
        double alike = 0.0;
        for (size_t k = 0; k < seen.size(); ++k) alike += double(seen[k]) * double(shape[k]);
        out.append(alike);
    }
    return out;
}

int shown(const QImage &ink, const QList<QPainterPath> &shapes)
{
    const QList<double> alike = scores(ink, shapes);
    if (alike.size() < 2) return -1;
    int best = -1;
    double first = -1.0, second = -1.0;
    for (int i = 0; i < alike.size(); ++i) {
        if (alike.at(i) > first) { second = first; first = alike.at(i); best = i; }
        else if (alike.at(i) > second) second = alike.at(i);
    }
    // Clearly one of them, and clearly that one before the next.
    constexpr double kAlike = 0.5, kAhead = 0.04;
    return first >= kAlike && first - second >= kAhead ? best : -1;
}

double alike(const QPainterPath &a, const QPainterPath &b)
{
    const std::vector<float> first = levelled(framed(a)), second = levelled(framed(b));
    double sum = 0.0;
    for (size_t k = 0; k < first.size(); ++k) sum += double(first[k]) * double(second[k]);
    return sum;
}

}
