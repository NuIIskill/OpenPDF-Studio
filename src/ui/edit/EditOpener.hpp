#pragma once

#ifdef HAVE_PDF_RENDERING

#include "engine/edit/EditSession.hpp"
#include "engine/edit/ScanFont.hpp"
#include "engine/edit/TextBlock.hpp"
#include "engine/edit/TextBoxProperties.hpp"
#include "engine/edit/TextLayout.hpp"
#include "engine/ocr/OcrEngine.hpp"

#include <QColor>
#include <QHash>
#include <QSet>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class DocumentSource;
class PageCanvas;
class ZoomController;

/// The text a click opens for editing, with the bounds, font, baseline and colours the editor starts from.
struct EditTarget
{
    QLabel           *label { nullptr };
    bool              exactFont { true };
    QPointF           pdfPt;
    qreal             scale { 1.0 };
    QList<QRectF>     erasedZones;
    bool              isSessionEdit { false };
    EditSession::Edit sessionEdit;
    int               blankPage { -1 };
    TextBlock         block;
    QString           displayText;
    QPointF           textOrigin;
    double            turn { 0.0 };
    QPointF           turnPivot;

    QTransform toShown() const
    {
        return QTransform().translate(turnPivot.x(), turnPivot.y()).rotate(turn)
                           .translate(-turnPivot.x(), -turnPivot.y());
    }

    int               editPage   { -1 };
    int               sourcePage { -1 };
    QRectF            bounds;
    QRectF            originalBounds;
    bool              needsBlank { false };
    QRectF            eraseBounds;
    TextBoxProperties box;
    QString           fieldName;
    QString           originalText;
    QString           pdfText;

    double  fontSizePt        { 12.0 };
    bool    sizeChangedByUser { false };
    bool    colorChangedByUser { false };
    QString fontFamily;
    bool    bold              { false };
    bool    italic            { false };
    bool    underline         { false };
    bool    familyChangedByUser { false };
    bool    boldChangedByUser   { false };
    bool    italicChangedByUser { false };
    double  baseSizePt          { 12.0 };
    double  lineSpacingPt     { 0.0 };
    bool    hasOrigin         { false };
    QColor  color;
    QColor  bgColor;

    int     selStart { -1 };
    int     selEnd   { -1 };
    QString selectionIn;
    QList<TextLayout::OriginalLine> framedLines;
    QList<TextLayout::Source>       framedSources;
    QList<TextLayout::Mark>         framedMarks;
    QList<EditSession::Edit>        rest;
    bool                            laid { false };
    bool                            scanned { false };
    QList<QRectF>                   scanRects;
    bool                              hasUnit { false };
    EditSession::Edit                 unit;
    QList<QList<TextLayout::Source>>  unitSources;
    QList<QList<TextLayout::Mark>>    unitMarks;
};

/// Finds and measures the text a click in the text tool opens, without touching the editor.
class EditOpener
{
public:
    void attach(PageCanvas *canvas, DocumentSource *src, ZoomController *zoom, OcrEngine *ocr)
    { m_canvas = canvas; m_src = src; m_zoom = zoom; m_ocr = ocr; }
    void setSession(EditSession *session) { m_session = session; }
    void forgetScans() { m_scansAsked.clear(); }
    void readScansNear(int page);
    bool ocrMissing() const { return m_ocrMissing; }

    bool open(const QPoint &from, const QPoint &to, bool framed, int &lastCommittedPage,
              const QRectF &lastCommittedOrigBounds, EditTarget &target);

    static void clampToPage(const DocumentSource *src, int page, QRectF &r);
    static void clampTo(const QRectF &area, QRectF &r);
    static QPointF turnedPivotShift(double turn, const QPointF &by);
    static QPointF windowSpot(const QRectF &shown, const QSizeF &size, const QSizeF &pageSize);

    /// The part of a box edited before that a frame takes, and what stays of it.
    struct Cut {
        bool valid { false };
        EditSession::Edit edit;
        QList<QList<TextLayout::Source>> takenSources;
        QList<QList<TextLayout::Mark>>   takenMarks;
        QList<TextLayout::OriginalLine>  rest;
        QList<QList<TextLayout::Source>> restSources;
        QList<QList<TextLayout::Mark>>   restMarks;
        QList<TextLayout::OriginalLine>  lines;
        QList<QList<TextLayout::Source>> sources;
        QList<QList<TextLayout::Mark>>   marks;
        QList<TextLayout::OriginalLine>  gapLines;
        QList<TextLayout::OriginalLine>  wholeLines;
    };
    QList<TextLayout::OriginalLine> linesInFrame(const QPoint &from, const QPoint &to,
                                                 QPoint *firstAt = nullptr, int *page = nullptr,
                                                 Cut *cut = nullptr);
    void forgetFrameLines() { m_pageLines = {}; m_editLines = {}; }

private:
    struct EditOpen;
    bool resolveEditTarget(const QPoint &canvasPos, EditOpen &o, int &lastCommittedPage,
                           const QRectF &lastCommittedOrigBounds);
    bool scopeToFrame(EditOpen &o, const QList<TextLayout::OriginalLine> &lines);
    void scopeToCut(EditOpen &o, const QList<TextLayout::OriginalLine> &lines, const Cut &cut);
    void findUnit(EditOpen &whole, EditOpen &o, const QList<TextLayout::OriginalLine> &gapLines,
                  const QList<TextLayout::OriginalLine> &wholeLines);
    void clampToPage(const EditOpen &o, QRectF &r) const;
    void applyEditTargetBounds(EditOpen &o);
    void chooseEditorFont(EditOpen &o);
    void anchorEditOrigin(EditOpen &o);
    void fitEditHeight(EditOpen &o);
    void sampleEditColors(EditOpen &o);
    void fitEditWidth(EditOpen &o);
    void placeTurnedWindow(EditOpen &o);
    void measureScan(EditOpen &o);
    QList<QRectF> scanLines(EditOpen &o) const;
    QList<TextLayout::OriginalLine> scanTextLines(const EditOpen &o) const;
    QList<TextLayout::OriginalLine> scanParagraph(EditOpen &o, bool &cleaned);
    void matchScanFace(EditOpen &o);
    bool rereadScanLine(EditOpen &o, TextLayout::OriginalLine &line);
    QList<ScanFont::Line> scanContext(EditOpen &o, const QList<TextLayout::OriginalLine> &taken);
    bool waitForScan(int page);

    PageCanvas     *m_canvas  { nullptr };
    DocumentSource *m_src     { nullptr };
    ZoomController *m_zoom    { nullptr };
    OcrEngine      *m_ocr     { nullptr };
    EditSession    *m_session { nullptr };
    QSet<int> m_scansAsked;
    bool m_ocrMissing { false };

    struct PageLines { int page = -1; QList<TextLayout::OriginalLine> lines; };
    PageLines m_pageLines;
    struct EditLines {
        bool valid { false };
        EditSession::Edit edit;
        QList<TextLayout::OriginalLine> lines;
        QList<QList<TextLayout::Source>> sources;
        QList<QList<TextLayout::Mark>>   marks;
    };
    EditLines m_editLines;
};

#endif
