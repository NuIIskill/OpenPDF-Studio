#pragma once

#include <QHash>
#include <QSet>
#include <QString>

/// Puts back the text PDFium cannot rewrite: Type3 fonts and character or word spacing.
namespace KeptText {

struct Kept {
    int       total { 0 };
    QSet<int> indices;
    bool      type3Only { true };
};

bool restore(const QString &source, const QString &output,
             const QHash<int, Kept> &pages);

}
