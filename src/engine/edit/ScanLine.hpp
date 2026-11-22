#pragma once

#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <utility>
#include <vector>

/// Measures a scanned line for ScanFont: binarized, levelled and parted into its words.
namespace ScanLine {

/// The pixels of a line darker than its cut.
struct Plane {
    int w { 0 }, h { 0 };
    std::vector<uint8_t> on;
    bool at(int x, int y) const { return on[size_t(y) * size_t(w) + size_t(x)] != 0; }
};

/// A scanned line binarized halfway between paper and ink, with the columns its words cover.
struct Scan {
    Plane ink;
    std::vector<float> dark;
    QPoint at;
    int left { 0 }, right { -1 }, top { 0 }, bottom { -1 };
    int baseline { -1 };
    int paper { 255 }, inkLevel { 0 };
    QList<std::pair<int, int>> words;
    QList<std::pair<int, int>> foreign;
    double tilt { 0.0 }, tiltAt { 0.0 };
    bool tilted { false };
    bool hinted { false };
    bool isValid() const { return right > left && bottom > top; }
    // How far below its level a point of the line lies on the page, at a column of the scan.
    double drop(double x) const { return (x - tiltAt) * tilt; }
};

bool noWord(const QString &word);

Scan scanOf(const QImage &img, const QRect &px, QStringList &words, int textTop, int textBottom,
            QList<double> starts = {}, double baseRow = -1.0, double tilt = 0.0);

}
