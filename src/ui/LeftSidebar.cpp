#include "LeftSidebar.hpp"

#include "ui/widgets/ThumbnailItem.hpp"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

LeftSidebar::LeftSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LeftSidebar"));
    setFixedWidth(220);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // ── Outer layout ──────────────────────────────────────────────────────
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Header ────────────────────────────────────────────────────────────
    auto *header = new QLabel(tr("SEITEN"), this);
    header->setObjectName(QStringLiteral("SidebarHeader"));
    header->setContentsMargins(12, 14, 12, 10);
    outer->addWidget(header);

    // ── Scroll area ───────────────────────────────────────────────────────
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("SidebarScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget(m_scrollArea);
    container->setObjectName(QStringLiteral("SidebarContainer"));
    container->setStyleSheet(QStringLiteral(
        "#SidebarContainer { background: transparent; }"));

    m_thumbnailLayout = new QVBoxLayout(container);
    m_thumbnailLayout->setContentsMargins(0, 0, 0, 8);
    m_thumbnailLayout->setSpacing(2);
    m_thumbnailLayout->addStretch(1);

    m_scrollArea->setWidget(container);
    outer->addWidget(m_scrollArea, 1);

    // Pre-populate with 3 placeholder thumbnails
    setPageCount(3);
    if (!m_items.isEmpty())
        m_items.first()->setSelected(true);
}

void LeftSidebar::setPageCount(int count)
{
    clearThumbnails();
    for (int i = 1; i <= count; ++i) {
        auto *item = new ThumbnailItem(i, m_scrollArea->widget());
        connect(item, &ThumbnailItem::clicked,
                this, &LeftSidebar::pageClicked);
        connect(item, &ThumbnailItem::clicked, this, [this](int page) {
            setCurrentPage(page);
        });
        // Insert before the trailing stretch
        m_thumbnailLayout->insertWidget(m_thumbnailLayout->count() - 1, item);
        m_items.append(item);
    }
}

void LeftSidebar::setCurrentPage(int page)
{
    for (auto *item : m_items) {
        item->setSelected(item->pageNumber() == page);
    }
}

void LeftSidebar::clearThumbnails()
{
    for (auto *item : m_items) {
        m_thumbnailLayout->removeWidget(item);
        item->deleteLater();
    }
    m_items.clear();
}
