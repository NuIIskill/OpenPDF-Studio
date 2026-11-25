#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumImages.hpp"
#include "engine/document/PdfiumLock.hpp"
#include "engine/document/PdfiumScanText.hpp"

#include "fpdf_edit.h"
#include "fpdf_text.h"

void PdfiumBackend::setScanText(int page, const QList<ScanLine> &lines)
{
    PdfiumLock lock;
    if (!m_doc || page < 0 || page >= pageCount()) return;
    if (!m_scanText) m_scanText = std::make_unique<PdfiumScanText>();
    m_scanText->set(page, pageSizePts(page), lines);
}

bool PdfiumBackend::hasScanText(int page) const
{
    PdfiumLock lock;
    return m_scanText && m_scanText->has(page);
}

// A scan that OCR read is read as the text it holds.
bool PdfiumBackend::readScan(int page, FPDF_PAGE &pg, FPDF_TEXTPAGE &text) const
{
    if (!m_scanText || !m_scanText->has(page)) return false;
    if (hasSelectableText(page)) {
        // The picture under a little text of the page's own is a scan where it holds far more text than that:
        // a photograph with a caption set onto it stays the page of text it is.
        const int shown = shownOnScan(page);
        if (!scanBelowText(page) || (shown > 0 && m_scanText->boxes(page).size() < qMax(shown * 4, 120))) return false;
    }
    pg   = m_scanText->page(page);
    text = m_scanText->text(page);
    return pg && text;
}

// How many characters of its own a page shows over a picture that covers it; below zero where no such picture lies under them.
int PdfiumBackend::shownOnScan(int page) const
{
    if (const auto known = m_shownOnScan.constFind(page); known != m_shownOnScan.cend()) return *known;
    int shown = -1;
    FPDF_PAGE pg = readPage(page);
    FPDF_TEXTPAGE tp = readText(page);
    const QSizeF size = pageSizePts(page);
    bool picture = false;
    if (pg && tp)
        for (const PdfiumImages::Found &image : PdfiumImages::find(pg))
            picture = picture || image.bounds.width() * image.bounds.height() >= size.width() * size.height() * 0.5;
    if (picture) {
        shown = 0;
        for (int i = 0, n = FPDFText_CountChars(tp); i < n; ++i) {
            if (FPDFText_IsGenerated(tp, i) == 1 || FPDFText_GetUnicode(tp, i) <= 32) continue;
            FPDF_PAGEOBJECT obj = FPDFText_GetTextObject(tp, i);
            const int mode = obj ? FPDFTextObj_GetTextRenderMode(obj) : -1;
            shown += mode != FPDF_TEXTRENDERMODE_INVISIBLE && mode != FPDF_TEXTRENDERMODE_CLIP;
        }
    }
    m_shownOnScan.insert(page, shown);
    return shown;
}

namespace {

// Whether a picture is made of flat blocks of several tones, as a smudge an edit left for paper is: print has
// one tone for its paper and strokes thinner than any block, a piece of a scan its grain.
bool blocks(const QImage &pixels)
{
    const int w = pixels.width(), h = pixels.height();
    if (w < 6 || h < 6) return false;
    qsizetype flat = 0, tones[256] = {};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int here = qGray(pixels.pixel(x, y));
            ++tones[here];
            flat += std::abs(qGray(pixels.pixel(qMin(x + 1, w - 1), y)) - here) <= 2 && std::abs(qGray(pixels.pixel(x, qMin(y + 1, h - 1))) - here) <= 2;
        }
    const qsizetype all = qsizetype(w) * h;
    // The tones that each fill a twentieth of it and more, a few steps of gray taken as one.
    int filled = 0;
    qsizetype most = 0;
    for (int t = 0; t < 256; t += 6) {
        qsizetype share = 0;
        for (int k = t; k < t + 6 && k < 256; ++k) share += tones[k];
        most = qMax(most, share);
        filled += share * 20 >= all;
    }
    return flat * 20 >= all * 9 && most * 2 < all && filled >= 3;
}

}

QList<QRectF> PdfiumBackend::scanPatches(int page) const
{
    // Small opaque pictures over a page's scan that hold no ink, as an edit leaves where it cleared a
    // word: paper a little off the scan's, which OCR would read as letters. Nor does one hold print that
    // shows no paper for it to stand on, as a smudge of many tones laid over a word.
    constexpr int kInk = 60, kPaper = 30;
    PdfiumLock lock;
    if (!m_doc || page < 0 || page >= pageCount()) return {};
    if (const auto known = m_scanPatches.constFind(page); known != m_scanPatches.cend()) return *known;
    QList<QRectF> patches, pictures;
    FPDF_PAGE pg = readPage(page);
    const QSizeF size = pageSizePts(page);
    const QList<PdfiumImages::Found> images = pg ? PdfiumImages::find(pg) : QList<PdfiumImages::Found>();
    const auto wide = [&size](const QRectF &b, double share) { return b.width() * b.height() >= size.width() * size.height() * share; };
    const bool scan = std::any_of(images.cbegin(), images.cend(), [&](const PdfiumImages::Found &image) { return wide(image.bounds, 0.5); });
    // What the page draws over a picture later shows instead of it: a picture under letters set on it is none.
    const auto covered = [pg](FPDF_PAGEOBJECT picture) {
        float left = 0, bottom = 0, right = 0, top = 0;
        if (!FPDFPageObj_GetBounds(picture, &left, &bottom, &right, &top)) return true;
        bool later = false;
        for (int i = 0, n = FPDFPage_CountObjects(pg); i < n; ++i) {
            FPDF_PAGEOBJECT other = FPDFPage_GetObject(pg, i);
            if (other == picture) { later = true; continue; }
            float l = 0, b = 0, r = 0, t = 0;
            if (later && FPDFPageObj_GetBounds(other, &l, &b, &r, &t) && l < right && r > left && b < top && t > bottom) return true;
        }
        return !later;
    };
    for (const PdfiumImages::Found &image : images) {
        const QRectF &b = image.bounds;
        if (!scan || b.width() < 3.0 || b.height() < 3.0 || wide(b, 0.02) || covered(image.object)) continue;
        const QImage pixels = PdfiumImages::extract(m_doc, pg, image).convertToFormat(QImage::Format_ARGB32);
        qsizetype tones[256] = {}, solid = 0, clear = 0;
        for (int y = 0; y < pixels.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
            for (int x = 0; x < pixels.width(); ++x) {
                if (qAlpha(row[x]) < 250) { ++clear; continue; }
                ++tones[qGray(row[x])];
                ++solid;
            }
        }
        // Ink laid on the scan alone is see-through around its strokes.
        if (solid == 0 || clear * 20 > solid) continue;
        pictures.append(b);
        int dark = 0, middle = 0, light = 255;
        for (qsizetype seen = 0; dark < 255 && (seen += tones[dark]) * 50 < solid;) ++dark;
        for (qsizetype seen = 0; middle < 255 && (seen += tones[middle]) * 2 < solid;) ++middle;
        for (qsizetype seen = 0; light > 0 && (seen += tones[light]) * 50 < solid;) --light;
        qsizetype paper = 0;
        for (int t = qMax(0, light - kPaper); t < 256; ++t) paper += tones[t];
        if (middle - dark <= kInk || paper * 5 < solid * 2 || blocks(pixels)) patches.append(b);
    }
    m_scanPatches.insert(page, patches);
    m_scanPictures.insert(page, pictures);
    return patches;
}

QList<QRectF> PdfiumBackend::scanPictures(int page) const
{
    // All small opaque pictures over a page's scan, with print or without: what an earlier edit laid there.
    scanPatches(page);
    PdfiumLock lock;
    return m_scanPictures.value(page);
}

bool PdfiumBackend::scanBelowText(int page) const
{
    // A scan another program read, its text drawn invisibly over the page's picture, or one with a little
    // text set onto it, as a stamp or a page number. Such a layer may miss whole tables, and the stamp
    // tells nothing of the scan: the page is read again as any scan is.
    constexpr int kLittle = 200;
    PdfiumLock lock;
    if (!m_doc) return false;
    const int shown = shownOnScan(page);
    return shown >= 0 && shown <= kLittle;
}

QList<PdfBackend::TextMatch> PdfiumBackend::findScanText(int page, const QString &text) const
{
    PdfiumLock lock;
    return m_scanText ? m_scanText->find(page, text) : QList<TextMatch>();
}

#endif
