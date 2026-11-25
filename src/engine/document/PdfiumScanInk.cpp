#include "engine/document/PdfiumScanInk.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumImages.hpp"
#include "engine/document/PdfiumPageSpace.hpp"
#include "engine/document/PdfiumPart.hpp"
#include "engine/edit/ScanLike.hpp"
#include "engine/edit/ScanPaper.hpp"
#include "engine/edit/ScanRules.hpp"

#include "fpdf_edit.h"

#include <QPainter>
#include <QPen>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace PdfiumScanInk {

namespace {

using ScanMove = PdfiumText::Typeset::ScanMove;
using ScanInk = PdfiumText::Typeset::ScanInk;
using Outline = PdfiumText::Typeset::Outline;
using ScanPaper::Plane;

// The scan's own pixels laid over the page: what is read and set stands on whole pixels of it.
struct Grid {
    QSize  pagePx;
    double scale { 1.0 };
    double sx { 1.0 }, sy { 1.0 };

    bool isValid() const { return !pagePx.isEmpty(); }
    QRect within(const QRect &px) const { return px.intersected(QRect(QPoint(), pagePx)); }
    QRectF at(const QRect &px) const { return QRectF(px.x() / sx, px.y() / sy, px.width() / sx, px.height() / sy); }
    QRect around(const QRectF &pt, int margin) const
    {
        return within(QRect(QPoint(qFloor(pt.left() * sx) - margin, qFloor(pt.top() * sy) - margin),
                            QPoint(qCeil(pt.right() * sx) + margin, qCeil(pt.bottom() * sy) + margin)));
    }
};

// How many pixels of the scan stand on a point of the page around an area.
double resolutionAt(FPDF_PAGE page, const QRectF &area)
{
    constexpr double kLeast = 150.0 / 72.0, kMost = 600.0 / 72.0, kUsual = 300.0 / 72.0;
    double finest = 0.0;
    for (const PdfiumImages::Found &image : PdfiumImages::find(page)) {
        unsigned int w = 0, h = 0;
        if (image.bounds.isEmpty() || !image.bounds.intersects(area)
                || !FPDFImageObj_GetImagePixelSize(image.object, &w, &h))
            continue;
        finest = qMax(finest, std::sqrt(double(w) * double(h) / (image.bounds.width() * image.bounds.height())));
    }
    return finest > 0.0 ? qBound(kLeast, finest, kMost) : kUsual;
}

Grid gridOf(FPDF_PAGE page, const QRectF &area)
{
    Grid grid;
    const double wide = FPDF_GetPageWidthF(page), high = FPDF_GetPageHeightF(page);
    if (wide <= 0.0 || high <= 0.0 || area.isEmpty()) return grid;
    grid.scale = resolutionAt(page, area);
    grid.pagePx = QSize(qRound(wide * grid.scale), qRound(high * grid.scale));
    grid.sx = grid.pagePx.width() / wide;
    grid.sy = grid.pagePx.height() / high;
    return grid;
}

QImage render(FPDF_PAGE page, const QRect &px, const QSize &pagePx)
{
    const PdfiumPart::Frame frame = PdfiumPart::frameFor(page, px, pagePx);
    FPDF_BITMAP bitmap = FPDFBitmap_Create(frame.area.width(), frame.area.height(), 0);
    if (!bitmap) return {};
    FPDFBitmap_FillRect(bitmap, 0, 0, frame.area.width(), frame.area.height(), 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, frame.startX(), frame.startY(), pagePx.width(), pagePx.height(), 0, 0);
    const QImage out = PdfiumPart::cut(bitmap, frame, px.size());
    FPDFBitmap_Destroy(bitmap);
    return out;
}

// A stretch of the page as the scan has it. A document that keeps what it read has the page read whole and
// once, and every stretch is cut from that: rendering each by itself would decode the scan each time.
QImage read(FPDF_PAGE page, int pageIndex, const Grid &grid, const QRect &px, Cache *cache)
{
    constexpr qint64 kMost = 40'000'000;
    if (px.isEmpty()) return {};
    if (cache && qint64(grid.pagePx.width()) * grid.pagePx.height() <= kMost) {
        QImage whole = cache->findWhole(pageIndex, grid.pagePx);
        if (whole.isNull()) {
            whole = render(page, QRect(QPoint(), grid.pagePx), grid.pagePx);
            if (!whole.isNull()) cache->addWhole(pageIndex, grid.pagePx, whole);
        }
        if (!whole.isNull()) return px == whole.rect() ? whole : whole.copy(px);
    }
    return render(page, px, grid.pagePx);
}

QImage pixelsAt(FPDF_PAGE page, int pageIndex, const Grid &grid, const QRect &px, Cache *cache)
{
    if (px.isEmpty()) return {};
    QImage pixels = cache ? cache->find(pageIndex, px, grid.pagePx.width()) : QImage();
    if (!pixels.isNull()) return pixels;
    pixels = read(page, pageIndex, grid, px, cache);
    if (cache && !pixels.isNull()) cache->add(pageIndex, px, grid.pagePx.width(), pixels);
    return pixels;
}

// A stretch of the page read once, for the pieces cut out of it.
struct Sheet {
    QImage pixels;
    QRect  px;

    QImage part(const QRect &r) const
    {
        return !pixels.isNull() && px.contains(r) ? pixels.copy(r.translated(-px.topLeft())) : QImage();
    }
};

Sheet sheetOf(FPDF_PAGE page, int pageIndex, const Grid &grid, const QRect &px, Cache *cache)
{
    constexpr qint64 kMost = 8'000'000;
    if (px.isEmpty() || qint64(px.width()) * px.height() > kMost) return {};
    return { read(page, pageIndex, grid, px, cache), px };
}

// How much ink each pixel holds of text drawn black with its outlines thickened by a gain, or thinned by one below zero.
Plane drawn(const QList<Outline> &text, double gainPt, const Grid &grid, const QRect &px)
{
    if (px.isEmpty()) return {};
    QImage image(px.size(), QImage::Format_Grayscale8);
    image.fill(255);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-px.x(), -px.y());
    p.scale(grid.sx, grid.sy);
    for (const Outline &o : text) {
        // A gain moves every edge of the letters, their heads and feet too: the text is drawn as much higher
        // or lower about its middle first, so it stands as high as the face was fitted to the scan.
        QPainterPath path = o.path;
        if (const QRectF box = path.boundingRect(); !qFuzzyIsNull(gainPt) && box.height() > 0.0) {
            const double middle = box.center().y();
            path = QTransform().translate(0.0, middle).scale(1.0, qBound(0.85, (box.height() - gainPt) / box.height(), 1.15))
                       .translate(0.0, -middle).map(path);
        }
        p.fillPath(path, Qt::black);
        const double pen = o.strokePt + gainPt;
        if (!qFuzzyIsNull(pen))
            p.strokePath(path, QPen(pen > 0.0 ? Qt::black : Qt::white, std::abs(pen), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    p.end();
    Plane ink = ScanPaper::grayOf(image);
    for (float &v : ink.v) v = (255.0f - v) / 255.0f;
    return ink;
}

}

QImage Cache::find(int page, const QRect &px, int pageWidthPx, bool cleared) const
{
    for (const Entry &e : m_entries)
        if (e.page == page && e.px == px && e.pageWidthPx == pageWidthPx && e.cleared == cleared) return e.pixels;
    return {};
}

void Cache::add(int page, const QRect &px, int pageWidthPx, const QImage &pixels, bool cleared)
{
    constexpr qsizetype kMost = 2048;
    if (m_entries.size() >= kMost) m_entries.remove(0, kMost / 4);
    m_entries.append({ page, px, pageWidthPx, pixels, cleared });
}

bool Cache::findLook(const QString &key, Look *look) const
{
    for (const auto &[k, l] : m_looks)
        if (k == key) { *look = l; return true; }
    return false;
}

void Cache::addLook(const QString &key, const Look &look)
{
    constexpr qsizetype kMost = 64;
    if (m_looks.size() >= kMost) m_looks.remove(0, kMost / 4);
    m_looks.append({ key, look });
}

bool Cache::findLetters(int page, int pageWidthPx, QList<QRect> *letters) const
{
    for (const Letters &l : m_letters)
        if (l.page == page && l.pageWidthPx == pageWidthPx) { *letters = l.boxes; return true; }
    return false;
}

void Cache::addLetters(int page, int pageWidthPx, const QList<QRect> &letters)
{
    constexpr qsizetype kMost = 8;
    if (m_letters.size() >= kMost) m_letters.removeFirst();
    m_letters.append({ page, pageWidthPx, letters });
}

QImage Cache::findWhole(int page, const QSize &pagePx) const
{
    for (const Whole &w : m_wholes)
        if (w.page == page && w.pagePx == pagePx) return w.pixels;
    return {};
}

void Cache::addWhole(int page, const QSize &pagePx, const QImage &pixels)
{
    constexpr qsizetype kMost = 2;
    if (m_wholes.size() >= kMost) m_wholes.removeFirst();
    m_wholes.append({ page, pagePx, pixels });
}

void Cache::clear()
{
    m_entries.clear();
    m_looks.clear();
    m_letters.clear();
    m_wholes.clear();
}

QList<QRectF> pictures(FPDF_PAGE page)
{
    // The small pictures lying over a page's scan, as an earlier edit leaves its letters there.
    QList<QRectF> out;
    const double all = double(FPDF_GetPageWidthF(page)) * double(FPDF_GetPageHeightF(page));
    const QList<PdfiumImages::Found> images = PdfiumImages::find(page);
    const auto share = [all](const QRectF &b) { return all > 0.0 ? b.width() * b.height() / all : 1.0; };
    if (std::none_of(images.cbegin(), images.cend(), [&](const PdfiumImages::Found &image) { return share(image.bounds) >= 0.5; })) return out;
    for (const PdfiumImages::Found &image : images)
        if (image.bounds.width() >= 3.0 && image.bounds.height() >= 3.0 && share(image.bounds) < 0.02) out.append(image.bounds);
    return out;
}

double baselineAt(const QLineF &along, double x)
{
    const double dx = along.x2() - along.x1();
    return std::abs(dx) < 1e-9 ? along.y1() : along.y1() + (x - along.x1()) * (along.y2() - along.y1()) / dx;
}

bool onLine(const QRectF &area, const QLineF &along, double size)
{
    const double x0 = qMin(along.x1(), along.x2()), x1 = qMax(along.x1(), along.x2());
    const double y = baselineAt(along, qBound(x0, area.center().x(), x1));
    // A mark's area, a dash or a middle dot, ends above the baseline; a word of the
    // line below with a dot or an accent on top reaches up to it and is not on this line.
    return y >= area.top() + area.height() * 0.45 && y <= area.bottom() + size * 0.5;
}

const QRectF *ownArea(const QList<QRectF> &areas, const QLineF &along, const QLineF &word, double size, bool part)
{
    // The area drawn around a word alone: the one on its line it lies in most. Some letters of a word lie in their word's.
    const double w0 = qMin(word.x1(), word.x2()), w1 = qMax(word.x1(), word.x2());
    const QRectF *own = nullptr;
    double most = 0.0;
    for (const QRectF &area : areas) {
        if (!onLine(area, along, size) || (!part && area.width() > (w1 - w0) + size * 2.0)) continue;
        const double overlap = qMin(area.right(), w1) - qMax(area.left(), w0);
        if (overlap > most) { most = overlap; own = &area; }
    }
    return own;
}

// What of a scan's erased areas loses its pixels: each area less the
// stretches of its line whose words the edit keeps. A stretch ends on the
// edge of the area of the word kept there, which is drawn around that word
// alone and parted from the next in the white between them.
QList<QRectF> withoutKept(const QList<QRectF> &areas, const QList<ScanInk> &kept)
{
    if (kept.isEmpty()) return areas;
    QList<ScanInk> snapped = kept;
    for (ScanInk &k : snapped) {
        if (k.side == 0) continue;
        const QRectF *own = ownArea(areas, k.along, k.word, k.size);
        if (!own) continue;
        QPointF p1 = k.along.p1(), p2 = k.along.p2();
        QPointF &cut = (k.side < 0) == (p2.x() >= p1.x()) ? p2 : p1;
        cut.setX(k.side < 0 ? own->right() : own->left());
        k.along = QLineF(p1, p2);
    }
    QList<QRectF> out;
    for (const QRectF &area : areas) {
        QList<std::pair<double, double>> parts { { area.left(), area.right() } };
        for (const ScanInk &k : std::as_const(snapped)) {
            const double x0 = qMin(k.along.x1(), k.along.x2()), x1 = qMax(k.along.x1(), k.along.x2());
            if (x1 <= area.left() || x0 >= area.right() || !onLine(area, k.along, k.size)) continue;
            QList<std::pair<double, double>> rest;
            for (const auto &[a, b] : std::as_const(parts)) {
                if (x0 > a) rest.append({ a, qMin(b, x0) });
                if (x1 < b) rest.append({ qMax(a, x1), b });
            }
            parts = rest;
        }
        for (const auto &[a, b] : std::as_const(parts))
            if (b - a > 0.1) out.append(QRectF(QPointF(a, area.top()), QPointF(b, area.bottom())));
    }
    return out;
}

bool inKept(const QPointF &c, const QList<ScanInk> &kept)
{
    for (const ScanInk &k : kept) {
        const double x0 = qMin(k.along.x1(), k.along.x2()), x1 = qMax(k.along.x1(), k.along.x2());
        if (c.x() >= x0 && c.x() <= x1 && std::abs(c.y() - (baselineAt(k.along, c.x()) - k.size * 0.3)) < k.size * 0.6)
            return true;
    }
    return false;
}

// Paints over the scanned pixels of text that is replaced, in the paper's colour.
void paintOver(FPDF_PAGE page, const QList<QRectF> &areas, const QColor &paper,
               const PdfiumPageSpace &space)
{
    const QColor fill = paper.isValid() ? paper : QColor(Qt::white);
    for (const QRectF &area : areas) {
        const QRectF r = space.toPdf(area);
        FPDF_PAGEOBJECT rect = FPDFPageObj_CreateNewRect(float(r.left()), float(r.top()),
                                                         float(r.width()), float(r.height()));
        if (!rect) continue;
        FPDFPageObj_SetFillColor(rect, fill.red(), fill.green(), fill.blue(), 255);
        FPDFPath_SetDrawMode(rect, FPDF_FILLMODE_WINDING, false);
        FPDFPage_InsertObject(page, rect);
    }
}

// The form's rules around some areas of the page, in the pixels the areas are read in.
QList<QRect> rulesAround(FPDF_PAGE page, int pageIndex, const Grid &grid, const QList<QRect> &pixels, Cache *cache)
{
    QRect all;
    for (const QRect &px : pixels) all |= px;
    if (all.isEmpty() || !grid.isValid()) return {};
    const int beyond = qRound(40.0 * grid.scale);
    const QRect around = grid.within(all.adjusted(-beyond, -beyond, beyond, beyond));
    QList<QRect> rules = ScanRules::find(pixelsAt(page, pageIndex, grid, around, cache), grid.scale);
    for (QRect &r : rules) r.translate(around.topLeft());
    return rules;
}

namespace {

// The pieces of ink on a page that hang together and are as large as a letter may be, in the scan's pixels.
QList<QRect> lettersOn(FPDF_PAGE page, int pageIndex, const Grid &grid, Cache *cache)
{
    QList<QRect> out;
    if (cache && cache->findLetters(pageIndex, grid.pagePx.width(), &out)) return out;
    const QImage gray = read(page, pageIndex, grid, QRect(QPoint(), grid.pagePx), cache).convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull()) return out;
    const int w = gray.width(), h = gray.height();
    qsizetype tones[256] = {};
    for (int y = 0; y < h; ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; ++x) ++tones[row[x]];
    }
    int paper = 255;
    for (int t = 0; t < 256; ++t)
        if (tones[t] > tones[paper]) paper = t;
    const int cut = paper - 60, least = qRound(3.0 * grid.scale), most = qRound(40.0 * grid.scale);
    std::vector<uint8_t> seen(size_t(w) * size_t(h), 0);
    std::vector<int> open;
    for (int y0 = 0; y0 < h; ++y0)
        for (int x0 = 0; x0 < w; ++x0) {
            const size_t start = size_t(y0) * size_t(w) + size_t(x0);
            if (seen[start] || gray.constScanLine(y0)[x0] >= cut) continue;
            QRect box(x0, y0, 1, 1);
            open.assign(1, int(start));
            seen[start] = 1;
            while (!open.empty()) {
                const int i = open.back(), x = i % w, y = i / w;
                open.pop_back();
                box |= QRect(x, y, 1, 1);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t k = size_t(ny) * size_t(w) + size_t(nx);
                        if (seen[k] || gray.constScanLine(ny)[nx] >= cut) continue;
                        seen[k] = 1;
                        open.push_back(int(k));
                    }
            }
            if (box.height() >= least && box.height() <= most && box.width() <= most) out.append(box);
        }
    if (cache) cache->addLetters(pageIndex, grid.pagePx.width(), out);
    return out;
}

// A typed character the edit's own lines do not show, taken from elsewhere on the page: the nearest piece of
// ink as large as that character whose ink shows it before every other character, set where the typed one's
// ink would lie, its foot on that one's foot.
bool borrowed(FPDF_PAGE page, int pageIndex, const Grid &grid, const ScanMove &m, const QList<PdfiumText::Typeset::Glyphs> &shapes,
              const QList<QRect> &letters, Cache *cache, Piece *out)
{
    constexpr double kSure = 0.6, kAhead = 0.05;
    constexpr int kTried = 240;
    if (m.onto.isEmpty() || m.figure < 0) return false;
    QString chars;
    QList<QPainterPath> paths;
    for (const PdfiumText::Typeset::Glyphs &kind : shapes) {
        chars += kind.chars;
        paths += kind.paths;
    }
    const QChar typed = QChar(char16_t(m.figure));
    const qsizetype own = chars.indexOf(typed);
    if (own < 0) return false;
    // Characters the face draws alike, stretched to one frame, are as good as the typed one: no ink tells them apart.
    constexpr double kTwin = 0.9;
    QList<bool> twin;
    for (qsizetype k = 0; k < paths.size(); ++k) twin.append(k == own || ScanLike::alike(paths.at(k), paths.at(own)) >= kTwin);
    // Nor does one count that the face draws far smaller or larger than a piece is: that piece cannot be it.
    // Text set larger or smaller than the scan's takes a piece as large as the scan has it, and sets it as large as the text.
    const double scale = m.scale > 0.0 ? m.scale : 1.0;
    const auto fits = [&](qsizetype k, const QRect &b) {
        const QRectF box = paths.at(k).boundingRect();
        return std::abs(box.height() * grid.sy / scale - b.height()) <= b.height() * 0.15 + 1.0
            && std::abs(box.width() * grid.sx / scale - b.width()) <= b.width() * 0.35 + 2.0;
    };
    const QRect place(QPoint(qRound(m.onto.left() * grid.sx), qRound(m.onto.top() * grid.sy)),
                      QSize(qMax(1, qRound(m.onto.width() * grid.sx)), qMax(1, qRound(m.onto.height() * grid.sy))));
    const QSize sought(qMax(1, qRound(place.width() / scale)), qMax(1, qRound(place.height() / scale)));
    const QRect want = sought == place.size() ? place : QRect(place.center() - QPoint(sought.width() / 2, sought.height() / 2), sought);
    // The scan's own print before what an earlier edit laid on it as a picture.
    const QList<QRectF> laid = pictures(page);
    QList<std::pair<double, int>> byDistance;
    for (int i = 0; i < letters.size(); ++i) {
        const QRect &b = letters.at(i);
        if (std::abs(b.height() - want.height()) > want.height() * 0.08 + 1.0 || std::abs(b.width() - want.width()) > want.width() * 0.3 + 2.0)
            continue;
        const QPointF middle = grid.at(b).center();
        const bool foreign = std::any_of(laid.cbegin(), laid.cend(), [&](const QRectF &r) { return r.contains(middle); });
        byDistance.append({ std::hypot(double(b.center().x() - want.center().x()), 2.0 * (b.center().y() - want.center().y())) + (foreign ? 1e7 : 0.0), i });
    }
    std::sort(byDistance.begin(), byDistance.end());
    for (int n = 0; n < byDistance.size() && n < kTried; ++n) {
        const QRect &b = letters.at(byDistance.at(n).second);
        // Read with paper above and below it, by which the paper under an upright stroke is told.
        const int room = qMax(4, b.height() / 2);
        const QRect around = grid.within(b.adjusted(-3, -room, 3, room));
        const QImage piece = pixelsAt(page, pageIndex, grid, around, cache);
        if (piece.isNull()) continue;
        const QImage ink = ScanPaper::inkAlone(piece, b.translated(-around.topLeft()), around.topLeft(), {});
        const QList<double> scores = ScanLike::scores(ink, paths);
        if (scores.size() != paths.size() || scores.at(own) < kSure) continue;
        bool clear = true;
        for (qsizetype k = 0; k < scores.size() && clear; ++k)
            clear = twin.at(k) || !fits(k, b) || scores.at(k) <= scores.at(own) - kAhead;
        if (!clear) continue;
        const QPoint to(place.center().x() - b.width() / 2, place.bottom() - b.height() + 1);
        *out = { ink, grid.at(QRect(to, b.size())) };
        if (std::abs(scale - 1.0) > 1e-6) {
            const QSizeF size = out->at.size() * scale;
            out->at = QRectF(QPointF(m.onto.center().x() - size.width() / 2.0, m.onto.bottom() - size.height()), size);
        }
        return true;
    }
    return false;
}

}

QList<Piece> moved(FPDF_PAGE page, int pageIndex, const QList<QRectF> &areas,
                   const QList<ScanMove> &moves, const QList<PdfiumText::Typeset::Glyphs> &shapes, QList<int> *missed, Cache *cache)
{
    QList<Piece> out;
    if (moves.isEmpty()) return out;
    QList<const QRectF *> own;
    QRectF all;
    for (const ScanMove &m : moves) {
        own.append(m.borrowed ? nullptr : ownArea(areas, m.word, m.word, m.size, m.cutLeft || m.cutRight || m.figure >= 0));
        if (own.last()) all = all.united(*own.last());
        if (m.borrowed) all = all.united(m.onto);
    }
    // Read at the scan's own resolution and moved by whole pixels of it, so a
    // word looks the same where it goes.
    const Grid grid = gridOf(page, all);
    // A run of words ends close to its ink, so its paper covers no word beside it.
    constexpr int kAround = 4;
    QList<QRect> pixels, arounds;
    QRect unread;
    for (int i = 0; i < moves.size(); ++i) {
        const ScanMove &m = moves.at(i);
        if (!own.at(i) || !grid.isValid()) { pixels.append(QRect()); arounds.append(QRect()); continue; }
        const QRectF &area = *own.at(i);
        const double w0 = qMin(m.word.x1(), m.word.x2()), w1 = qMax(m.word.x1(), m.word.x2());
        const double reach = m.size * 0.15;
        const double left = m.cutLeft ? qMax(area.left(), w0) : m.starts ? qMax(area.left(), w0 - reach) : area.left();
        // A copied figure ends where the next letter starts.
        const double right = m.cutRight || m.figure >= 0 ? qMin(area.right(), w1) : m.ends ? qMin(area.right(), w1 + reach) : area.right();
        pixels.append(grid.within(QRect(QPoint(qRound(left * grid.sx), qFloor(area.top() * grid.sy)),
                                        QPoint(qRound(right * grid.sx) - 1, qCeil(area.bottom() * grid.sy) - 1))));
        // Read with a little around it, which tells a rule running through the piece.
        arounds.append(pixels.last().isEmpty() ? QRect() : grid.within(pixels.last().adjusted(-kAround, -kAround, kAround, kAround)));
        if (!cache || cache->find(pageIndex, arounds.last(), grid.pagePx.width()).isNull()) unread |= arounds.last();
    }
    // What the document was not asked for before is read from the page in one go.
    const Sheet sheet = sheetOf(page, pageIndex, grid, unread, cache);
    const QList<QRect> rules = rulesAround(page, pageIndex, grid, pixels, cache);
    QList<QRect> letters;
    bool lettersRead = false;
    for (int i = 0; i < moves.size(); ++i) {
        if (moves.at(i).borrowed) {
            if (!lettersRead && grid.isValid()) letters = lettersOn(page, pageIndex, grid, cache);
            lettersRead = true;
            Piece taken;
            const ScanMove &b = moves.at(i);
            if (grid.isValid() && borrowed(page, pageIndex, grid, b, shapes, letters, cache, &taken)) {
                if (b.tint.isValid() || b.opacity < 0.999) taken.pixels = ScanPaper::tinted(taken.pixels, b.tint, b.opacity);
                out.append(taken);
            } else {
                missed->append(i);
            }
            continue;
        }
        const QRect &px = pixels.at(i), &around = arounds.at(i);
        QImage piece = cache && !px.isEmpty() ? cache->find(pageIndex, around, grid.pagePx.width()) : QImage();
        if (piece.isNull() && !px.isEmpty()) {
            piece = sheet.part(around);
            if (piece.isNull()) piece = read(page, pageIndex, grid, around, cache);
            if (cache && !piece.isNull()) cache->add(pageIndex, around, grid.pagePx.width(), piece);
        }
        if (piece.isNull()) { missed->append(i); continue; }
        const ScanMove &m = moves.at(i);
        const QPointF by(qRound(m.by.x() * grid.sx) / grid.sx, qRound(m.by.y() * grid.sy) / grid.sy);
        // The word goes as its ink: the paper where it lands stays that paper.
        const QImage ink = ScanPaper::inkAlone(piece, px.translated(-around.topLeft()), around.topLeft(), rules);
        // A figure taken for one typed is that figure only where its ink shows it: OCR may have read another there.
        if (m.figure >= 0) {
            const PdfiumText::Typeset::Glyphs kind = shapes.value(m.kind);
            const qsizetype want = kind.chars.indexOf(QChar(char16_t(m.figure)));
            if (want < 0 || ScanLike::shown(ink, kind.paths) != want) { missed->append(i); continue; }
        }
        // Set larger or smaller it is the same ink as large, about where its first letter starts.
        QRectF at = grid.at(px).translated(by);
        if (std::abs(m.scale - 1.0) > 1e-6) {
            const QRectF was = grid.at(px);
            at = QRectF(m.from + m.by + (was.topLeft() - m.from) * m.scale, was.size() * m.scale);
        }
        // In the colour the user gave it, or fainter, it is the same ink.
        out.append({ m.tint.isValid() || m.opacity < 0.999 ? ScanPaper::tinted(ink, m.tint, m.opacity) : ink, at });
    }
    return out;
}

QList<Piece> cleared(FPDF_PAGE page, int pageIndex, const QList<QRectF> &areas, QList<QRectF> *missed,
                     Cache *cache)
{
    // Each area as the scan has it, its ink taken out: the paper stays the scan's own.
    QList<Piece> out;
    QRectF all;
    for (const QRectF &area : areas) all = all.united(area);
    const Grid grid = gridOf(page, all);
    // The paper around an area tells what stood under its ink; blur and a JPEG's halo reach past the ink with the resolution.
    // What an area looks like without its ink is the same for as long as the document is open.
    const int reach = qBound(2, qRound(grid.scale * 1.1), 8);
    QList<QRect> pixels, arounds;
    QRect unread;
    for (const QRectF &area : areas) {
        const QRect px = grid.isValid()
            ? grid.within(QRect(QPoint(qRound(area.left() * grid.sx), qRound(area.top() * grid.sy)),
                                QPoint(qRound(area.right() * grid.sx) - 1, qRound(area.bottom() * grid.sy) - 1)))
            : QRect();
        const int margin = qBound(10, qRound(px.height() * 0.7), 40);
        pixels.append(px);
        arounds.append(px.isEmpty() ? QRect() : grid.within(px.adjusted(-margin, -margin, margin, margin)));
        if (!px.isEmpty() && (!cache || cache->find(pageIndex, px, grid.pagePx.width(), true).isNull())) unread |= arounds.last();
    }
    const Sheet sheet = sheetOf(page, pageIndex, grid, unread, cache);
    const QList<QRect> rules = unread.isEmpty() ? QList<QRect>() : rulesAround(page, pageIndex, grid, pixels, cache);
    for (int i = 0; i < areas.size(); ++i) {
        const QRect &px = pixels.at(i), &around = arounds.at(i);
        QImage clean = cache && !px.isEmpty() ? cache->find(pageIndex, px, grid.pagePx.width(), true) : QImage();
        if (clean.isNull() && !px.isEmpty()) {
            QImage paper = sheet.part(around);
            if (paper.isNull()) paper = read(page, pageIndex, grid, around, cache);
            if (!paper.isNull()) {
                clean = ScanPaper::withoutInk(paper, px.translated(-around.topLeft()), reach, around.topLeft(),
                                              uint(px.x()) * 7919u + uint(px.y()), rules);
                if (cache) cache->add(pageIndex, px, grid.pagePx.width(), clean, true);
            }
        }
        if (clean.isNull()) { missed->append(areas.at(i)); continue; }
        out.append({ clean, grid.at(px) });
    }
    return out;
}

// How far each cut lies from where the layout has it, set in the white between two letters of its
// word: the cuts of one word in step with each other, so what lies between two of them is as many
// letters as the text has there and no letter is cut or lost. No number where the scan shows no white near.
QList<double> settledCuts(FPDF_PAGE page, int pageIndex, const QList<QRectF> &areas,
                          const QList<PdfiumText::Typeset::ScanCut> &cuts, QList<bool> *outward, Cache *cache)
{
    QList<double> shifts(cuts.size(), qQNaN());
    *outward = QList<bool>(cuts.size(), false);
    QList<const QRectF *> words;
    QList<QList<int>> ofWord;
    for (int i = 0; i < cuts.size(); ++i) {
        const QPointF &at = cuts.at(i).at;
        for (const QRectF &area : areas) {
            if (area.left() > at.x() || area.right() < at.x() || !onLine(area, QLineF(at, at + QPointF(1.0, 0.0)), cuts.at(i).size)) continue;
            const qsizetype known = words.indexOf(&area);
            if (known >= 0) ofWord[known].append(i);
            else { words.append(&area); ofWord.append(QList<int> { i }); }
            break;
        }
    }
    for (int w = 0; w < words.size(); ++w) {
        // The word's own area, as high as its ink, so a rule under the line is no ink between its letters.
        const Grid grid = gridOf(page, *words.at(w));
        const QRect px = grid.isValid() ? grid.around(*words.at(w), 0) : QRect();
        // With some rows above and below it, which a box's line between two letters runs through.
        const QRect tall = grid.within(px.adjusted(0, -px.height() / 4, 0, px.height() / 4));
        const QImage pixels = px.isEmpty() ? QImage() : pixelsAt(page, pageIndex, grid, tall, cache);
        const QList<std::pair<int, int>> runs = ScanPaper::inkRuns(pixels, px.top() - tall.top(), tall.bottom() - px.bottom(), tall.topLeft(),
                                                                   rulesAround(page, pageIndex, grid, { tall }, cache));
        if (runs.size() < 2) continue;
        const auto gapAt = [&](int g) { return (runs.at(g - 1).second + 1 + runs.at(g).first) / 2.0; };
        QList<int> order = ofWord.at(w);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cuts.at(a).before < cuts.at(b).before; });
        // As many runs as letters are still not the letters one by one where one is broken and two others
        // touch: then a white lies far from its cut though the letters stand close, or two whites lie
        // apart otherwise than their cuts.
        bool known = true;
        for (int k = 0; k < order.size() && known; ++k) {
            const PdfiumText::Typeset::ScanCut &cut = cuts.at(order.at(k));
            known = runs.size() == cut.letters && cut.before >= 1 && cut.before < runs.size();
            if (!known) break;
            const double white = runs.at(cut.before).first - runs.at(cut.before - 1).second - 1;
            const double off = gapAt(cut.before) - (cut.at.x() * grid.sx - px.left());
            if (white < cut.size * 0.3 * grid.sx && std::abs(off) > cut.size * 0.6 * grid.sx) known = false;
            if (k == 0 || cuts.at(order.at(k - 1)).before == cut.before) continue;
            const PdfiumText::Typeset::ScanCut &last = cuts.at(order.at(k - 1));
            const double want = (cut.at.x() - last.at.x()) * grid.sx, have = gapAt(cut.before) - gapAt(last.before);
            if (std::abs(have - want) > qMax(cut.size * 0.35 * grid.sx, want * 0.4)) known = false;
        }
        int lastGap = -1, lastBefore = -1;
        for (const int i : std::as_const(order)) {
            const PdfiumText::Typeset::ScanCut &cut = cuts.at(i);
            // Where the runs are the word's letters one by one, the cut is the white behind as many of
            // them. Else two letters touch or the word was misread: the white right at the place, if
            // there is one, and else the place itself, as a white further off would take a letter along.
            const double place = cut.at.x() * grid.sx - px.left(), reach = cut.size * (known ? 1.2 : 0.25) * grid.sx;
            int gap = -1;
            if (known) {
                gap = cut.before;
            } else if (lastGap >= 0 && cut.before == lastBefore) {
                gap = lastGap;
            } else {
                for (int g = qMax(1, lastGap); g < runs.size(); ++g)
                    if (gap < 0 || std::abs(gapAt(g) - place) < std::abs(gapAt(gap) - place)) gap = g;
            }
            // A white beyond the middle of the letter beside the cut is behind that letter, not at the cut.
            const double off = gap >= 1 && gap < runs.size() ? gapAt(gap) - place : 0.0;
            const bool passes = !known && (off < -cut.left * grid.sx || off > cut.right * grid.sx);
            if (gap < 1 || gap >= runs.size() || std::abs(off) > reach || passes) {
                // Two letters touch where the cut was to be: it goes outward to the next white, the word's edge
                // at the farthest, and what it passes is set anew with the typed, as a cut through ink parts a letter.
                bool inked = false;
                for (const auto &[from, to] : runs) inked = inked || (place >= from && place <= to + 1);
                if (!inked) continue;
                double edge = cut.side < 0 ? 0.0 : pixels.width();
                for (int g = 1; g < runs.size(); ++g)
                    if (cut.side < 0 ? (gapAt(g) < place && gapAt(g) > edge) : (gapAt(g) > place && gapAt(g) < edge)) edge = gapAt(g);
                shifts[i] = (px.left() + edge) / grid.sx - cut.at.x();
                (*outward)[i] = true;
                continue;
            }
            shifts[i] = (px.left() + gapAt(gap)) / grid.sx - cut.at.x();
            lastGap = gap;
            lastBefore = cut.before;
        }
    }
    return shifts;
}

Look lookOf(FPDF_PAGE page, int pageIndex, const QList<Outline> &sample, const QRectF &around, Cache *cache)
{
    // The sample is the scan's own words in the edit's face where the scan has
    // them: drawn with their outlines thickened by a few gains and held
    // against the scan's pixels there, they tell how the scan printed.
    Look look;
    QRectF all;
    for (const Outline &word : sample) all = all.united(word.path.boundingRect());
    const Grid grid = gridOf(page, all);
    QList<QRect> rects;
    QList<QList<Plane>> words;
    QList<Plane> scans;
    if (grid.isValid()) {
        const int margin = 3 + qCeil(0.6 * grid.scale);
        for (const Outline &word : sample) rects.append(grid.around(word.path.boundingRect(), margin));
        for (const double gain : ScanPaper::kGainsPx) {
            QList<Plane> thickened;
            for (int i = 0; i < sample.size(); ++i)
                thickened.append(drawn({ sample.at(i) }, gain / grid.scale, grid, rects.at(i)));
            words.append(thickened);
        }
        for (const QRect &px : std::as_const(rects))
            scans.append(ScanPaper::grayOf(pixelsAt(page, pageIndex, grid, px, cache)));
    }
    const ScanPaper::Printed printed = ScanPaper::printedLike(scans, words, qCeil(0.6 * grid.scale));
    if (!printed.found) {
        // Without words to tell by, the pixels around still tell black and white from gray.
        const Grid nearby = gridOf(page, around);
        if (nearby.isValid())
            look.bilevel = ScanPaper::bilevel(ScanPaper::grayOf(pixelsAt(page, pageIndex, nearby, nearby.around(around, 0), cache)));
        return look;
    }
    look.gainPt   = printed.gainPx / grid.scale;
    look.blurPx   = printed.blurPx;
    look.contrast = printed.contrast;
    look.bilevel  = printed.bilevel;
    look.cut      = printed.cut;
    return look;
}

bool layered(FPDF_PAGE page)
{
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i)
        if (FPDFPageObj_GetType(FPDFPage_GetObject(page, i)) == FPDF_PAGEOBJ_TEXT) return true;
    return false;
}

Edited edited(FPDF_PAGE page, int pageIndex, const EditSession::Edit &edit, PdfiumText::Typeset &plan,
              bool layered, Cache *cache)
{
    Edited out;
    if (!edit.scanned) return out;
    plan.setLayered(layered);
    // A word is cut where the scan shows white between two of its letters.
    const QList<PdfiumText::Typeset::ScanCut> cuts = plan.cutsOnScan();
    QList<bool> outward;
    const QList<double> shifts = settledCuts(page, pageIndex, edit.eraseRects, cuts, &outward, cache);
    plan.setCuts(cuts, shifts, outward);
    // A word whose pixels cannot be had is set again instead.
    if (!edit.eraseRects.isEmpty()) {
        QList<int> missed;
        const QList<ScanMove> moves = plan.movedOnScan();
        const bool taken = std::any_of(moves.cbegin(), moves.cend(), [](const ScanMove &m) { return m.figure >= 0 || m.borrowed; });
        out.moved = moved(page, pageIndex, edit.eraseRects, moves, taken ? plan.shapes() : QList<PdfiumText::Typeset::Glyphs>(), &missed, cache);
        for (const int move : std::as_const(missed)) plan.setAgain(move);
    }
    if (!plan.printedOnScan()) return out;
    // The look is the same for as long as the edit covers the same words in the same face.
    const QRectF first = edit.eraseRects.value(0, edit.pdfBounds);
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8").arg(pageIndex).arg(edit.eraseRects.size())
        .arg(first.x()).arg(first.y()).arg(edit.fontFamily).arg(edit.fontSizePt).arg(edit.bold).arg(edit.italic)
        + u'|' + plan.sampleKey();
    if (!cache || !cache->findLook(key, &out.look)) {
        out.look = lookOf(page, pageIndex, plan.sample(), first, cache);
        if (cache) cache->addLook(key, out.look);
    }
    // Set larger or smaller than the scan has its text, what is printed is as much thicker and softer.
    out.look.gainPt *= plan.scanScale();
    out.look.blurPx *= plan.scanScale();
    return out;
}

void insert(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<Piece> &pieces, const PdfiumPageSpace &space)
{
    for (const Piece &piece : pieces) {
        const bool clear = piece.pixels.hasAlphaChannel();
        const QImage src = piece.pixels.convertToFormat(clear ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        FPDF_BITMAP bitmap = FPDFBitmap_Create(src.width(), src.height(), clear ? 1 : 0);
        if (!bitmap) continue;
        auto *to = static_cast<uchar *>(FPDFBitmap_GetBuffer(bitmap));
        const int stride = FPDFBitmap_GetStride(bitmap);
        for (int y = 0; y < src.height(); ++y)
            memcpy(to + size_t(y) * size_t(stride), src.constScanLine(y), size_t(src.bytesPerLine()));
        if (FPDF_PAGEOBJECT object = FPDFPageObj_NewImageObj(doc)) {
            FPDFImageObj_SetBitmap(&page, 1, object, bitmap);
            const QPointF across = space.rightward() * piece.at.width(), up = space.upward() * piece.at.height();
            const QPointF origin = space.toPdf(piece.at.bottomLeft());
            FPDFImageObj_SetMatrix(object, across.x(), across.y(), up.x(), up.y(), origin.x(), origin.y());
            FPDFPage_InsertObject(page, object);
        }
        FPDFBitmap_Destroy(bitmap);
    }
}

namespace {

void printAlike(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<Outline> &text, const Look &look,
                const QColor &paper, const PdfiumPageSpace &space)
{
    QRectF all;
    for (const Outline &o : text) all = all.united(o.path.boundingRect());
    const Grid grid = gridOf(page, all);
    if (!grid.isValid()) return;
    const QRect px = grid.around(all, qCeil((look.gainPt + text.first().strokePt) * grid.scale + 3.0 * look.blurPx) + 2);
    if (px.isEmpty()) return;
    QRect inked;
    // A colour the user chose shows as chosen, not as dark as the scan's own ink.
    const Outline &first = text.first();
    QImage pixels = ScanPaper::printed(drawn(text, look.gainPt, grid, px),
                                       { true, look.blurPx, look.gainPt * grid.scale, first.chosen ? -1.0 : look.contrast,
                                         look.bilevel, look.cut }, first.fill, paper, &inked);
    if (inked.isEmpty()) return;
    if (first.chosen || first.opacity < 0.999) pixels = ScanPaper::tinted(pixels, first.chosen ? first.fill : QColor(), first.opacity);
    insert(doc, page, { { pixels.copy(inked), grid.at(inked.translated(px.topLeft())) } }, space);
}

}

void print(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<Outline> &text, const Look &look,
           const QColor &paper, const PdfiumPageSpace &space)
{
    // The text is drawn at the scan's resolution, thickened, blurred and faded
    // as the scan printed its own, and laid on the page as pixels in its ink: what is of one colour in one go.
    QList<Outline> rest = text;
    while (!rest.isEmpty()) {
        const Outline first = rest.first();
        QList<Outline> alike, others;
        for (const Outline &o : std::as_const(rest))
            (o.fill == first.fill && o.chosen == first.chosen && qFuzzyCompare(o.opacity + 1.0, first.opacity + 1.0) ? alike : others).append(o);
        printAlike(doc, page, alike, look, paper, space);
        rest = others;
    }
}

}

#endif
