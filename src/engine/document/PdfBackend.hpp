#pragma once

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBookmark.hpp"
#include "engine/edit/TextBlock.hpp"

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class ContentProvider;
class EditSession;

/// One open PDF, behind the operations the application actually performs on it.
///
/// Everything above this line — view, editor, exporter — used to reach for
/// either a QPdfDocument or a Poppler::Document through `#ifdef`, in a dozen
/// files. The two libraries answer the same questions; only the spelling
/// differed. This is that set of questions, written once.
///
/// The backend owns the document AND the rendering, and that is the point:
/// swapping the open file is one call here instead of a choreography at the
/// call site. On the Poppler path the renderer used to point into the document
/// object, so every file change had to tear down the renderer, hand the new
/// document over, build a new renderer and re-hand it to four collaborators —
/// in that order, or it read freed memory. A PdfBackend* stays valid for the
/// life of the application, so none of that is needed any more.
///
/// Lives in engine/ and therefore knows nothing about widgets: asking the user
/// for a password is a callback, not a dialog.
class PdfBackend
{
public:
    /// Asked while an encrypted document refuses to open. `retry` is true after
    /// a rejected attempt, so the caller can say so. Returning nullopt means
    /// the user gave up and the load is abandoned.
    ///
    /// Only the asking is a callback — a password already stored for the file
    /// is tried first, and one that works is remembered, both without involving
    /// the caller.
    using PasswordAsker =
        std::function<std::optional<QString>(const QString &file, bool retry)>;

    virtual ~PdfBackend() = default;

    /// The backend this build was compiled with.
    static std::unique_ptr<PdfBackend> create();

    /// For logs and the about box — "Qt PDF", "Poppler".
    virtual QString name() const = 0;

    /// Opens `path`, asking through `ask` for as long as it stays encrypted.
    ///
    /// On failure the previously open document keeps working: a rejected open
    /// used to leave the document closed while the page widgets still described
    /// the old file, and the view went blank with no error.
    virtual bool open(const QString &path, const PasswordAsker &ask) = 0;

    /// Closes the document and releases the file. Needed before overwriting the
    /// file that is currently open — Windows refuses to replace a file that a
    /// process still holds.
    virtual void close() = 0;

    /// Path of the open document, empty when none is open.
    QString path() const { return m_path; }
    bool    isOpen() const { return !m_path.isEmpty(); }

    virtual int pageCount() const = 0;

    /// The document outline in display order. Page indices are zero-based.
    virtual QList<PdfBookmark> bookmarks() const { return {}; }

    virtual QSizeF pageSizePts(int page) const = 0;

    /// Size in pixels of the image renderPage() returns for `scale`. Backends
    /// disagree on rounding, and a page widget sized one pixel apart from its
    /// own pixmap makes QLabel centre and clip it — so the backend that renders
    /// is the one that answers.
    virtual QSize pixelSize(int page, qreal scale) const = 0;

    /// `scale` is output pixels per PDF point. Returns a null image when the
    /// page cannot be rendered; a broken page must never take the app down.
    virtual QImage renderPage(int page, qreal scale) const = 0;

    virtual QImage renderPage(int page, qreal scale,
                              const EditSession *session) const;

    /// Per-page region model (text, paragraphs, table cells, form fields,
    /// images) for the open document, or null when this build cannot build one.
    virtual std::unique_ptr<ContentProvider> makeContentProvider() const = 0;

    // ── Speichern ────────────────────────────────────────────────────────────

    /// Schreibt das offene Dokument mit allen Änderungen der Sitzung nach
    /// `outputPath`. Die Datei wird vollständig geschrieben oder gar nicht;
    /// den Tausch auf das Ziel besorgt der Aufrufer.
    ///
    /// Wie geschrieben wird, unterscheidet sich grundlegend: PDFium tauscht
    /// Seitenobjekte, die anderen beiden rastern oder schreiben den
    /// Content-Stream um. Für den Aufrufer ist es dieselbe Frage — und genau
    /// deshalb steht sie hier und nicht als Fallunterscheidung in der Ansicht.
    virtual bool saveWithEdits(const QString &outputPath,
                               const EditSession &session) const = 0;

    // ── Textabfrage ──────────────────────────────────────────────────────────
    // Die drei Fragen, die der Inline-Editor stellt. Koordinaten sind
    // PDF-Punkte mit Ursprung oben links (Y nach unten).
    //
    // `exclude` listet Bereiche, deren Text als GELÖSCHT gilt (von der Sitzung
    // überschriebene Stellen): ein Wort, dessen Mitte darin liegt, ist für jede
    // dieser Abfragen unsichtbar. Ohne das würde der Editor Text anbieten, den
    // der Benutzer gerade weggeschrieben hat.

    /// Textzeile an `pdfPt`, oder ein ungültiger TextBlock, wenn dort nichts steht.
    virtual TextBlock textAt(int page, const QPointF &pdfPt,
                             const QList<QRectF> &exclude = {}) const = 0;

    /// Vollständiger Text des Blocks, der `rect` abdeckt, Zeilen mit '\n'
    /// verbunden — plus die glyphengenauen Außenmaße der gefundenen Wörter.
    /// Enger als die geschätzte Breite aus dem Regionenmodell.
    virtual TextBlock blockInRect(int page, const QRectF &rect,
                                  const QList<QRectF> &exclude = {}) const = 0;

    // ── Auswahl ──────────────────────────────────────────────────────────────

    /// Was auf einer Seite markiert ist: die Kästen zum Einfärben und der Text
    /// zum Kopieren.
    struct Selection {
        QList<QRectF> rects;   // PDF-Punkte, Ursprung oben links
        QString       text;
    };

    /// Auswahl auf `page` zwischen zwei Ankern in PDF-Punkten. Ein nicht
    /// gesetzter Anker heißt „vom Anfang" beziehungsweise „bis zum Ende" der
    /// Seite — genau das bekommen die Seiten in der Mitte einer mehrseitigen
    /// Markierung.
    ///
    /// Wie aus zwei Mauspunkten eine Textauswahl wird, ist beim Backend gut
    /// aufgehoben: Qt hat dafür eine eigene API, Poppler nicht und muss den
    /// Textfluss aus der Wortliste rekonstruieren. Für den Aufrufer ist beides
    /// dieselbe Frage.
    virtual Selection selectPage(int page,
                                 const std::optional<QPointF> &from,
                                 const std::optional<QPointF> &to) const = 0;

    virtual bool hasSelectableText(int page) const = 0;

    virtual QString embeddedFontFamily(int page, const QPointF &pdfPt) const;

    /// Enge Wortkästen des Blocks in `area`. Damit wird beim Ersetzen NUR die
    /// Schrift übermalt und nicht die Fläche — sonst verschwinden Diagramme,
    /// Linien und Bilder, die sich das Rechteck mit dem Text teilen.
    virtual QList<QRectF> glyphRects(int page, const QRectF &area,
                                     const QList<QRectF> &exclude = {}) const = 0;

protected:
    QString m_path;
};

#endif // HAVE_PDF_RENDERING
