#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "app/PdfPwStore.hpp"
#include "engine/document/PdfiumContentProvider.hpp"
#include "engine/document/PdfiumEdits.hpp"
#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumLock.hpp"
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

FPDF_DOCUMENT loadWith(const QByteArray &utf8Path, const QString &password,
                       unsigned long *error)
{
    PdfiumLock lock;
    const QByteArray pw = password.toUtf8();
    FPDF_DOCUMENT doc = FPDF_LoadDocument(utf8Path.constData(),
                                          pw.isEmpty() ? nullptr : pw.constData());
    *error = doc ? FPDF_ERR_SUCCESS : FPDF_GetLastError();
    return doc;
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

}

PdfiumBackend::PdfiumBackend()
{
    PdfiumLock lock;
    retainPdfium();
}

PdfiumBackend::~PdfiumBackend()
{
    PdfiumLock lock;
    dropBackgroundPages();
    dropReadPages();
    {
        std::lock_guard guard(m_metricsMutex);
        m_metricsCache.clear();
    }
    if (m_doc) {
        PdfiumFonts::releaseFonts(m_doc);
        FPDF_CloseDocument(m_doc);
    }
    releasePdfium();
}

bool PdfiumBackend::open(const QString &path, const PasswordAsker &ask)
{
    if (path.isEmpty()) return false;
    const QByteArray utf8 = path.toUtf8();

    unsigned long error = FPDF_ERR_SUCCESS;
    FPDF_DOCUMENT opened = loadWith(utf8, PdfPwStore::get(path), &error);

    for (int attempt = 0; ask && !opened && error == FPDF_ERR_PASSWORD;
         ++attempt) {
        const std::optional<QString> entered = ask(path, attempt > 0);
        if (!entered) break;
        opened = loadWith(utf8, *entered, &error);
        if (opened) {

            PdfPwStore::set(path, *entered);
        }
    }

    if (!opened) {
        qWarning() << "PdfiumBackend: could not open" << path
                   << "- error" << error;

        return false;
    }

    PdfiumLock lock;
    dropBackgroundPages();
    dropReadPages();
    if (m_doc) {
        PdfiumFonts::releaseFonts(m_doc);
        FPDF_CloseDocument(m_doc);
    }
    m_doc  = opened;
    m_path = path;
    m_pageSizes.clear();
    for (int page = 0, count = FPDF_GetPageCount(m_doc); page < count; ++page) {
        FS_SIZEF size {};
        FPDF_GetPageSizeByIndexF(m_doc, page, &size);
        m_pageSizes.push_back(QSizeF(size.width, size.height));
    }
    return true;
}

void PdfiumBackend::close()
{
    PdfiumLock lock;
    dropBackgroundPages();
    dropReadPages();
    {
        std::lock_guard guard(m_metricsMutex);
        m_metricsCache.clear();
    }
    if (m_doc) {
        PdfiumFonts::releaseFonts(m_doc);
        FPDF_CloseDocument(m_doc);
        m_doc = nullptr;
    }
    m_path.clear();
    m_pageSizes.clear();
}

int PdfiumBackend::pageCount() const
{
    return static_cast<int>(m_pageSizes.size());
}

QList<PdfBookmark> PdfiumBackend::bookmarks() const
{
    PdfiumLock lock;
    if (!m_doc) return {};
    int remaining = 10000;
    return bookmarkLevel(m_doc, nullptr, 0, remaining);
}

QList<PdfBackend::Link> PdfiumBackend::pageLinks(int page) const
{
    PdfiumLock lock;
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
    PdfiumLock lock;
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
    if (page < 0 || page >= pageCount()) return {};
    return m_pageSizes[static_cast<size_t>(page)];
}

QSize PdfiumBackend::pixelSize(int page, qreal scale) const
{

    const QSizeF pts = pageSizePts(page);
    return QSize(qRound(pts.width() * scale), qRound(pts.height() * scale));
}

std::unique_ptr<ContentProvider> PdfiumBackend::makeContentProvider() const
{
    if (!m_doc) return nullptr;
    return std::make_unique<PdfiumContentProvider>(m_doc);
}

bool PdfiumBackend::saveWithEdits(const QString &outputPath,
                                  const EditSession &session,
                                  const std::function<bool()> &cancelled) const
{
    return PdfiumWriter::save(m_path, outputPath, session, cancelled);
}

struct PdfiumChar {
    QRectF  box;
    QString ch;
    int    index { 0 };
    double fontSize { 0.0 };

    double baseline { 0.0 };
    double originX  { 0.0 };
    double endX     { 0.0 };
    const void *object { nullptr };
};

struct PdfiumLine {
    QRectF  rect;
    QString text;
    double  baseline { 0.0 };
    QList<double> charX;
    QList<bool>   objectStart;
    double  endX { 0.0 };
    const void *lastObject { nullptr };

    void append(const PdfiumChar &c)
    {
        for (int k = 0; k < c.ch.size(); ++k) {
            charX.append(c.originX);
            objectStart.append(k == 0 && (charX.size() == 1 || c.object != lastObject));
        }
        lastObject = c.object;
        text += c.ch;
    }
};

namespace {

bool centerInAny(const QRectF &box, const QList<QRectF> &zones)
{
    const QPointF c = box.center();
    for (const QRectF &z : zones)
        if (z.contains(c)) return true;
    return false;
}

bool readingOrderLess(const PdfiumChar &a, const PdfiumChar &b)
{
    if (!PdfiumTextRules::sameLine(a.baseline, b.baseline,
                                  qMin(a.box.height(), b.box.height())))
        return a.baseline < b.baseline;
    if (a.box.left() != b.box.left()) return a.box.left() < b.box.left();
    return a.index < b.index;
}

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

}

// Text queries of the editor read the same page many times per click; the
// last pages read stay loaded with their text. They are never changed.
FPDF_PAGE PdfiumBackend::readPage(int page) const
{
    constexpr size_t kKept = 2;
    for (size_t i = 0; i < m_readPages.size(); ++i) {
        if (m_readPages[i].index != page) continue;
        const ReadPage hit = m_readPages[i];
        m_readPages.erase(m_readPages.begin() + qsizetype(i));
        m_readPages.insert(m_readPages.begin(), hit);
        return hit.page;
    }
    if (!m_doc || page < 0 || page >= pageCount()) return nullptr;
    FPDF_PAGE loaded = FPDF_LoadPage(m_doc, page);
    if (!loaded) return nullptr;
    m_readPages.insert(m_readPages.begin(), { page, loaded, nullptr });
    while (m_readPages.size() > kKept) {
        if (m_readPages.back().text) FPDFText_ClosePage(m_readPages.back().text);
        FPDF_ClosePage(m_readPages.back().page);
        m_readPages.pop_back();
    }
    return loaded;
}

FPDF_TEXTPAGE PdfiumBackend::readText(int page) const
{
    if (!readPage(page)) return nullptr;
    ReadPage &front = m_readPages.front();
    if (!front.text) front.text = FPDFText_LoadPage(front.page);
    return front.text;
}

void PdfiumBackend::dropReadPages() const
{
    for (const ReadPage &kept : m_readPages) {
        if (kept.text) FPDFText_ClosePage(kept.text);
        FPDF_ClosePage(kept.page);
    }
    m_readPages.clear();
}

std::vector<PdfiumLine> PdfiumBackend::linesOfPage(int page,
                                                   const QList<QRectF> &exclude,
                                                   const std::optional<QPointF> &from,
                                                   const std::optional<QPointF> &to,
                                                   LineSplit split) const
{
    std::vector<PdfiumLine> lines;
    if (!m_doc) return lines;

    FPDF_PAGE pg = readPage(page);
    if (!pg) return lines;
    FPDF_TEXTPAGE tp = readText(page);
    if (!tp) return lines;

    const double pageHeight = FPDF_GetPageHeightF(pg);
    const int    total      = FPDFText_CountChars(tp);

    std::vector<PdfiumChar> chars;
    chars.reserve(total > 0 ? total : 0);
    for (int i = 0; i < total; ++i) {
        double left = 0, right = 0, bottom = 0, top = 0;
        if (!FPDFText_GetCharBox(tp, i, &left, &right, &bottom, &top)) continue;

        const char32_t cp = FPDFText_GetUnicode(tp, i);
        if (cp == 0 || cp == u'\r' || cp == u'\n') continue;
        const QString ch = QChar::requiresSurrogates(cp) ? QString::fromUcs4(&cp, 1)
                                                         : QString(QChar(char16_t(cp)));

        double originX = 0, originY = 0;
        FPDFText_GetCharOrigin(tp, i, &originX, &originY);

        QRectF box(left, pageHeight - top, right - left, top - bottom);
        double baseline = pageHeight - originY;

        // A space that the writer placed through the text matrix instead of a
        // glyph comes back with an empty box at a meaningless spot; Qt's own PDF
        // writer does exactly that. Sorted by that spot it ends up somewhere in
        // the middle of another word, and every word on the page runs into the
        // next. Anchored to the character before it, it lands where it belongs.
        if (box.width() <= 0.0 && !chars.empty()) {
            const PdfiumChar &previous = chars.back();
            box      = QRectF(previous.box.right(), previous.box.top(),
                              0.01, previous.box.height());
            baseline = previous.baseline;
        }
        if (box.width() <= 0.0 && box.height() <= 0.0) continue;

        if (centerInAny(box, exclude)) continue;

        FPDF_PAGEOBJECT object = FPDFText_GetTextObject(tp, i);
        const double fontSize = PdfiumTextRules::effectiveFontSize(tp, i);
        const double advance = object ? PdfiumFonts::glyphWidth(FPDFTextObj_GetFont(object),
                                                               cp, fontSize)
                                      : box.width();
        chars.push_back({ box, ch, i, fontSize, baseline, originX,
                          originX + (advance > 0.0 ? advance : box.width()), object });
    }
    std::sort(chars.begin(), chars.end(), readingOrderLess);

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
        const bool ink = !c.ch.at(0).isSpace();

        const bool newBaseline = lines.empty()
                          || !PdfiumTextRules::sameLine(c.baseline, baseline,
                                                        c.box.height());
        const auto startLine = [&]() {
            PdfiumLine line;
            line.rect = c.box;
            line.baseline = c.baseline;
            line.append(c);
            line.endX = c.endX;
            lines.push_back(line);
            baseline = c.baseline;
            prev = i;
        };
        if (newBaseline) {
            if (!ink) continue;
            startLine();
            continue;
        }
        if (lines.empty()) continue;

        const double fontSize = prev >= 0
            ? qMax(chars[prev].fontSize, c.fontSize) : c.fontSize;

        if (ink && split == LineSplit::Blocks && prev >= 0
                && PdfiumTextRules::separatesBlocks(chars[prev].box, c.box, fontSize)
                && !sameObject(chars[prev].box, c.box)) {
            startLine();
            continue;
        }

        PdfiumLine &line = lines.back();
        if (ink && prev >= 0 && chars[prev].ch == c.ch
                && PdfiumTextRules::sameGlyph(chars[prev].box, c.box))
            continue;
        if (ink && prev >= 0 && !line.text.endsWith(QLatin1Char(' '))
                && PdfiumTextRules::separatesWords(chars[prev].box, c.box, fontSize)) {
            line.text += QLatin1Char(' ');
            line.charX.append(chars[prev].endX);
            line.objectStart.append(false);
        }
        line.append(c);
        if (ink) {
            line.rect = line.rect.united(c.box);
            line.endX = c.endX;
            prev = i;
        }
    }

    for (PdfiumLine &line : lines)
        while (line.text.endsWith(QLatin1Char(' '))) {
            line.text.chop(1);
            line.charX.removeLast();
            line.objectStart.removeLast();
        }

    return lines;
}

TextBlock PdfiumBackend::textAt(int page, const QPointF &pdfPt,
                                const QList<QRectF> &exclude) const
{
    PdfiumLock lock;
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
    PdfiumLock lock;
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
    PdfiumLock lock;
    QList<QRectF> out;
    for (const PdfiumLine &line :
             linesOfPage(page, exclude, {}, {}, LineSplit::Blocks))
        if (area.contains(line.rect.center())) out.append(line.rect);
    return out;
}

QList<TextLayout::OriginalLine> PdfiumBackend::originalLines(
    int page, const QRectF &area, const QList<QRectF> &exclude) const
{
    PdfiumLock lock;
    QList<TextLayout::OriginalLine> out;
    for (const PdfiumLine &line :
             linesOfPage(page, exclude, {}, {}, LineSplit::Blocks)) {
        if (!area.contains(line.rect.center())) continue;
        TextLayout::OriginalLine o;
        o.rect        = line.rect;
        o.text        = line.text;
        o.baseline    = line.baseline;
        o.charX       = line.charX;
        o.objectStart = line.objectStart;
        o.endX        = line.endX;
        out.append(o);
    }
    return out;
}

bool PdfiumBackend::hasSelectableText(int page) const
{
    PdfiumLock lock;
    if (!m_doc) return false;
    FPDF_TEXTPAGE tp = readText(page);
    const int chars = tp ? FPDFText_CountChars(tp) : 0;

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

double widthWithFallback(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_FONT font,
                         const QString &text, double sizePt)
{
    QList<uint> unverified;
    for (const uint cp : text.toUcs4())
        if (!QChar::isSpace(cp) && !unverified.contains(cp)
                && PdfiumFonts::hasGlyph(font, cp))
            unverified.append(cp);
    const QSet<uint> decodable = PdfiumFonts::decodable(doc, page, font, unverified);
    double width = 0.0;
    for (const char32_t cp : text.toUcs4()) {
        FPDF_FONT used = font;
        const bool own = QChar::isSpace(cp)
            || (PdfiumFonts::hasGlyph(font, cp) && decodable.contains(uint(cp)));
        if (!own)
            used = PdfiumFonts::fallbackFont(doc, font, cp);
        if (used) width += PdfiumFonts::glyphWidth(used, cp, sizePt);
    }
    return width;
}

}

double PdfiumBackend::textWidthPt(int page, const QPointF &pdfPt,
                                  const QString &text, double sizePt) const
{
    PdfiumLock lock;
    if (!m_doc || text.isEmpty() || sizePt <= 0.0) return text.isEmpty() ? 0.0 : -1.0;
    FPDF_PAGE pg = readPage(page);
    if (!pg) return -1.0;
    FPDF_FONT font = fontAtPoint(pg, FPDF_GetPageHeightF(pg), pdfPt);
    const double width = font && !PdfiumFonts::isType3(font)
                             ? widthWithFallback(m_doc, pg, font, text, sizePt) : -1.0;
    return width;
}

double PdfiumBackend::standardTextWidthPt(const QString &family, bool bold,
                                          bool italic, const QString &text,
                                          double sizePt) const
{
    PdfiumLock lock;
    if (!m_doc || sizePt <= 0.0) return -1.0;
    if (text.isEmpty()) return 0.0;
    FPDF_FONT font = FPDFText_LoadStandardFont(
        m_doc, PdfiumFonts::standardFontFor(family, bold, italic).constData());
    if (!font) return -1.0;
    const double width = widthWithFallback(m_doc, nullptr, font, text, sizePt);
    FPDFFont_Close(font);
    return width;
}

bool PdfiumBackend::canEmbedFont(const QString &family, bool bold,
                                 bool italic) const
{
    PdfiumLock lock;
    return !PdfiumFonts::fontData(family, bold, italic).isEmpty();
}

TextLayout::Metrics PdfiumBackend::editMetrics(const EditSession::Edit &edit) const
{
    // The box's place only moves the result's origin, its height only counts
    // without a font size and its shape not at all; the editor asks again on
    // every zoom step, which must not wait for PDFium.
    EditSession::Edit key = edit;
    key.pdfBounds  = edit.fontSizePt > 0.0 ? QRectF() : QRectF(0, 0, 0, edit.pdfBounds.height());
    key.box.bounds = QRectF();
    {
        std::lock_guard guard(m_metricsMutex);
        for (const auto &[known, metrics] : m_metricsCache)
            if (known == key) {
                TextLayout::Metrics out = metrics;
                out.boxTopLeft = edit.pdfBounds.topLeft();
                return out;
            }
    }
    PdfiumLock lock;
    if (!m_doc || edit.page < 0) return {};
    FPDF_PAGE pg = readPage(edit.page);
    if (!pg) return {};
    const TextLayout::Metrics metrics = PdfiumEdits::metrics(m_doc, pg, edit);
    std::lock_guard guard(m_metricsMutex);
    constexpr int kKept = 32;
    m_metricsCache.prepend({ key, metrics });
    if (m_metricsCache.size() > kKept) m_metricsCache.removeLast();
    return metrics;
}

QString PdfiumBackend::embeddedFontFamily(int page, const QPointF &pdfPt) const
{
    PdfiumLock lock;
    if (!m_doc) return {};
    FPDF_PAGE pg = readPage(page);
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
    return family;
}

PdfBackend::Selection PdfiumBackend::selectPage(int page,
                                                const std::optional<QPointF> &from,
                                                const std::optional<QPointF> &to) const
{
    PdfiumLock lock;
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
    for (int page = 0; page < pageCount(); ++page) matches += findTextOnPage(page, text);
    return matches;
}

QList<PdfBackend::TextMatch> PdfiumBackend::findTextOnPage(int page, const QString &text) const
{
    PdfiumLock lock;
    QList<TextMatch> matches;
    if (!m_doc || text.isEmpty() || page < 0 || page >= pageCount()) return matches;

    std::vector<unsigned short> needle;
    needle.reserve(static_cast<size_t>(text.size()) + 1);
    for (const QChar ch : text)
        needle.push_back(ch.unicode());
    needle.push_back(0);

    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return matches;
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(pg);
    if (!textPage) {
        FPDF_ClosePage(pg);
        return matches;
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
    return matches;
}

#endif
