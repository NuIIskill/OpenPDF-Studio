#include "DocumentView.hpp"

#include "ui/widgets/PagePlaceholder.hpp"

#include <QVBoxLayout>
#include <QWidget>
#include <QScrollBar>

DocumentView::DocumentView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("DocumentView"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(false);           // we manage the container size manually
    setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_container = new QWidget(this);
    m_container->setObjectName(QStringLiteral("DocumentContainer"));
    m_container->setStyleSheet(QStringLiteral(
        "#DocumentContainer { background: #F1F5F9; }"));

    setWidget(m_container);

    rebuildPages();
}

void DocumentView::setPageCount(int count)
{
    if (m_pageCount == count)
        return;
    m_pageCount = count;
    rebuildPages();
}

void DocumentView::setZoom(int percent)
{
    if (m_zoom == percent)
        return;
    m_zoom = percent;
    rebuildPages();
    Q_EMIT zoomChanged(percent);
}

void DocumentView::rebuildPages()
{
    // Delete existing layout and children
    if (auto *old = m_container->layout()) {
        QLayoutItem *item;
        while ((item = old->takeAt(0)) != nullptr) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        delete old;
    }

    const qreal scale = m_zoom / 100.0;
    const int   pageW = static_cast<int>(595 * scale);
    const int   pageH = static_cast<int>(842 * scale);
    const int   gap   = 24;
    const int   padH  = 32;
    const int   padV  = 32;

    auto *layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(padH, padV, padH, padV);
    layout->setSpacing(gap);
    layout->setAlignment(Qt::AlignHCenter);

    for (int i = 1; i <= m_pageCount; ++i) {
        auto *page = new PagePlaceholder(i, m_container);
        page->setFixedSize(pageW, pageH);
        layout->addWidget(page, 0, Qt::AlignHCenter);
    }

    // Size the container so the scroll area can measure it
    const int totalH = padV * 2 + m_pageCount * pageH + (m_pageCount - 1) * gap;
    const int totalW = padH * 2 + pageW;
    m_container->setMinimumSize(totalW, totalH);
}
