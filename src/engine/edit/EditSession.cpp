#include "EditSession.hpp"

#include <QPainter>
#include <QFont>

#ifdef HAVE_QT_PDF
#include <QPdfWriter>
#include <QPageSize>
#endif

// ── Mutation ──────────────────────────────────────────────────────────────────

void EditSession::addEdit(int page, const QRectF &pdfBounds, const QString &newText)
{
    // Replace any existing edit at the same location
    removeEdit(page, pdfBounds);
    m_edits.append({ page, pdfBounds, newText });
}

void EditSession::removeEdit(int page, const QRectF &pdfBounds)
{
    m_edits.removeIf([&](const Edit &e) {
        return e.page == page && e.pdfBounds == pdfBounds;
    });
}

void EditSession::clear()
{
    m_edits.clear();
}

// ── Queries ───────────────────────────────────────────────────────────────────

bool EditSession::hasEditsOnPage(int page) const
{
    for (const auto &e : m_edits)
        if (e.page == page) return true;
    return false;
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void EditSession::applyToImage(int page, QImage &img, qreal scale) const
{
    if (img.isNull() || !hasEditsOnPage(page)) return;
    QPainter p(&img);
    for (const auto &e : m_edits)
        if (e.page == page) paintEdit(p, e, scale);
}

void EditSession::paintEdit(QPainter &p, const Edit &e, qreal scale)
{
    const QRectF px(e.pdfBounds.topLeft() * scale, e.pdfBounds.size() * scale);
    // Erase original text
    p.fillRect(px, Qt::white);
    // Draw replacement
    QFont f = p.font();
    f.setPixelSize(qMax(8, int(px.height() * 0.78)));
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(px.toRect(), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
               e.newText);
}

// ── Save ──────────────────────────────────────────────────────────────────────

#ifdef HAVE_QT_PDF
bool EditSession::saveToFile(const QString &path, QPdfDocument *doc,
                              int pageCount) const
{
    if (pageCount <= 0) return false;

    QPdfWriter writer(path);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(150);

    // Set page layout from the first page (covers the common same-size case)
    const QSizeF firstPts = doc->pagePointSize(0);
    writer.setPageSize(QPageSize(firstPts, QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter(&writer);
    if (!painter.isActive()) return false;

    constexpr qreal kSaveDpi = 150.0;
    constexpr qreal kPts2Px  = kSaveDpi / 72.0;

    for (int i = 0; i < pageCount; ++i) {
        if (i > 0 && !writer.newPage()) return false;

        const QSizeF pts  = doc->pagePointSize(i);
        const QSize  px(int(pts.width() * kPts2Px), int(pts.height() * kPts2Px));

        QImage img = doc->render(i, px);
        if (img.isNull()) continue;

        applyToImage(i, img, kPts2Px);

        const QRect pageRect(0, 0, painter.device()->width(),
                             painter.device()->height());
        painter.drawImage(pageRect, img);
    }

    painter.end();
    return true;
}
#endif
