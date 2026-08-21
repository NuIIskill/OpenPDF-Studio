#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "app/PdfPwStore.hpp"
#include "engine/document/PdfiumContentProvider.hpp"
#include "engine/document/PdfiumTextRules.hpp"
#include "engine/document/PdfiumWriter.hpp"
#include "engine/edit/ContentModel.hpp"

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
    if (!m_doc) return {};
    const QSize px = pixelSize(page, scale);
    if (px.isEmpty()) return {};

    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return {};

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
                                                   const std::optional<QPointF> &to) const
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

    if (!chars.empty() && first >= 0 && last >= first)
        lines = buildLines(chars, first, last);

    FPDFText_ClosePage(tp);
    FPDF_ClosePage(pg);
    return lines;
}

std::vector<PdfiumLine> PdfiumBackend::buildLines(const std::vector<PdfiumChar> &chars,
                                                  int first, int last)
{
    std::vector<PdfiumLine> lines;
    double baseline = 0.0;
    for (int i = first; i <= last && i < static_cast<int>(chars.size()); ++i) {
        const PdfiumChar &c = chars[i];
        const bool newLine = lines.empty()
                          || !PdfiumTextRules::sameLine(c.baseline, baseline,
                                                        c.box.height());
        if (newLine) {
            lines.push_back({ c.box, QString(c.ch) });
            baseline = c.baseline;
            continue;
        }
        PdfiumLine &line = lines.back();
        if (PdfiumTextRules::separatesWords(chars[i - 1].box, c.box,
                                            qMax(chars[i - 1].fontSize, c.fontSize)))
            line.text += QLatin1Char(' ');
        line.rect  = line.rect.united(c.box);
        line.text += c.ch;
    }
    return lines;
}

TextBlock PdfiumBackend::textAt(int page, const QPointF &pdfPt,
                                const QList<QRectF> &exclude) const
{
    const std::vector<PdfiumLine> lines = linesOfPage(page, exclude, {}, {});

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
    if (std::hypot(dx, dy) > 40.0) return {};

    return TextBlock{ page, best->rect, best->text };
}

TextBlock PdfiumBackend::blockInRect(int page, const QRectF &rect,
                                     const QList<QRectF> &exclude) const
{
    QRectF  bounds;
    QString text;
    for (const PdfiumLine &line : linesOfPage(page, exclude, {}, {})) {
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
    for (const PdfiumLine &line : linesOfPage(page, exclude, {}, {}))
        if (area.contains(line.rect.center())) out.append(line.rect);
    return out;
}

PdfBackend::Selection PdfiumBackend::selectPage(int page,
                                                const std::optional<QPointF> &from,
                                                const std::optional<QPointF> &to) const
{
    Selection out;
    for (const PdfiumLine &line : linesOfPage(page, {}, from, to)) {
        out.rects.append(line.rect);
        if (!out.text.isEmpty()) out.text += QLatin1Char('\n');
        out.text += line.text;
    }
    return out;
}

#endif // HAVE_PDF_RENDERING && HAVE_PDFIUM
