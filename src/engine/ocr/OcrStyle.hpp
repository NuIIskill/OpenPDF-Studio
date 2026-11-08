#pragma once

#include "engine/ocr/OcrEngine.hpp"

#include <QImage>
#include <QList>

#include <functional>

/// Sets what OCR read in the face each line is printed in and keeps the reading and the letter boxes that cover the scan best.
namespace OcrStyle {

void settle(const QImage &gray, qreal scale, QList<OcrEngine::Line> &lines,
            const std::function<bool()> &cancelled = {},
            const std::function<void(qreal done)> &progress = {});

}
