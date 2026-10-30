#include "ui/redact/RedactionPanel.hpp"

#include "ui/theme/Theme.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace {

QPointer<RedactionPanel> g_instance;

class RegionCard : public QFrame
{
public:
    RegionCard(int number, int page, bool selected, std::function<void()> activated,
               std::function<void()> removed, QWidget *parent)
        : QFrame(parent), m_activated(std::move(activated))
    {
        setObjectName(QStringLiteral("RedactionCard"));
        setProperty("selected", selected);
        setCursor(Qt::PointingHandCursor);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(12, 12, 10, 12);
        row->setSpacing(12);
        auto *badge = new QLabel(QString::number(number), this);
        badge->setObjectName(QStringLiteral("RedactionNumber"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(32, 30);
        row->addWidget(badge);

        auto *text = new QVBoxLayout;
        text->setSpacing(3);
        auto *title = new QLabel(RedactionPanel::tr("Region %1").arg(number), this);
        title->setObjectName(QStringLiteral("RedactionCardTitle"));
        text->addWidget(title);
        auto *meta = new QLabel(RedactionPanel::tr("Page %1").arg(page + 1), this);
        meta->setObjectName(QStringLiteral("RedactionCardMeta"));
        text->addWidget(meta);
        row->addLayout(text, 1);

        auto *remove = new QPushButton(this);
        remove->setObjectName(QStringLiteral("RedactionRemove"));
        remove->setIcon(Theme::makeIcon(QStringLiteral("trash-2"), Theme::IconMuted));
        remove->setToolTip(RedactionPanel::tr("Remove region"));
        remove->setFixedSize(28, 30);
        connect(remove, &QPushButton::clicked, this, std::move(removed));
        row->addWidget(remove);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            if (m_activated) m_activated();
            event->accept();
            return;
        }
        QFrame::mouseReleaseEvent(event);
    }

private:
    std::function<void()> m_activated;
};

}

RedactionPanel::RedactionPanel(QWidget *parent)
    : QWidget(parent)
{
    g_instance = this;
    setObjectName(QStringLiteral("RedactionPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 22, 20, 20);
    root->setSpacing(16);

    auto *header = new QHBoxLayout;
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("RedactionTitle"));
    header->addWidget(m_title);
    header->addStretch();
    m_close = new QPushButton(this);
    m_close->setObjectName(QStringLiteral("RedactionClose"));
    m_close->setFixedSize(30, 30);
    connect(m_close, &QPushButton::clicked, this, &RedactionPanel::closeRequested);
    header->addWidget(m_close);
    root->addLayout(header);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("RedactionScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_list = new QWidget(scroll);
    m_list->setObjectName(QStringLiteral("RedactionList"));
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(12);
    scroll->setWidget(m_list);
    root->addWidget(scroll, 1);

    retranslateUi();
    refreshIcons();
}

RedactionPanel::~RedactionPanel() = default;

RedactionPanel *RedactionPanel::instance()
{
    return g_instance.data();
}

void RedactionPanel::setRegions(const QList<int> &pages, int selected)
{
    m_pages    = pages;
    m_selected = selected;
    rebuildList();
}

void RedactionPanel::rebuildList()
{
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    for (int i = 0; i < m_pages.size(); ++i)
        m_listLayout->addWidget(new RegionCard(i + 1, m_pages.at(i), i == m_selected,
            [this, i]() { Q_EMIT regionActivated(i); },
            [this, i]() { Q_EMIT removeRequested(i); }, m_list));
    m_listLayout->addStretch();
}

void RedactionPanel::retranslateUi()
{
    m_title->setText(tr("Redact"));
    m_close->setToolTip(tr("Close"));
    rebuildList();
}

void RedactionPanel::refreshIcons()
{
    m_close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
}

void RedactionPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) retranslateUi();
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        refreshIcons();
        rebuildList();
    }
    QWidget::changeEvent(event);
}
