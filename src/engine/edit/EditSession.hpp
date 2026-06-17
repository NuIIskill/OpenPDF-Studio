#pragma once

#include "TextBlock.hpp"
#include <QList>
#include <QImage>
#include <QString>

#ifdef HAVE_QT_PDF
#  include <QPdfDocument>
#endif
#ifdef HAVE_POPPLER
#  include <poppler/qt6/poppler-qt6.h>
#endif

// Stores all pending text edits for one document.
// Knows how to apply them to a QImage and how to write a final PDF.
class EditSession
{
public:
    EditSession() = default;

    void addEdit(int page, const QRectF &pdfBounds, const QString &newText);
    void removeEdit(int page, const QRectF &pdfBounds);
    void clear();

    bool hasEditsOnPage(int page) const;
    bool hasAnyEdits()            const { return !m_edits.isEmpty(); }

    // Paint replacements onto an already-rendered QImage.
    // scale = PDF-point-to-pixel factor used when rendering.
    void applyToImage(int page, QImage &img, qreal scale) const;

#if defined(HAVE_QT_PDF) && defined(HAVE_QT_PRINT)
    bool saveToFile(const QString &path, QPdfDocument *doc, int pageCount) const;
#endif
#if defined(HAVE_POPPLER) && defined(HAVE_QT_PRINT)
    bool saveToFile(const QString &path, Poppler::Document *doc, int pageCount) const;
#endif

private:
    struct Edit {
        int     page;
        QRectF  pdfBounds;
        QString newText;
    };

    static void paintEdit(QPainter &p, const Edit &e, qreal scale);

    QList<Edit> m_edits;
};
