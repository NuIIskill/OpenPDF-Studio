#pragma once

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QRectF>
#include <QString>

struct NoteData
{
    QString   id;
    QString   title;
    QString   text;
    int       page { -1 };
    QRectF    pdfBounds;
    QDateTime modified;
    bool      pinned { false };
    QList<QRectF> textRects;
    QString   quote;
    QColor    color;
    QColor    originalColor;
    bool      existing { false };
    bool      removed { false };
    QString   originalId;
    QString   originalTitle;
    QString   originalText;
    QRectF    originalBounds;
    bool      originalPinned { false };

    bool operator==(const NoteData &) const = default;
};
