#pragma once

#include "engine/edit/TextLayout.hpp"

#include <QList>
#include <QString>

/// Lays the edited text of a scan out as its paragraph flows: each line where the scan has it, its words passing to the next line or coming up from it as a text editor breaks them.
namespace ScanFlow {

QList<TextLayout::Line> lines(const QString &text, const TextLayout::Params &params, double sizePt,
                              const QList<TextLayout::OriginalLine> &originals,
                              const TextLayout::Advance &advance, double low, double high);

}
