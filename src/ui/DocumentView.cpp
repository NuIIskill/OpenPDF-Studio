#include "DocumentView.hpp"
#include "ui/widgets/PagePlaceholder.hpp"

#include <QVBoxLayout>
#include <QWidget>

DocumentView::DocumentView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("DocumentView"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setAlignment(Qt::AlignCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_canvas = new QWidget(this);
    m_canvas->setObjectName(QStringLiteral("DocumentCanvas"));

    auto *layout = new QVBoxLayout(m_canvas);
    layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    layout->setContentsMargins(40, 40, 40, 40);

    m_page = new PagePlaceholder(1, m_canvas);
    layout->addWidget(m_page, 0, Qt::AlignHCenter);

    setWidget(m_canvas);
    updatePageSize();
}

void DocumentView::setZoom(int percent)
{
    if (m_zoom == percent) return;
    m_zoom = percent;
    updatePageSize();
}

void DocumentView::updatePageSize()
{
    const qreal scale = m_zoom / 100.0;
    m_page->setFixedSize(static_cast<int>(595 * scale),
                         static_cast<int>(842 * scale));
}
