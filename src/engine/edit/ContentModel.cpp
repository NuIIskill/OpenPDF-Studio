#include "ContentModel.hpp"

#include <QDebug>
#include <QElapsedTimer>

// ── ContentProvider (cache) ───────────────────────────────────────────────────

QList<ContentItem> ContentProvider::pageItems(int page)
{
    auto it = m_cache.find(page);
    if (it == m_cache.end()) {
        QElapsedTimer timer;
        timer.start();
        it = m_cache.insert(page, buildPage(page));
        qDebug() << "[ContentModel] built page" << page << "items=" << it->size()
                 << "in" << timer.elapsed() << "ms";
    }
    return *it;
}

ContentItem ContentProvider::itemAt(int page, const QPointF &pdfPt,
                                    unsigned typeMask, double maxDistance)
{
    return contentItemAt(pageItems(page), pdfPt, typeMask,
                         maxDistance < 0.0 ? 0.0 : maxDistance);
}

// ── qpdf backend ──────────────────────────────────────────────────────────────

#ifdef HAVE_QPDF

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>

namespace {

std::string qpdfPageContents(QPDFPageObjectHelper &ph)
{
    QPDFObjectHandle contents = ph.getObjectHandle().getKey("/Contents");
    if (contents.isNull()) return {};
    std::string result;
    auto append = [&](QPDFObjectHandle s) {
        if (!s.isStream()) return;
        auto data = s.getStreamData(qpdf_dl_all);
        result.append(reinterpret_cast<const char *>(data->getBuffer()),
                      data->getSize());
        result += '\n';
    };
    if (contents.isArray())
        for (int i = 0; i < contents.getArrayNItems(); ++i)
            append(contents.getArrayItem(i));
    else
        append(contents);
    return result;
}

} // namespace

QpdfContentProvider::QpdfContentProvider(const QString &filePath,
                                         std::function<QSizeF(int)> pageSizePts)
    : m_path(filePath)
    , m_pageSize(std::move(pageSizePts))
{
}

QpdfContentProvider::~QpdfContentProvider() = default;

QList<ContentItem> QpdfContentProvider::buildPage(int page)
{
    try {
        if (!m_qpdf) {
            auto qpdf = std::make_unique<QPDF>();
            qpdf->processFile(m_path.toLocal8Bit().constData());
            m_qpdf = std::move(qpdf);
        }
        QPDF &input = *m_qpdf;
        QPDFPageDocumentHelper pdh(input);
        auto pages = pdh.getAllPages();
        if (page < 0 || page >= static_cast<int>(pages.size())) return {};

        auto &ph = pages[static_cast<std::size_t>(page)];
        const std::string cs = qpdfPageContents(ph);

        const QSizeF size = m_pageSize ? m_pageSize(page) : QSizeF();
        double pageH = size.height();
        if (pageH <= 0.0) {
            // Fall back to /MediaBox (inherited) when the caller can't help.
            QPDFObjectHandle mb = ph.getAttribute("/MediaBox", false);
            if (mb.isArray() && mb.getArrayNItems() == 4)
                pageH = std::abs(mb.getArrayItem(3).getNumericValue()
                               - mb.getArrayItem(1).getNumericValue());
            else
                pageH = 842.0;
        }

        return qpdfBuildPageItems(cs, pageH, ph.getObjectHandle());
    } catch (const std::exception &ex) {
        qWarning() << "[ContentModel] qpdf build failed for page" << page
                   << ":" << ex.what();
        m_qpdf.reset();   // don't keep a half-parsed instance around
        return {};
    } catch (...) {
        m_qpdf.reset();
        return {};
    }
}

#endif // HAVE_QPDF

// ── Poppler backend ───────────────────────────────────────────────────────────

#ifdef HAVE_POPPLER

#include <poppler/qt6/poppler-qt6.h>
#include <poppler/qt6/poppler-form.h>
#include <poppler/qt6/poppler-annotation.h>

PopplerContentProvider::PopplerContentProvider(Poppler::Document *doc)
    : m_doc(doc)
{
}

QList<ContentItem> PopplerContentProvider::buildPage(int page)
{
    // Poppler can throw on malformed files or missing substitute fonts —
    // detection must degrade to "nothing found", never crash.
    try {
        return buildPageUnguarded(page);
    } catch (const std::exception &ex) {
        qWarning() << "[ContentModel] poppler build failed for page" << page
                   << ":" << ex.what();
        return {};
    } catch (...) {
        return {};
    }
}

QList<ContentItem> PopplerContentProvider::buildPageUnguarded(int page)
{
    if (!m_doc) return {};
    const auto popplerPage = m_doc->page(page);
    if (!popplerPage) return {};

    const QSizeF pageSize = popplerPage->pageSizeF();

    // Text words → clusters. Poppler gives exact glyph boxes; the word-box
    // height approximates ascent+descent, so font size ≈ height × 0.9.
    QList<ContentCluster> clusters;
    const auto words = popplerPage->textList();
    clusters.reserve(static_cast<int>(words.size()));
    for (const auto &tb : words) {
        ContentCluster c;
        c.bounds     = tb->boundingBox();
        c.text       = tb->text();
        // Word-box height ≈ ascent+descent ≈ the font size itself.
        c.fontSizePt = std::max(2.0, c.bounds.height() * 1.05);
        c.exactWidth = true;
        clusters.append(std::move(c));
    }
    QList<ContentItem> items = classifyContentClusters(clusters);

    // AcroForm text fields. FormField::rect() is normalized [0..1] relative
    // to the page (top-left origin) — scale to PDF points.
    QList<ContentItem> fields;
    for (const auto &field : popplerPage->formFields()) {
        if (!field || field->type() != Poppler::FormField::FormText) continue;
        const QRectF r = field->rect();
        ContentItem item;
        item.type      = ContentItem::Type::FormField;
        item.bounds    = QRectF(r.x() * pageSize.width(),  r.y() * pageSize.height(),
                                r.width() * pageSize.width(),
                                r.height() * pageSize.height());
        item.fieldName = field->name();
        if (const auto *text = dynamic_cast<const Poppler::FormFieldText *>(field.get())) {
            item.text = text->text();
            const double fs = text->getFontSize();
            if (fs > 0.0) item.fontSizePt = fs;
        }
        if (item.isValid())
            fields.append(std::move(item));
    }

    // Media annotations (video/sound): Screen, RichMedia, Movie.
    QList<ContentItem> media;
    for (const auto &ann : popplerPage->annotations()) {
        if (!ann) continue;
        const auto st = ann->subType();
        if (st != Poppler::Annotation::AScreen
            && st != Poppler::Annotation::ARichMedia
            && st != Poppler::Annotation::AMovie)
            continue;
        const QRectF r = ann->boundary();   // normalized, top-left origin
        ContentItem item;
        item.type   = ContentItem::Type::Media;
        item.bounds = QRectF(r.x() * pageSize.width(), r.y() * pageSize.height(),
                             r.width() * pageSize.width(),
                             r.height() * pageSize.height());
        if (item.isValid())
            media.append(std::move(item));
    }

    QList<ContentItem> result;
    result.reserve(fields.size() + items.size() + media.size());
    result += fields;
    result += items;
    result += media;
    return result;
}

#endif // HAVE_POPPLER
