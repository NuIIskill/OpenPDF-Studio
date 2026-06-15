#include "StatusBar.hpp"
#include "ui/widgets/IconButton.hpp"
#include "ui/theme/Theme.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("StatusBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buildLayout();
}

void StatusBar::setPageInfo(int current, int total)
{
    m_pageInput->setText(QString::number(current));
    m_totalLabel->setText(QStringLiteral("/ %1").arg(total));
}

void StatusBar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(0);

    layout->addStretch(1);

    m_prevBtn = new IconButton(this);
    m_prevBtn->setIconName(QStringLiteral("chevron-left"), Theme::IconMuted);
    m_prevBtn->setToolTip(tr("Vorherige Seite"));
    connect(m_prevBtn, &QPushButton::clicked, this, &StatusBar::previousPageRequested);
    layout->addWidget(m_prevBtn);

    layout->addSpacing(4);

    m_pageInput = new QLineEdit(QStringLiteral("1"), this);
    m_pageInput->setObjectName(QStringLiteral("PageInput"));
    m_pageInput->setFixedWidth(36);
    m_pageInput->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_pageInput);

    layout->addSpacing(6);

    m_totalLabel = new QLabel(QStringLiteral("/ 1"), this);
    m_totalLabel->setObjectName(QStringLiteral("PageTotalLabel"));
    layout->addWidget(m_totalLabel);

    layout->addSpacing(4);

    m_nextBtn = new IconButton(this);
    m_nextBtn->setIconName(QStringLiteral("chevron-right"), Theme::IconMuted);
    m_nextBtn->setToolTip(tr("Nächste Seite"));
    connect(m_nextBtn, &QPushButton::clicked, this, &StatusBar::nextPageRequested);
    layout->addWidget(m_nextBtn);

    layout->addStretch(1);

    auto *panelBtn = new IconButton(this);
    panelBtn->setIconName(QStringLiteral("panel-right"), Theme::IconMuted);
    panelBtn->setToolTip(tr("Panel umschalten"));
    layout->addWidget(panelBtn);
}
