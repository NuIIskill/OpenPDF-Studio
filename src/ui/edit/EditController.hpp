#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

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

/// State of the one edit that is currently open, plus what the last commit
/// left behind.
///
/// Twenty-seven fields that only ever move together: which block is being
/// edited, how it has to look, where its pen sits, and what the editor was
/// presented with so a commit can tell whether anything actually changed.
/// They lived on DocumentView, where they made up a third of its members and
/// were indistinguishable from the view's own state.
///
/// The behaviour is moving here one method at a time. That works because the
/// state came first: a migrated method only touches fields it already owns.
///
/// What it needs back from the view goes out as signals rather than through a
/// host interface — a second polymorphic base on DocumentView changes its
/// object layout, and an earlier attempt at exactly that stopped mouse events
/// reaching the view at all.
///
/// Fields stay public while the migration runs: accessors around data one
/// owner mutates in sequence would be noise, and would have to be unpicked
/// again once the last caller in DocumentView is gone.
class EditController : public QObject
{
    Q_OBJECT

public:
    explicit EditController(QObject *parent = nullptr) : QObject(parent) {}

    /// Handed the collaborators once, after DocumentView has built them. One
    /// call so a forgotten one is a compile error, not a null dereference on
    /// the first click.
    void attach(PageCanvas *canvas, DocumentSource *src, TextBoxFrame *frame,
                ZoomController *zoom, HoverHighlight *hover, OcrEngine *ocr,
                QUndoStack *undo)
    { m_canvas = canvas; m_src = src; m_frame = frame; m_zoom = zoom;
      m_hover = hover; m_ocr = ocr; m_undo = undo; }

#ifdef HAVE_PDF_RENDERING
    void setSession(EditSession *session) { m_session = session; }
    void clearOcrCache() { m_ocrCache.clear(); }
#endif

    // ── Font and colour of the open editor (driven by the FormatBar) ─────────
    /// Pushes the current family/bold/italic into the open editor widget.
    void refreshFontLive();
    void setFontFamily(const QString &family);
    void setBold(bool on);
    void setItalic(bool on);
    /// A size the user typed is meant literally, in the document's own font —
    /// no calibration to a substitute face on top of it. The edit bounds scale
    /// with it so line breaks survive the change.
    void setFontSize(int ptSize);
    void setTextColor(const QColor &color);

    /// Clamps `r` so it stays fully inside page `page` (position and size).
    void clampToPdfPage(int page, QRectF &r) const;

#ifdef HAVE_PDF_RENDERING
    /// Opens an edit on whatever sits under `canvasPos`: resolve what was
    /// clicked, work out how the replacement has to look, put the editor over
    /// it. Does nothing when there is nothing editable there.
    void handleClick(const QPoint &canvasPos);

    /// Writes the open edit into the session and closes the editor. Called
    /// when the user leaves the box, presses the commit key, or clicks away.
    void commit(const QString &newText);
    /// Closes the editor and throws the typed text away.
    void cancel();
    /// Drag-to-create: opens an empty editor over `viewportDragRect`.
    void createTextFrame(const QRect &viewportDragRect);
#endif

Q_SIGNALS:
    /// An editor opened — the FormatBar has to show what was detected.
    void fontSizeChanged(int ptSize);
    void fontChanged(const QString &family, bool bold, bool italic);
    /// Redraw a page as the document has it.
    void pageNeedsRerender(int page);
    /// Redraw a page with `bounds` blanked, hiding the text being edited.
    void pageNeedsBlank(int page, const QRectF &bounds);
    /// An edit happened and belongs in the document's change log.
    void changeRecorded(const DocumentHistory::Change &change);

public:
    int     activeEditPage { -1 };        // page under the box NOW (follows drags)
    // Page the edit was OPENED on. The box may be dragged onto another page:
    // the blank (erasing the original text) always belongs to the source page,
    // the new text is committed on activeEditPage.
    int     activeEditSourcePage { -1 };
    QRectF  activeEditBounds;
    QRectF  activeEditOriginalBounds;  // native PDF bounds when the edit was first opened
    // Tight glyph rects of the original text — erasure targets ONLY these,
    // never the whole bounds rect (which would wipe co-located graphics).
    QList<QRectF> activeEditEraseRects;
    // true  → edit was opened by clicking on existing text (handleEditClick); a blank
    //         edit must be committed to erase the original text before drawing the new.
    // false → editor was created fresh via drag (createTextFrame); no erasure needed —
    //         the text is drawn as a transparent overlay so it can sit on top of content.
    bool    activeEditNeedsBlank { false };
    // true → the editor was opened on content that is already in the document
    // (handleEditClick). Committing such an edit erases the original, so a
    // commit that changes NOTHING must be dropped instead: re-drawing identical
    // text would still swap the embedded font for ours (see commitCurrentEdit).
    bool    activeEditInPlace { false };
    QString activeEditOriginalText;
    // The text as the DOCUMENT had it, which survives every re-edit — unlike
    // activeEditOriginalText, which becomes whatever was typed last. It is
    // the proof of which glyphs the file's own font carries, so re-editing a
    // line must not let characters we typed ourselves pass as evidence.
    QString activeEditPdfText;
    // State as the editor was PRESENTED — the reference for "did anything
    // actually change?". Bounds are the presented ones (they can be wider than
    // activeEditOriginalBounds, which stays at the original glyph rect).
    QRectF  activeEditPresentedBounds;
    double  activeEditPresentedFontSizePt { 0.0 };
    QColor  activeEditPresentedColor;
    QString activeEditFieldName;   // non-empty: editing an AcroForm text field
    // Where the text being edited starts (x = pen, y = baseline), as an offset
    // from activeEditBounds' top-left so moving or resizing the frame carries
    // it along. This is what both paint paths align to; without it the
    // replacement lands a few points below and right of the line it replaces.
    QPointF activeEditOriginOffset;
    bool    activeEditHasOrigin { false };
    // Baseline-to-baseline distance of the block being edited (0 = unknown).
    double  activeEditLineSpacingPt { 0.0 };
    // Two sizes, on purpose (see EditSession::Edit): the one the file gets and
    // the one the screen gets. They differ whenever the family being painted
    // is not the embedded one, which is most of the time.
    double  currentEditorFontSizePt   { 12.0 };
    double  currentEditorRenderSizePt { 12.0 };
    bool    editorSizeChangedByUser   { false };
    QColor  currentEditorColor     { 0x11, 0x11, 0x11 };
    QColor  currentBgColor;  // cell background detected from PDF content stream
    // Font of the active editor (detected from the clicked block, or chosen
    // in the FormatBar). editorFontChangedByUser gates the vector-save font
    // switch: only a user choice replaces the PDF's original font resource.
    QString currentEditorFontFamily;
    bool    currentEditorBold   { false };
    bool    currentEditorItalic { false };
    bool    editorFontChangedByUser { false };

    // After a commit, the press+release that triggered it must not re-open
    // an editor for the same block — that would blank the just-committed text.
    int    lastCommittedPage { -1 };
    QRectF lastCommittedOrigBounds;

#ifdef HAVE_PDF_RENDERING
    /// Session edits as they stood when the editor opened — the "before" an
    /// undo command restores.
    QList<EditSession::Edit> undoSnapBefore;
#endif

    /// Set while an edit is being pushed onto the undo stack: the index moves,
    /// but towards a state the history has not been told about yet.
    bool pushingEdit { false };

private:
#ifdef HAVE_PDF_RENDERING
    // Everything one edit-open carries from resolving the click to presenting
    // the editor; each step reads what the earlier ones worked out.
    struct EditOpen;
    bool resolveEditTarget(const QPoint &canvasPos, EditOpen &o);
    void applyEditTargetBounds(EditOpen &o);
    void chooseEditorFont(EditOpen &o);
    void anchorEditOrigin(EditOpen &o);
    void fitEditHeight(EditOpen &o);
    void sampleEditColors(EditOpen &o);
    void fitEditWidth(EditOpen &o);
    void presentEditor(EditOpen &o);

    // Builds a session edit from the active editor state (font, colors,
    // form-field binding). Single source of truth for commit AND live paths.
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
