#pragma once

#include <QList>
#include <QString>

/// One entry in a PDF document's outline tree.
struct PdfBookmark
{
    QString            title;
    int                page { -1 };
    bool               expanded { true };
    QList<PdfBookmark> children;
    /// False when the entry performs an action the editor cannot preserve.
    bool               supported { true };

    bool operator==(const PdfBookmark &other) const
    {
        if (title != other.title || page != other.page
                || expanded != other.expanded
                || supported != other.supported
                || children.size() != other.children.size())
            return false;
        for (qsizetype i = 0; i < children.size(); ++i)
            if (!(children[i] == other.children[i])) return false;
        return true;
    }
};
