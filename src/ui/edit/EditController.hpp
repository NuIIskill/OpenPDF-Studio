#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "engine/edit/TextBoxProperties.hpp"

class DocumentSource;
class QUndoStack;
class HoverHighlight;
class OcrEngine;
class PageCanvas;
class TextBoxFrame;
class ZoomController;

#include "app/DocumentHistory.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#  include "engine/ocr/OcrEngine.hpp"
#endif

/// State of the one edit that is currently open, plus what the last commit left behind.
class EditController : public QObject
{
    Q_OBJECT

public:
    explicit EditController(QObject *parent = nullptr) : QObject(parent) {}

    void attach(PageCanvas *canvas, DocumentSource *src, TextBoxFrame *frame,
                ZoomController *zoom, HoverHighlight *hover, OcrEngine *ocr,
                QUndoStack *undo)
    { m_canvas = canvas; m_src = src; m_frame = frame; m_zoom = zoom;
      m_hover = hover; m_ocr = ocr; m_undo = undo; }

#ifdef HAVE_PDF_RENDERING
    void setSession(EditSession *session) { m_session = session; }
    void clearOcrCache() { m_ocrCache.clear(); }
#endif

    void refreshFontLive();
    void setFontFamily(const QString &family);
    void setBold(bool on);
    void setItalic(bool on);
    void setUnderline(bool on);

    void setFontSize(int ptSize);
    void setTextColor(const QColor &color);
    void setTextBoxProperties(const TextBoxProperties &properties);
    TextBoxProperties textBoxProperties() const;
    void notifyBoundsChanged();

    void syncBoundsFromFrame();

    void refreshAdvanceMeasure();

    void refreshLivePreview();
    void setTextBoxDefaults(const TextBoxProperties &properties) { defaultBox = properties; }
    void setHorizontalAlignment(Qt::Alignment alignment);
    void setListStyle(TextBoxProperties::ListStyle style);
    void changeIndent(int delta);
    void setLineSpacing(double multiplier);

    void clampToPdfPage(int page, QRectF &r) const;

#ifdef HAVE_PDF_RENDERING

    void handleClick(const QPoint &canvasPos);

    void commit(const QString &newText);

    void cancel();

    void createTextFrame(const QRect &viewportDragRect);
#endif

Q_SIGNALS:

    void fontSizeChanged(int ptSize);
    void fontChanged(const QString &family, bool bold, bool italic,
                     bool underline);
    void textBoxPropertiesChanged(const TextBoxProperties &properties);
    void textBoxEditingChanged(bool active);

    void pageNeedsRerender(int page);

    void pageNeedsBlank(int page, const QRectF &bounds);
    void livePreviewChanged(int page, const QList<EditSession::Edit> &edits);

    void changeRecorded(const DocumentHistory::Change &change);

public:
    int     activeEditPage { -1 };

    int     activeEditSourcePage { -1 };
    QRectF  activeEditBounds;
    QRectF  activeEditOriginalBounds;

    QList<QRectF> activeEditEraseRects;
    QRectF        activeEditEraseBounds;

    bool    activeEditNeedsBlank { false };

    bool    activeEditInPlace { false };
    QString activeEditOriginalText;

    QString activeEditPdfText;

    QRectF  activeEditPresentedBounds;
    double  activeEditPresentedFontSizePt { 0.0 };
    QColor  activeEditPresentedColor;
    QString activeEditFieldName;

    QPointF activeEditOriginOffset;

    QPointF activeEditPdfPt;
    bool    activeEditHasOrigin { false };

    double  activeEditLineSpacingPt { 0.0 };

    double  currentEditorFontSizePt   { 12.0 };
    double  currentEditorRenderSizePt { 12.0 };
    bool    editorSizeChangedByUser   { false };
    QColor  currentEditorColor     { 0x11, 0x11, 0x11 };
    QColor  currentBgColor;

    QString currentEditorFontFamily;
    bool    currentEditorBold   { false };
    bool    currentEditorItalic { false };
    bool    currentEditorUnderline { false };
    bool    editorFontChangedByUser { false };
    TextBoxProperties currentBox;
    TextBoxProperties presentedBox;
    TextBoxProperties defaultBox;

    int    lastCommittedPage { -1 };
    QRectF lastCommittedOrigBounds;

#ifdef HAVE_PDF_RENDERING

    QList<EditSession::Edit> undoSnapBefore;
#endif

    bool pushingEdit { false };

private:
#ifdef HAVE_PDF_RENDERING

    struct EditOpen;
    bool resolveEditTarget(const QPoint &canvasPos, EditOpen &o);
    void applyEditTargetBounds(EditOpen &o);
    void chooseEditorFont(EditOpen &o);
    void anchorEditOrigin(EditOpen &o);
    void fitEditHeight(EditOpen &o);
    void sampleEditColors(EditOpen &o);
    void fitEditWidth(EditOpen &o);
    void presentEditor(EditOpen &o);

    EditSession::Edit makeSessionEdit(int page, const QRectF &bounds,
                                      const QRectF &sourceRect,
                                      const QString &text) const;

    EditSession *m_session { nullptr };
    QUndoStack  *m_undo    { nullptr };
    QHash<int, QList<OcrEngine::Block>> m_ocrCache;
#endif
    PageCanvas     *m_canvas { nullptr };
    DocumentSource *m_src    { nullptr };
    TextBoxFrame   *m_frame  { nullptr };
    ZoomController *m_zoom   { nullptr };
    HoverHighlight *m_hover  { nullptr };
    OcrEngine      *m_ocr    { nullptr };
};
