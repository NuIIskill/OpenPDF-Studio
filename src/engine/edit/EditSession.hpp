#pragma once

#include "engine/edit/ContentMap.hpp"
#include "engine/edit/TextBlock.hpp"
#include "engine/edit/TextBoxProperties.hpp"
#include "engine/edit/TextLayout.hpp"
#include <QColor>
#include <QList>
#include <QPair>
#include <QSet>
#include <QImage>
#include <QString>
#include <QVector>

/// Stores pending content and annotation edits for one document session.
class EditSession
{
public:
    /// Image overlay embedded in the document (inserted via image tool).
    struct ImageEdit {
        int    page;
        QRectF pdfBounds;
        QImage image;
    };

    /// A change to an image object of the original page.
    struct ImageChange {
        int        page { -1 };
        QList<int> key;
        QRectF     from;
        QRectF     to;
        int        quarterTurns { 0 };
        QImage     replacement;
        bool       removed { false };

        bool operator==(const ImageChange &) const = default;
    };

    struct DrawStroke {
        int              page { -1 };
        QVector<QPointF> points;
        QColor           color { QColor(QStringLiteral("#145DFF")) };
        qreal            widthPt { 2.5 };

        bool operator==(const DrawStroke &) const = default;
    };

    struct LinkEdit {
        int     page { -1 };
        QRectF  originalBounds;
        QRectF  pdfBounds;
        QString originalUrl;
        QString url;
        QList<QRectF> textRects;
        bool    existing { false };
        bool    removed  { false };
        bool    colorText { false };

        bool operator==(const LinkEdit &) const = default;
    };

    struct NoteEdit {
        int     page { -1 };
        QString id;
        QString originalId;
        QString title;
        QString text;
        QString originalText;
        QRectF  pdfBounds;
        QRectF  originalBounds;
        bool    existing { false };
        bool    removed  { false };
        bool    pinned { false };
        bool    originalPinned { false };

        bool operator==(const NoteEdit &) const = default;
    };

    /// Stores one pending text edit.
    struct Edit {
        int     page       { -1 };
        int     sourcePage { -1 };
        QRectF  pdfBounds;
        QRectF  sourceRect;
        QString newText;

        double  fontSizePt { 0.0 };

        QPointF textOriginOffset;
        bool    hasTextOrigin { false };

        double  lineSpacingPt { 0.0 };

        QString originalText;
        QColor  textColor;
        QColor  bgColor;
        QString fontFamily;
        bool    bold        { false };
        bool    italic      { false };
        bool    underline   { false };

        bool    familyChanged { false };
        bool    boldChanged   { false };
        bool    italicChanged { false };

        bool    sizeChanged { false };
        double  baseSizePt  { 0.0 };

        bool    colorChanged { false };
        bool    laid { false };
        bool    scanned { false };

        QString formField;

        QList<QRectF> eraseRects;
        TextBoxProperties box;
        QList<TextLayout::OriginalLine> originalLines;
        QList<TextLayout::Source> sources;

        bool operator==(const Edit &o) const {
            return page == o.page && sourcePage == o.sourcePage && pdfBounds == o.pdfBounds &&
                   sourceRect == o.sourceRect && newText == o.newText &&
                   qFuzzyCompare(fontSizePt + 1.0, o.fontSizePt + 1.0) &&
                   textOriginOffset == o.textOriginOffset &&
                   hasTextOrigin == o.hasTextOrigin &&
                   qFuzzyCompare(lineSpacingPt + 1.0, o.lineSpacingPt + 1.0) &&
                   textColor == o.textColor && bgColor == o.bgColor &&
                   fontFamily == o.fontFamily && bold == o.bold &&
                   italic == o.italic && underline == o.underline &&
                   familyChanged == o.familyChanged && boldChanged == o.boldChanged &&
                   italicChanged == o.italicChanged &&
                   qFuzzyCompare(baseSizePt + 1.0, o.baseSizePt + 1.0) &&
                   sizeChanged == o.sizeChanged && colorChanged == o.colorChanged &&
                   scanned == o.scanned &&
                   formField == o.formField && eraseRects == o.eraseRects &&
                   box == o.box && originalLines == o.originalLines && sources == o.sources;
        }
        bool operator!=(const Edit &o) const { return !(*this == o); }
    };

    EditSession() = default;

    void addEdit(Edit e) { m_edits.append(std::move(e)); }

    void addEdit(int page, const QRectF &pdfBounds, const QString &newText,
                 double fontSizePt = 0.0, const QColor &color = QColor(),
                 const QRectF &sourceRect = QRectF(),
                 const QColor &bgColor = QColor());
    void removeEdit(int page, const QRectF &pdfBounds);

    void removeAllAt(int page, const QRectF &pdfBounds);

    void suspendEditsAt(int page, const QRectF &pdfBounds, int blankPage = -1,
                        const QRectF &blankBounds = QRectF());
    void clearSuspended();
    void restoreSuspended();

    quint64 imageRevision() const { return m_imageRevision; }
    void addImageEdit(int page, const QRectF &pdfBounds, const QImage &image);
    void removeImageEdit(int page, const QRectF &pdfBounds);
    bool hasImageEditsOnPage(int page) const;
    bool hasDrawEditsOnPage(int page) const;
    bool hasLinkEditsOnPage(int page) const;
    bool hasNoteEditsOnPage(int page) const;
    void clearImageEdits();
    const QList<ImageEdit> &imageEdits() const { return m_imageEdits; }

    void replaceImageChanges(QList<ImageChange> changes);
    const QList<ImageChange> &imageChanges() const { return m_imageChanges; }

    void replaceDrawStrokes(QList<DrawStroke> strokes);
    const QList<DrawStroke> &drawStrokes() const { return m_drawStrokes; }

    void replaceLinkEdits(QList<LinkEdit> edits);
    const QList<LinkEdit> &linkEdits() const { return m_linkEdits; }
    void replaceNoteEdits(QList<NoteEdit> edits);
    const QList<NoteEdit> &noteEdits() const { return m_noteEdits; }

    void clear();

    const QList<Edit> &edits() const { return m_edits; }

    QList<Edit> snapshotEdits() const   { return m_edits; }
    void        restoreEdits(QList<Edit> s) { m_edits = std::move(s); }

    bool    hasEditsOnPage(int page) const;
    bool    hasAnyEdits() const
    { return !m_edits.isEmpty() || !m_imageEdits.isEmpty() || !m_imageChanges.isEmpty()
          || !m_drawStrokes.isEmpty() || !m_linkEdits.isEmpty()
          || !m_noteEdits.isEmpty(); }

    QString editTextAt(int page, const QRectF &pdfBounds) const;

    bool isBlankAt(int page, const QPointF &pdfPt) const;

    bool isBlankCovering(int page, const QRectF &bounds) const;

    QList<QRectF> blankRegions(int page) const;

    QColor  editColorAt(int page, const QRectF &pdfBounds) const;

    bool findEditAt(int page, const QPointF &pdfPt, Edit *out = nullptr) const;

    void paintFormFields(int page, QImage &img, qreal scale) const;

private:
    static void paintTextEdit(QPainter &p, const Edit &e, qreal scale);

    static void paintBackgroundPatch(QPainter &p, const QImage &img,
                                     const QRect &rectPx);
    static void paintBackgroundPatch(QPainter &p, const QImage &img,
                                     const QList<QRect> &rectsPx);

    static void paintBlankEdit(QPainter &p, const QImage &img, const Edit &e,
                               qreal scale);

    QList<Edit>       m_edits;
    QList<QPair<int, Edit>> m_suspendedEdits;
    QList<ImageEdit>  m_imageEdits;
    QList<ImageChange> m_imageChanges;
    quint64           m_imageRevision { 0 };
    QList<DrawStroke> m_drawStrokes;
    QList<LinkEdit>   m_linkEdits;
    QList<NoteEdit>   m_noteEdits;
};
