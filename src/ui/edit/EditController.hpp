#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <functional>
#include <optional>

#include "engine/edit/TextBoxProperties.hpp"
#include "engine/edit/TextLayout.hpp"

class DocumentSource;
class QUndoStack;
class HoverHighlight;
class OcrEngine;
class PageCanvas;
class TextBoxFrame;
class ZoomController;

#include "engine/historymanager/DocumentHistory.hpp"
#include "ui/edit/EditNotice.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#  include "engine/edit/SourcedText.hpp"
#  include "engine/ocr/OcrEngine.hpp"
#  include "ui/edit/EditOpener.hpp"
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
      m_hover = hover; m_ocr = ocr; m_undo = undo;
#ifdef HAVE_PDF_RENDERING
      m_opener.attach(canvas, src, zoom, ocr);
#endif
    }

#ifdef HAVE_PDF_RENDERING
    void setSession(EditSession *session) { m_session = session; m_opener.setSession(session); }
    void clearOcrCache() { m_opener.clearOcrCache(); }

    QByteArray encodedEdits() const;
    void       restoreEncodedEdits(const QByteArray &data);
#endif

    void refreshFontLive();
    void setFontFamily(const QString &family);
    void setBold(bool on);
    void setItalic(bool on);
    void setUnderline(bool on);

    void setFontSize(double ptSize);
    void setTextColor(const QColor &color);
    void setTextBoxProperties(const TextBoxProperties &properties);
    TextBoxProperties textBoxProperties() const;
    void notifyBoundsChanged();

    void syncBoundsFromFrame();

    void refreshAdvanceMeasure();

    void refreshLivePreview();
    void showLook();
    void setTextBoxDefaults(const TextBoxProperties &properties) { defaultBox = properties; }
    void setHorizontalAlignment(Qt::Alignment alignment);
    void setListStyle(TextBoxProperties::ListStyle style);
    void changeIndent(int delta);
    void setLineSpacing(double multiplier);

    void clampToPdfPage(int page, QRectF &r) const;

#ifdef HAVE_PDF_RENDERING

    void handleClick(const QPoint &canvasPos);
    void handleFrame(const QPoint &from, const QPoint &to);
    QList<QRect> framedText(const QPoint &from, const QPoint &to);
    void startFrame() { m_opener.forgetFrameLines(); }

    void commit(const QString &newText);

    void cancel();

    void createTextFrame(const QRect &viewportDragRect);
    bool pasteBox(const QPoint &canvasPos);
    bool hasTextAt(const QPoint &canvasPos) const;
    bool copyBoxAt(const QPoint &canvasPos);
#endif

Q_SIGNALS:

    void fontSizeChanged(double ptSize);
    void fontChanged(const QString &family, bool bold, bool italic,
                     bool underline);
    void textColorChanged(const QColor &color);
    void lineSpacingChanged(double multiplier);
    void textBoxPropertiesChanged(const TextBoxProperties &properties);
    void textBoxEditingChanged(bool active);

    void pageNeedsRerender(int page);

    void pageNeedsBlank(int page, const QRectF &bounds);
    void livePreviewChanged(int page, const QList<EditSession::Edit> &edits);

    void changeRecorded(const DocumentHistory::Change &change);
    void noticeWanted(EditNotice::Kind kind);

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
    QString activeEditOriginalPlain;
    QList<TextLayout::OriginalLine> activeEditOriginalLines;
    bool    activeEditMovedByUser { false };

    QString activeEditPdfText;
    bool    activeEditLaid { false };
    bool    activeEditScanned { false };

    QString activeEditFieldName;

    QPointF activeEditOriginOffset;

    QPointF activeEditPdfPt;
    bool    activeEditHasOrigin { false };

    double  activeEditLineSpacingPt { 0.0 };

    double  currentEditorFontSizePt   { 12.0 };
    bool    editorSizeChangedByUser   { false };
    QColor  currentEditorColor     { 0x11, 0x11, 0x11 };
    bool    editorColorChangedByUser  { false };
    QColor  currentBgColor;

    QString currentEditorFontFamily;
    bool    currentEditorBold   { false };
    bool    currentEditorItalic { false };
    bool    currentEditorUnderline { false };
    bool    editorFamilyChangedByUser { false };
    bool    editorBoldChangedByUser   { false };
    bool    editorItalicChangedByUser { false };
    double  activeEditBaseSizePt      { 12.0 };
    TextBoxProperties currentBox;
    TextBoxProperties defaultBox;

    int    lastCommittedPage { -1 };
    QRectF lastCommittedOrigBounds;

#ifdef HAVE_PDF_RENDERING

    QList<EditSession::Edit> undoSnapBefore;
#endif

private:
    /// How the open edit looks; one that still looks as it did when opened closes without a change.
    struct Look {
        int     page { -1 };
        QString family;
        bool    bold { false };
        bool    italic { false };
        bool    underline { false };
        double  sizePt { 0.0 };
        QColor  color;
        QRectF  bounds;
        TextBoxProperties box;
        bool    familyChanged { false };
        bool    boldChanged { false };
        bool    italicChanged { false };
        bool    colorChanged { false };
        QList<TextLayout::Mark> marks;

        bool sameStyle(const Look &o) const;
    };
    Look currentLook() const;
    Look m_presented;

    bool openedAll(const std::function<bool(const TextLayout::Appearance &)> &has,
                   bool boxHad) const;
    bool markSelection(const TextLayout::Mark &mark);
    void unmark(const TextLayout::Mark &fields);
    void emitLineSpacing();
    QList<TextLayout::Appearance> m_opened;
#ifdef HAVE_PDF_RENDERING
    QList<EditSession::Edit>      m_rest;
    bool                              m_hasUnit { false };
    EditSession::Edit                 m_unit;
    QList<QList<TextLayout::Source>>  m_unitSources;
    QList<QList<TextLayout::Mark>>    m_unitMarks;
#endif
    TextLayout::Appearance        m_shown;

    int    m_metricsPage { -1 };
    QRectF m_metricsBounds;
    bool   m_refreshingMetrics { false };
#ifdef HAVE_PDF_RENDERING

    void openAt(const QPoint &from, const QPoint &to, bool framed);
    void openTarget(const EditTarget &t);
    bool pasteLaid(int page, const QLabel *label, const QPoint &canvasPos, const SourcedText::Box &box);
    void hideFrame();
    void resizeText(double ptSize);
    QString documentKey() const;
    void presentEditor(const EditTarget &t);

    EditSession::Edit makeSessionEdit(int page, const QRectF &bounds,
                                      const QRectF &sourceRect,
                                      const QString &text) const;
    std::optional<EditSession::Edit> mergedFrameEdit(const QString &text) const;

    EditSession *m_session { nullptr };
    QUndoStack  *m_undo    { nullptr };
    EditOpener   m_opener;
#endif
    PageCanvas     *m_canvas { nullptr };
    DocumentSource *m_src    { nullptr };
    TextBoxFrame   *m_frame  { nullptr };
    ZoomController *m_zoom   { nullptr };
    HoverHighlight *m_hover  { nullptr };
    OcrEngine      *m_ocr    { nullptr };
};
