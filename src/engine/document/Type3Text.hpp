#pragma once

#include <QHash>
#include <QSet>
#include <QString>

/// Puts back the Type3 text that PDFium drops when it rewrites a page.
namespace Type3Text {

struct Kept {
    int       total { 0 };
    QSet<int> indices;
};

bool restore(const QString &source, const QString &output,
             const QHash<int, Kept> &pages);

}
