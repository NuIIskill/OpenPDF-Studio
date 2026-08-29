#include "ui/view/HoverHighlight.hpp"

#include "ui/view/PageCanvas.hpp"

#include <QFrame>
#include <QLabel>
#include <QWidget>

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/ContentMap.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/edit/EditSession.hpp"
#endif

HoverHighlight::HoverHighlight(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{

    m_frame = new QFrame(canvas->canvasWidget());
    m_frame->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_frame->hide();
}

void HoverHighlight::hide()
{
    m_frame->hide();
    m_page   = -1;
    m_bounds = QRectF();
}

#ifdef HAVE_PDF_RENDERING

void HoverHighlight::setSource(ContentProvider *provider, EditSession *session)
{
    m_provider = provider;
    m_session  = session;
    hide();
}

void HoverHighlight::showAt(const QPoint &canvasPos)
{
    if (!m_provider) return;

    if (m_editorFrame && m_editorFrame->isVisible()
            && m_editorFrame->geometry().contains(canvasPos)) {
        hide();
        return;
    }

    auto [pageIdx, pageLbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) { hide(); return; }

    const qreal   scale = m_canvas->screenScale();
    const QPointF pdfPt = QPointF(canvasPos - pageLbl->pos()) / scale;

    if (!m_provider->hasPage(pageIdx)) { hide(); return; }

    if (m_session && m_session->isBlankAt(pageIdx, pdfPt)) { hide(); return; }

    const ContentItem item = m_provider->itemAt(pageIdx, pdfPt,
                                                kAllContentTypes, 6.0);
    if (!item.isValid()) { hide(); return; }

    if (m_session && m_session->isBlankCovering(pageIdx, item.bounds)) {
        hide();
        return;
    }

    if (pageIdx == m_page && item.bounds == m_bounds && m_frame->isVisible())
        return;
    m_page   = pageIdx;
    m_bounds = item.bounds;

    const char *border = "#3B82F6";
    QString label      = tr("Text");
    switch (item.type) {
    case ContentItem::Type::Paragraph: label = tr("Paragraph");                     break;
    case ContentItem::Type::TableCell: label = tr("Table cell"); border = "#10B981"; break;
    case ContentItem::Type::FormField: label = tr("Form field"); border = "#F59E0B"; break;
    case ContentItem::Type::Image:     label = tr("Image");      border = "#8B5CF6"; break;
    case ContentItem::Type::Media:     label = tr("Media");      border = "#EF4444"; break;
    default: break;
    }

    QString tip = label;
    if (!item.fontFamily.isEmpty() && item.fontSizePt > 0.0)
        tip += QStringLiteral(" — %1 %2 pt").arg(item.fontFamily)
                   .arg(qRound(item.fontSizePt));
    else if (item.fontSizePt > 0.0)
        tip += QStringLiteral(" — %1 pt").arg(qRound(item.fontSizePt));
    if (!item.fieldName.isEmpty())
        tip += QStringLiteral(" (%1)").arg(item.fieldName);

    m_frame->setStyleSheet(QStringLiteral(
        "QFrame { border: 1px dashed %1; border-radius: 2px;"
        " background: transparent; }").arg(QLatin1String(border)));
    m_frame->setToolTip(tip);

    const QRectF canvasRect(item.bounds.topLeft() * scale + QPointF(pageLbl->pos()),
                            item.bounds.size() * scale);
    m_frame->setGeometry(canvasRect.toAlignedRect().adjusted(-2, -2, 2, 2));
    m_frame->raise();
    m_frame->show();
}

#else

void HoverHighlight::showAt(const QPoint &canvasPos)
{
    Q_UNUSED(canvasPos)
}

#endif
