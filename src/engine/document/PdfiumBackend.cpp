#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "app/PdfPwStore.hpp"
#include "engine/document/PdfiumContentProvider.hpp"
#include "engine/document/PdfiumEdits.hpp"
#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumTextRules.hpp"
#include "engine/document/PdfiumWriter.hpp"
#include "engine/edit/ContentModel.hpp"
#include "engine/edit/EditSession.hpp"

#include "fpdf_edit.h"
#include "fpdf_annot.h"
#include "fpdf_doc.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

// FPDF_InitLibrary gehört genau einmal pro Prozess aufgerufen, aber es gibt
// mehrere Backends gleichzeitig: eines für die Ansicht, eines für den Organizer,
// eines für die Präsentation. Ein Zähler hält die Bibliothek so lange am Leben,
// wie mindestens eines davon existiert.
//
// Nicht threadsicher, und das ist Absicht — Backends entstehen ausschließlich im
// UI-Thread. Sollte sich das je ändern, gehört hier ein Mutex hin.
int g_pdfiumUsers = 0;

void retainPdfium()
{
    if (g_pdfiumUsers++ > 0) return;
    FPDF_LIBRARY_CONFIG config;
    config.version          = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate       = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);
}

void releasePdfium()
{
    if (--g_pdfiumUsers > 0) return;
    FPDF_DestroyLibrary();
}

// Öffnet mit genau einem Passwort. Leer heißt "ohne".
FPDF_DOCUMENT loadWith(const QByteArray &utf8Path, const QString &password)
{
    const QByteArray pw = password.toUtf8();
    return FPDF_LoadDocument(utf8Path.constData(),
                             pw.isEmpty() ? nullptr : pw.constData());
}

QString bookmarkTitle(FPDF_BOOKMARK bookmark)
{
    const unsigned long bytes = FPDFBookmark_GetTitle(bookmark, nullptr, 0);
    if (bytes <= sizeof(char16_t)) return {};

    std::vector<char16_t> buffer((bytes + sizeof(char16_t) - 1)
                                 / sizeof(char16_t));
    const unsigned long written = FPDFBookmark_GetTitle(
        bookmark, buffer.data(), static_cast<unsigned long>(
            buffer.size() * sizeof(char16_t)));
    if (written <= sizeof(char16_t)) return {};
    return QString::fromUtf16(buffer.data(),
                              static_cast<qsizetype>(written / sizeof(char16_t) - 1));
}

QString annotationString(FPDF_ANNOTATION annotation, const char *key)
{
    const unsigned long bytes = FPDFAnnot_GetStringValue(annotation, key, nullptr, 0);
    if (bytes <= sizeof(char16_t)) return {};
    std::vector<char16_t> buffer((bytes + sizeof(char16_t) - 1) / sizeof(char16_t));
    const unsigned long written = FPDFAnnot_GetStringValue(
        annotation, key, reinterpret_cast<FPDF_WCHAR *>(buffer.data()), bytes);
    if (written <= sizeof(char16_t)) return {};
    return QString::fromUtf16(buffer.data(),
        static_cast<qsizetype>(written / sizeof(char16_t) - 1));
}

int bookmarkPage(FPDF_DOCUMENT document, FPDF_BOOKMARK bookmark, bool &supported)
{
    FPDF_DEST dest = FPDFBookmark_GetDest(document, bookmark);
    if (!dest) {
        FPDF_ACTION action = FPDFBookmark_GetAction(bookmark);
        if (action) {
            if (FPDFAction_GetType(action) != PDFACTION_GOTO) {
                supported = false;
                return -1;
            }
            dest = FPDFAction_GetDest(document, action);
        }
    }
    return dest ? FPDFDest_GetDestPageIndex(document, dest) : -1;
}

QList<PdfBookmark> bookmarkLevel(FPDF_DOCUMENT document, FPDF_BOOKMARK parent,
                                 int depth, int &remaining)
{
    QList<PdfBookmark> result;
    if (depth >= 64 || remaining <= 0) return result;

    for (FPDF_BOOKMARK item = FPDFBookmark_GetFirstChild(document, parent);
         item && remaining > 0;
         item = FPDFBookmark_GetNextSibling(document, item)) {
        --remaining;
        PdfBookmark bookmark;
        bookmark.title    = bookmarkTitle(item).trimmed();
        bookmark.page     = bookmarkPage(document, item, bookmark.supported);
        bookmark.expanded = FPDFBookmark_GetCount(item) >= 0;
        bookmark.children = bookmarkLevel(document, item, depth + 1, remaining);
        if (bookmark.title.isEmpty())
            bookmark.title = QStringLiteral("Untitled bookmark");
        result.append(std::move(bookmark));
    }
    return result;
}

QString markName(FPDF_PAGEOBJECTMARK mark)
{
    unsigned long bytes = 0;
    if (!mark || !FPDFPageObjMark_GetName(mark, nullptr, 0, &bytes) || bytes <= 2)
        return {};
    std::vector<unsigned short> buffer(bytes / 2 + 1, 0);
    if (!FPDFPageObjMark_GetName(mark, buffer.data(), bytes, &bytes)) return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()));
}

bool isOpenPdfLinkStyle(FPDF_PAGEOBJECT object)
{
    const int count = FPDFPageObj_CountMarks(object);
    for (int i = 0; i < count; ++i)
        if (markName(FPDFPageObj_GetMark(object, static_cast<unsigned long>(i)))
                == QLatin1String("OpenPDFLinkStyle"))
            return true;
    return false;
}

bool objectTouches(FPDF_PAGEOBJECT object, const QList<QRectF> &rects,
                   double pageHeight)
{
    float left = 0, bottom = 0, right = 0, top = 0;
    if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) return false;
    const QRectF bounds(left, pageHeight - top, right - left, top - bottom);
    for (const QRectF &rect : rects)
        if (rect.intersects(bounds) || rect.contains(bounds.center())) return true;
    return false;
}

} // namespace

PdfiumBackend::PdfiumBackend()
{
    retainPdfium();
}

PdfiumBackend::~PdfiumBackend()
{
    if (m_doc) FPDF_CloseDocument(m_doc);
    releasePdfium();
}

bool PdfiumBackend::open(const QString &path, const PasswordAsker &ask)
{
    if (path.isEmpty()) return false;
    const QByteArray utf8 = path.toUtf8();

    // Erst mit dem, was für diese Datei schon bekannt ist (Wiederöffnen,
    // Arbeitskopie) — danach erst wird gefragt.
    FPDF_DOCUMENT opened = loadWith(utf8, PdfPwStore::get(path));

    for (int attempt = 0; ask && !opened && FPDF_GetLastError() == FPDF_ERR_PASSWORD;
         ++attempt) {
        const std::optional<QString> entered = ask(path, attempt > 0);
        if (!entered) break;                            // abgebrochen
        opened = loadWith(utf8, *entered);
        if (opened) {
            // Jeder andere Leser dieser Datei — Inhaltsscanner, Sitzung,
            // Exporter — holt sich das Passwort von hier.
            PdfPwStore::set(path, *entered);
        }
    }

    if (!opened) {
        qWarning() << "PdfiumBackend: could not open" << path
                   << "- error" << FPDF_GetLastError();
        // Das vorherige Dokument bleibt offen und die Ansicht zeigt weiter, was
        // sie hatte — genau wie bei den beiden anderen Backends.
        return false;
    }

    if (m_doc) FPDF_CloseDocument(m_doc);
    m_doc  = opened;
    m_path = path;
    return true;
}

void PdfiumBackend::close()
{
    if (m_doc) {
        FPDF_CloseDocument(m_doc);
        m_doc = nullptr;
    }
    m_path.clear();
}

int PdfiumBackend::pageCount() const
{
    return m_doc ? FPDF_GetPageCount(m_doc) : 0;
}

QList<PdfBookmark> PdfiumBackend::bookmarks() const
{
    if (!m_doc) return {};
    int remaining = 10000;
    return bookmarkLevel(m_doc, nullptr, 0, remaining);
}

QList<PdfBackend::Link> PdfiumBackend::pageLinks(int page) const
{
    QList<Link> result;
    if (!m_doc || page < 0 || page >= pageCount()) return result;

    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return result;
    const double pageHeight = FPDF_GetPageHeightF(pg);

    const int count = FPDFPage_GetAnnotCount(pg);
    for (int i = 0; i < count; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(pg, i);
        if (!annot) continue;
        if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_LINK) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        const FPDF_LINK link = FPDFAnnot_GetLink(annot);
        const FPDF_ACTION action = link ? FPDFLink_GetAction(link) : nullptr;
        FS_RECTF rect {};
        if (!action || FPDFAction_GetType(action) != PDFACTION_URI
                || !FPDFAnnot_GetRect(annot, &rect)) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        const unsigned long bytes = FPDFAction_GetURIPath(m_doc, action, nullptr, 0);
        if (bytes > 1) {
            std::vector<char> buffer(bytes, 0);
            if (FPDFAction_GetURIPath(m_doc, action, buffer.data(), bytes) > 1) {
                Link item;
                item.bounds = QRectF(rect.left, pageHeight - rect.top,
                                     rect.right - rect.left, rect.top - rect.bottom);
                item.url = QString::fromUtf8(buffer.data());
                const int quadCount = FPDFLink_CountQuadPoints(link);
                for (int quadIndex = 0; quadIndex < quadCount; ++quadIndex) {
                    FS_QUADPOINTSF quad {};
                    if (!FPDFLink_GetQuadPoints(link, quadIndex, &quad)) continue;
                    const float minX = std::min({ quad.x1, quad.x2, quad.x3, quad.x4 });
                    const float maxX = std::max({ quad.x1, quad.x2, quad.x3, quad.x4 });
                    const float minY = std::min({ quad.y1, quad.y2, quad.y3, quad.y4 });
                    const float maxY = std::max({ quad.y1, quad.y2, quad.y3, quad.y4 });
                    item.textRects.append(QRectF(minX, pageHeight - maxY,
                                                 maxX - minX, maxY - minY));
                }
                if (item.textRects.isEmpty()) item.textRects.append(item.bounds);
                const int objectCount = FPDFPage_CountObjects(pg);
                for (int objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
                    FPDF_PAGEOBJECT object = FPDFPage_GetObject(pg, objectIndex);
                    if (object && isOpenPdfLinkStyle(object)
                            && objectTouches(object, item.textRects, pageHeight)) {
                        item.styledByOpenPdf = true;
                        break;
                    }
                }
                if (item.bounds.isValid() && !item.url.isEmpty())
                    result.append(std::move(item));
            }
        }
        FPDFPage_CloseAnnot(annot);
    }
    FPDF_ClosePage(pg);
    return result;
}

QList<PdfBackend::Note> PdfiumBackend::pageNotes(int page) const
{
    QList<Note> result;
    if (!m_doc || page < 0 || page >= pageCount()) return result;

    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return result;
    const double pageHeight = FPDF_GetPageHeightF(pg);

    const int count = FPDFPage_GetAnnotCount(pg);
    for (int i = 0; i < count; ++i) {
        FPDF_ANNOTATION annotation = FPDFPage_GetAnnot(pg, i);
        if (!annotation) continue;
        FS_RECTF rect {};
        if (FPDFAnnot_GetSubtype(annotation) == FPDF_ANNOT_TEXT
                && FPDFAnnot_GetRect(annotation, &rect)) {
            Note note;
            note.id     = annotationString(annotation, "NM");
            note.title  = annotationString(annotation, "T");
            note.text   = annotationString(annotation, "Contents");
            note.pinned = annotationString(annotation, "OpenPDFPinned")
                       == QLatin1String("1");
            note.bounds = QRectF(rect.left, pageHeight - rect.top,
                                 rect.right - rect.left, rect.top - rect.bottom);
            if (note.bounds.isValid()) result.append(std::move(note));
        }
        FPDFPage_CloseAnnot(annotation);
    }
    FPDF_ClosePage(pg);
    return result;
}

QSizeF PdfiumBackend::pageSizePts(int page) const
{
    if (!m_doc) return {};
    FS_SIZEF size {};
    if (!FPDF_GetPageSizeByIndexF(m_doc, page, &size)) return {};
    return QSizeF(size.width, size.height);
}

QSize PdfiumBackend::pixelSize(int page, qreal scale) const
{
    // Gerundet wie im Qt-Backend, das dieselbe Engine benutzt: so bleibt der
    // Vergleich zwischen beiden ein Vergleich der Engine und nicht der Rundung.
    const QSizeF pts = pageSizePts(page);
    return QSize(qRound(pts.width() * scale), qRound(pts.height() * scale));
}

QImage PdfiumBackend::renderPage(int page, qreal scale) const
{
    return renderPageInternal(page, scale, nullptr);
}

QImage PdfiumBackend::renderPage(int page, qreal scale,
                                 const EditSession *session) const
{
    return renderPageInternal(page, scale, session);
}

QImage PdfiumBackend::renderPageInternal(int page, qreal scale,
                                         const EditSession *session) const
{
    if (!m_doc) return {};
    const QSize px = pixelSize(page, scale);
    if (px.isEmpty()) return {};

    if (session && !session->hasEditsOnPage(page)
            && !session->hasImageEditsOnPage(page)
            && !session->hasDrawEditsOnPage(page)
            && !session->hasLinkEditsOnPage(page))
        session = nullptr;

    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return {};

    if (session) PdfiumEdits::applyToPage(m_doc, pg, page, *session);

    // alpha = 0: die Bitmap hat kein Alpha, das Layout ist BGRx und entspricht
    // damit QImage::Format_RGB32. Vorher weiß füllen — dann stellt sich die
    // Frage nach vormultipliziertem Alpha gar nicht erst, und die Seite sieht
    // aus wie bei den anderen Backends.
    FPDF_BITMAP bmp = FPDFBitmap_Create(px.width(), px.height(), 0);
    if (!bmp) { FPDF_ClosePage(pg); return {}; }
    FPDFBitmap_FillRect(bmp, 0, 0, px.width(), px.height(), 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bmp, pg, 0, 0, px.width(), px.height(), 0, FPDF_ANNOT);

    const auto *buffer = static_cast<const uchar *>(FPDFBitmap_GetBuffer(bmp));
    QImage rendered;
    if (buffer) {
        // Der Puffer gehört PDFium und stirbt gleich — deshalb eine echte Kopie
        // und keine Sicht darauf.
        rendered = QImage(buffer, px.width(), px.height(),
                          FPDFBitmap_GetStride(bmp), QImage::Format_RGB32).copy();
    }

    FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(pg);

    if (session && !rendered.isNull())
        session->applyToImage(page, rendered, scale, EditSession::Paint::FormFields);

    return rendered;
}

std::unique_ptr<ContentProvider> PdfiumBackend::makeContentProvider() const
{
    if (!m_doc) return nullptr;
    return std::make_unique<PdfiumContentProvider>(m_doc);
}

bool PdfiumBackend::saveWithEdits(const QString &outputPath,
                                  const EditSession &session) const
{
    return PdfiumWriter::save(m_path, outputPath, session);
}

// ── Text ─────────────────────────────────────────────────────────────────────
//
// PDFiums Text-API rechnet in PDF-Koordinaten: Ursprung unten links, Y nach
// oben. Das Interface hier — und alles darüber — rechnet oben links mit Y nach
// unten. Umgerechnet wird deshalb an genau einer Stelle, in linesOfPage(); ab
// da ist alles Qt-Konvention.

/// Ein Zeichen mit seinem Kasten, bereits in Qt-Koordinaten.
///
/// Im globalen Namensraum, weil der Header ihn vorwärts deklariert.
struct PdfiumChar {
    QRectF box;
    QChar  ch;
    int    index { 0 };   ///< Index in PDFiums eigener Zeichenliste
    double fontSize { 0.0 };
    /// Y der Schriftgrundlinie. Zeilen werden danach gebildet und NICHT nach
    /// der Kastenmitte: ein Komma hängt unter die Grundlinie, ein Großbuchstabe
    /// ragt darüber. Nach Mitten gruppiert brach die Zeile "… sicher, wenn …"
    /// genau am Komma.
    double baseline { 0.0 };
};

/// Eine sichtbare Zeile mit ihrem Text.
struct PdfiumLine {
    QRectF  rect;
    QString text;
};

namespace {

bool centerInAny(const QRectF &box, const QList<QRectF> &zones)
{
    const QPointF c = box.center();
    for (const QRectF &z : zones)
        if (z.contains(c)) return true;
    return false;
}

/// Lesereihenfolge: erst die Zeile, dann die Position darin. Dieselbe Regel wie
/// im Poppler-Backend, damit sich beide gleich verhalten.
bool readingOrderLess(const PdfiumChar &a, const PdfiumChar &b)
{
    if (!PdfiumTextRules::sameLine(a.baseline, b.baseline,
                                  qMin(a.box.height(), b.box.height())))
        return a.baseline < b.baseline;
    return a.box.left() < b.box.left();
}

/// Index des Zeichens, zu dem ein Anker gehört. Senkrechter Abstand wiegt
/// schwerer, damit ein Punkt im rechten Rand am Ende SEINER Zeile landet und
/// nicht in einer waagerecht näheren darüber.
int anchorIndex(const std::vector<PdfiumChar> &chars, const QPointF &pt)
{
    if (chars.empty()) return -1;
    int    best     = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < chars.size(); ++i) {
        const QRectF &b = chars[i].box;
        const double dy = qMax(0.0, qMax(b.top()  - pt.y(), pt.y() - b.bottom()));
        const double dx = qMax(0.0, qMax(b.left() - pt.x(), pt.x() - b.right()));
        const double d  = dy * 8.0 + dx;
        if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
    }
    return best;
}

} // namespace

std::vector<PdfiumLine> PdfiumBackend::linesOfPage(int page,
                                                   const QList<QRectF> &exclude,
                                                   const std::optional<QPointF> &from,
                                                   const std::optional<QPointF> &to,
                                                   LineSplit split) const
{
    std::vector<PdfiumLine> lines;
    if (!m_doc) return lines;

    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return lines;
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(pg);
    if (!tp) { FPDF_ClosePage(pg); return lines; }

    const double pageHeight = FPDF_GetPageHeightF(pg);
    const int    total      = FPDFText_CountChars(tp);

    std::vector<PdfiumChar> chars;
    chars.reserve(total > 0 ? total : 0);
    for (int i = 0; i < total; ++i) {
        double left = 0, right = 0, bottom = 0, top = 0;
        if (!FPDFText_GetCharBox(tp, i, &left, &right, &bottom, &top)) continue;

        const QChar ch(static_cast<char16_t>(FPDFText_GetUnicode(tp, i)));
        if (ch.isNull() || ch == QChar(u'\r') || ch == QChar(u'\n')) continue;

        double originX = 0, originY = 0;
        FPDFText_GetCharOrigin(tp, i, &originX, &originY);

        // Hier und nur hier wird gespiegelt: PDF rechnet von unten, wir von oben.
        const QRectF box(left, pageHeight - top, right - left, top - bottom);
        if (box.width() <= 0.0 && box.height() <= 0.0) continue;
        // Von der Sitzung überschriebene Stellen gelten als leer.
        if (centerInAny(box, exclude)) continue;

        chars.push_back({ box, ch, i, FPDFText_GetFontSize(tp, i),
                          pageHeight - originY });
    }
    std::sort(chars.begin(), chars.end(), readingOrderLess);

    // Anker beschneiden den Bereich; ohne Anker gilt die ganze Seite.
    int first = 0;
    int last  = static_cast<int>(chars.size()) - 1;
    if (from) first = anchorIndex(chars, *from);
    if (to)   last  = anchorIndex(chars, *to);

    QList<QRectF> textObjects;
    if (split == LineSplit::Blocks) {
        const int objects = FPDFPage_CountObjects(pg);
        for (int i = 0; i < objects; ++i) {
            FPDF_PAGEOBJECT obj = FPDFPage_GetObject(pg, i);
            if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) continue;
            float l = 0, b = 0, r = 0, t = 0;
            if (!FPDFPageObj_GetBounds(obj, &l, &b, &r, &t)) continue;
            textObjects.append(QRectF(l, pageHeight - t, r - l, t - b));
        }
    }

    if (!chars.empty() && first >= 0 && last >= first)
        lines = buildLines(chars, first, last, split, textObjects);

    FPDFText_ClosePage(tp);
    FPDF_ClosePage(pg);
    return lines;
}

std::vector<PdfiumLine> PdfiumBackend::buildLines(const std::vector<PdfiumChar> &chars,
                                                  int first, int last, LineSplit split,
                                                  const QList<QRectF> &textObjects)
{
    const auto sameObject = [&textObjects](const QRectF &a, const QRectF &b) {
        for (const QRectF &o : textObjects)
            if (o.contains(a.center()) && o.contains(b.center())) return true;
        return false;
    };

    std::vector<PdfiumLine> lines;
    double baseline = 0.0;
    int prev = -1;

    for (int i = first; i <= last && i < static_cast<int>(chars.size()); ++i) {
        const PdfiumChar &c = chars[i];
        const bool ink = !c.ch.isSpace();

        const bool newBaseline = lines.empty()
                          || !PdfiumTextRules::sameLine(c.baseline, baseline,
                                                        c.box.height());
        if (newBaseline) {
            if (!ink) continue;
            lines.push_back({ c.box, QString(c.ch) });
            baseline = c.baseline;
            prev = i;
            continue;
        }
        if (lines.empty()) continue;

        const double fontSize = prev >= 0
            ? qMax(chars[prev].fontSize, c.fontSize) : c.fontSize;

        if (ink && split == LineSplit::Blocks && prev >= 0
                && PdfiumTextRules::separatesBlocks(chars[prev].box, c.box, fontSize)
                && !sameObject(chars[prev].box, c.box)) {
            lines.push_back({ c.box, QString(c.ch) });
            baseline = c.baseline;
            prev = i;
            continue;
        }

        PdfiumLine &line = lines.back();
        if (ink && prev >= 0 && chars[prev].ch == c.ch
                && PdfiumTextRules::sameGlyph(chars[prev].box, c.box))
            continue;
        if (ink && prev >= 0 && !line.text.endsWith(QLatin1Char(' '))
                && PdfiumTextRules::separatesWords(chars[prev].box, c.box, fontSize))
            line.text += QLatin1Char(' ');
        line.text += c.ch;
        if (ink) {
            line.rect = line.rect.united(c.box);
            prev = i;
        }
    }

    for (PdfiumLine &line : lines)
        while (line.text.endsWith(QLatin1Char(' ')))
            line.text.chop(1);

    return lines;
}

TextBlock PdfiumBackend::textAt(int page, const QPointF &pdfPt,
                                const QList<QRectF> &exclude) const
{
    const std::vector<PdfiumLine> lines =
        linesOfPage(page, exclude, {}, {}, LineSplit::Blocks);

    const PdfiumLine *best = nullptr;
    double bestDist = std::numeric_limits<double>::max();
    for (const PdfiumLine &line : lines) {
        const QRectF &r = line.rect;
        const double dy = qMax(0.0, qMax(r.top()  - pdfPt.y(), pdfPt.y() - r.bottom()));
        const double dx = qMax(0.0, qMax(r.left() - pdfPt.x(), pdfPt.x() - r.right()));
        const double d  = dy * 8.0 + dx;
        if (d < bestDist) { bestDist = d; best = &line; }
    }
    if (!best) return {};

    // Nur treffen, was in Reichweite liegt: ein Klick weit neben dem Text darf
    // nicht die nächstbeste Zeile aufmachen. 40 pt ist dieselbe Grenze, die der
    // Qt-Extraktor in seinem dritten Durchgang ansetzt und die das
    // Regionenmodell für seine Umkreissuche benutzt — eine engere Fassung ließ
    // hier Klicks ins Leere laufen, die auf dem alten Pfad noch eine Zeile
    // öffneten.
    const QRectF &r = best->rect;
    const double dy = qMax(0.0, qMax(r.top()  - pdfPt.y(), pdfPt.y() - r.bottom()));
    const double dx = qMax(0.0, qMax(r.left() - pdfPt.x(), pdfPt.x() - r.right()));
    const bool onLine = dy <= qMax(2.0, r.height() * 0.25);
    if (onLine ? dx > 100.0 : std::hypot(dx, dy) > 40.0) return {};

    return TextBlock{ page, best->rect, best->text };
}

TextBlock PdfiumBackend::blockInRect(int page, const QRectF &rect,
                                     const QList<QRectF> &exclude) const
{
    QRectF  bounds;
    QString text;
    for (const PdfiumLine &line :
             linesOfPage(page, exclude, {}, {}, LineSplit::Blocks)) {
        if (!rect.contains(line.rect.center())) continue;
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text  += line.text;
        bounds = bounds.isNull() ? line.rect : bounds.united(line.rect);
    }
    if (bounds.isNull()) return {};
    return TextBlock{ page, bounds, text };
}

QList<QRectF> PdfiumBackend::glyphRects(int page, const QRectF &area,
                                        const QList<QRectF> &exclude) const
{
    QList<QRectF> out;
    for (const PdfiumLine &line :
             linesOfPage(page, exclude, {}, {}, LineSplit::Blocks))
        if (area.contains(line.rect.center())) out.append(line.rect);
    return out;
}

bool PdfiumBackend::hasSelectableText(int page) const
{
    if (!m_doc) return false;
    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return false;
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(pg);
    const int chars = tp ? FPDFText_CountChars(tp) : 0;
    if (tp) FPDFText_ClosePage(tp);
    FPDF_ClosePage(pg);

    return chars >= 16;
}

namespace {

FPDF_FONT fontAtPoint(FPDF_PAGE pg, double pageHeight, const QPointF &pdfPt)
{
    FPDF_FONT best = nullptr;
    double bestArea = 0.0;
    const int count = FPDFPage_CountObjects(pg);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(pg, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) continue;
        float left = 0, bottom = 0, right = 0, top = 0;
        if (!FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) continue;
        const QRectF box(left, pageHeight - top, right - left, top - bottom);
        if (!box.adjusted(-2, -2, 2, 2).contains(pdfPt)) continue;
        const double area = box.width() * box.height();
        if (best && area >= bestArea) continue;
        best     = FPDFTextObj_GetFont(obj);
        bestArea = area;
    }
    return best;
}

}

double PdfiumBackend::textWidthPt(int page, const QPointF &pdfPt,
                                  const QString &text, double sizePt) const
{
    if (!m_doc || text.isEmpty() || sizePt <= 0.0) return text.isEmpty() ? 0.0 : -1.0;
    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return -1.0;
    FPDF_FONT font = fontAtPoint(pg, FPDF_GetPageHeightF(pg), pdfPt);
    double width = -1.0;
    if (font) {
        width = 0.0;
        for (const uint cp : text.toUcs4()) {
            float advance = 0.f;
            if (FPDFFont_GetGlyphWidth(font, cp, static_cast<float>(sizePt), &advance))
                width += advance;
        }
    }
    FPDF_ClosePage(pg);
    return width;
}

QString PdfiumBackend::embeddedFontFamily(int page, const QPointF &pdfPt) const
{
    if (!m_doc) return {};
    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return {};
    const double pageHeight = FPDF_GetPageHeightF(pg);

    FPDF_FONT best = nullptr;
    double bestArea = 0.0;
    const int count = FPDFPage_CountObjects(pg);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(pg, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) continue;
        float left = 0, bottom = 0, right = 0, top = 0;
        if (!FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) continue;
        const QRectF box(left, pageHeight - top, right - left, top - bottom);
        if (!box.adjusted(-2, -2, 2, 2).contains(pdfPt)) continue;
        const double area = box.width() * box.height();
        if (best && area >= bestArea) continue;
        best     = FPDFTextObj_GetFont(obj);
        bestArea = area;
    }

    const QString family = PdfiumFonts::registerWithQt(best);
    FPDF_ClosePage(pg);
    return family;
}

PdfBackend::Selection PdfiumBackend::selectPage(int page,
                                                const std::optional<QPointF> &from,
                                                const std::optional<QPointF> &to) const
{
    Selection out;
    for (const PdfiumLine &line :
             linesOfPage(page, {}, from, to, LineSplit::Baseline)) {
        out.rects.append(line.rect);
        if (!out.text.isEmpty()) out.text += QLatin1Char('\n');
        out.text += line.text;
    }
    return out;
}

QList<PdfBackend::TextMatch> PdfiumBackend::findText(const QString &text) const
{
    QList<TextMatch> matches;
    if (!m_doc || text.isEmpty()) return matches;

    std::vector<unsigned short> needle;
    needle.reserve(static_cast<size_t>(text.size()) + 1);
    for (const QChar ch : text)
        needle.push_back(ch.unicode());
    needle.push_back(0);

    for (int page = 0; page < pageCount(); ++page) {
        FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
        if (!pg) continue;
        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(pg);
        if (!textPage) {
            FPDF_ClosePage(pg);
            continue;
        }

        FPDF_SCHHANDLE search = FPDFText_FindStart(textPage, needle.data(), 0, 0);
        const double pageHeight = FPDF_GetPageHeightF(pg);
        while (search && FPDFText_FindNext(search)) {
            const int start = FPDFText_GetSchResultIndex(search);
            const int count = FPDFText_GetSchCount(search);
            TextMatch match;
            match.page = page;

            const int rectCount = FPDFText_CountRects(textPage, start, count);
            for (int i = 0; i < rectCount; ++i) {
                double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
                if (!FPDFText_GetRect(textPage, i, &left, &top, &right, &bottom))
                    continue;
                match.rects.append(QRectF(left, pageHeight - top,
                                           right - left, top - bottom));
            }
            if (!match.rects.isEmpty()) matches.append(std::move(match));
        }

        if (search) FPDFText_FindClose(search);
        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(pg);
    }
    return matches;
}

#endif // HAVE_PDF_RENDERING && HAVE_PDFIUM
