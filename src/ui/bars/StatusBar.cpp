#include "ui/bars/StatusBar.hpp"
#include "engine/document/DocumentInfo.hpp"
#include "ui/info/InfoText.hpp"
#include "ui/widgets/IconButton.hpp"
#include "ui/theme/Theme.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QPushButton>

namespace {

constexpr int kSideWidth = 160;

}

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("StatusBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buildLayout();
    setPageInfo(1, 1);
    setDocumentInfo(false, nullptr);
}

StatusBar::~StatusBar() = default;

void StatusBar::setPageInfo(int current, int total)
{
    m_totalPages  = qMax(1, total);
    m_currentPage = qBound(1, current, m_totalPages);
    m_validator->setTop(m_totalPages);

    if (!m_pageInput->hasFocus())
        m_pageInput->setText(QString::number(m_currentPage));
    m_totalLabel->setText(QStringLiteral("/ %1").arg(m_totalPages));

    m_prevBtn->setEnabled(m_currentPage > 1);
    m_nextBtn->setEnabled(m_currentPage < m_totalPages);
}

void StatusBar::setDocumentInfo(bool open, const DocumentInfo *info)
{
    m_infoOpen = open;
    m_info = open && info ? std::make_unique<DocumentInfo>(*info) : nullptr;
    refreshInfoBadges();
}

void StatusBar::setHint(const QString &text)
{
    m_hintLabel->setText(text);
}

void StatusBar::refreshInfoBadges()
{
    m_infoBtn->setVisible(m_infoOpen);
    const bool signatures = m_info && m_info->signatureCount() > 0;
    m_signatureBadge->setVisible(signatures);
    if (signatures) {
        const QColor tint = InfoText::signatureColor(*m_info);
        m_signatureBadge->setIcon(Theme::makeIcon(InfoText::signatureIcon(*m_info), tint, tint,
                                                  tint, 18));
        m_signatureBadge->setText(QString::number(m_info->signatureCount()));
        m_signatureBadge->setToolTip(InfoText::signatureSummary(*m_info));
    }
    const bool encrypted = m_info && m_info->encrypted;
    m_lockBadge->setVisible(encrypted);
    if (encrypted) {
        m_lockBadge->setIcon(Theme::makeIcon(QStringLiteral("lock"), Theme::Primary,
                                             Theme::Primary, Theme::Primary, 18));
        m_lockBadge->setToolTip(InfoText::encryptionSummary(*m_info));
    }
}

void StatusBar::commitPageInput()
{
    bool ok = false;
    const int page = m_pageInput->text().trimmed().toInt(&ok);

    if (!ok || page < 1 || page > m_totalPages) {
        m_pageInput->setText(QString::number(m_currentPage));
        return;
    }

    if (page != m_currentPage)
        Q_EMIT pageRequested(page);
}

void StatusBar::refreshTheme()
{
    const QColor mc = Theme::IconMuted;
    m_prevBtn->setIconName(m_prevBtn->iconName(), mc);
    m_nextBtn->setIconName(m_nextBtn->iconName(), mc);
    if (m_panelBtn)
        m_panelBtn->setIconName(m_panelBtn->iconName(), mc);
    m_infoBtn->setIconName(m_infoBtn->iconName(), mc);
    refreshInfoBadges();
}

void StatusBar::retranslateUi()
{
    m_prevBtn->setToolTip(tr("Previous Page"));
    m_nextBtn->setToolTip(tr("Next Page"));
    m_pageInput->setToolTip(tr("Go to page"));
    if (m_panelBtn)
        m_panelBtn->setToolTip(tr("Toggle Panel"));
    m_infoBtn->setToolTip(tr("Document information"));
    refreshInfoBadges();
}

void StatusBar::buildInfoGroup(QHBoxLayout *layout)
{
    m_infoGroup = new QWidget(this);
    m_infoGroup->setFixedWidth(kSideWidth);
    auto *row = new QHBoxLayout(m_infoGroup);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    m_infoBtn = new IconButton(m_infoGroup);
    m_infoBtn->setIconName(QStringLiteral("info"), Theme::IconMuted);
    m_infoBtn->setToolTip(tr("Document information"));
    connect(m_infoBtn, &QPushButton::clicked, this, &StatusBar::infoRequested);
    row->addWidget(m_infoBtn);

    m_signatureBadge = new QPushButton(m_infoGroup);
    m_lockBadge = new QPushButton(m_infoGroup);
    for (QPushButton *badge : { m_signatureBadge, m_lockBadge }) {
        badge->setObjectName(QStringLiteral("StatusBadge"));
        badge->setFlat(true);
        badge->setCursor(Qt::PointingHandCursor);
        badge->setIconSize(QSize(18, 18));
        badge->setFixedHeight(32);
        row->addWidget(badge);
    }
    connect(m_signatureBadge, &QPushButton::clicked, this, &StatusBar::signaturesRequested);
    connect(m_lockBadge, &QPushButton::clicked, this, &StatusBar::protectionRequested);
    row->addStretch(1);
    layout->addWidget(m_infoGroup);
}

void StatusBar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(0);

    buildInfoGroup(layout);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("StatusHint"));
    m_hintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_hintLabel, 1);

    m_prevBtn = new IconButton(this);
    m_prevBtn->setIconName(QStringLiteral("chevron-left"), Theme::IconMuted);
    m_prevBtn->setToolTip(tr("Previous Page"));
    connect(m_prevBtn, &QPushButton::clicked, this, &StatusBar::previousPageRequested);
    layout->addWidget(m_prevBtn);

    layout->addSpacing(4);

    m_pageInput = new QLineEdit(QStringLiteral("1"), this);
    m_pageInput->setObjectName(QStringLiteral("PageInput"));
    m_pageInput->setFixedWidth(36);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setToolTip(tr("Go to page"));
    m_validator = new QIntValidator(1, 1, m_pageInput);
    m_pageInput->setValidator(m_validator);
    connect(m_pageInput, &QLineEdit::editingFinished, this, &StatusBar::commitPageInput);

    connect(m_pageInput, &QLineEdit::returnPressed, m_pageInput, &QLineEdit::clearFocus);
    layout->addWidget(m_pageInput);

    layout->addSpacing(6);

    m_totalLabel = new QLabel(QStringLiteral("/ 1"), this);
    m_totalLabel->setObjectName(QStringLiteral("PageTotalLabel"));
    layout->addWidget(m_totalLabel);

    layout->addSpacing(4);

    m_nextBtn = new IconButton(this);
    m_nextBtn->setIconName(QStringLiteral("chevron-right"), Theme::IconMuted);
    m_nextBtn->setToolTip(tr("Next Page"));
    connect(m_nextBtn, &QPushButton::clicked, this, &StatusBar::nextPageRequested);
    layout->addWidget(m_nextBtn);

    layout->addStretch(1);

    auto *right = new QWidget(this);
    right->setFixedWidth(kSideWidth);
    auto *rightRow = new QHBoxLayout(right);
    rightRow->setContentsMargins(0, 0, 0, 0);
    rightRow->addStretch(1);
    m_panelBtn = new IconButton(right);
    m_panelBtn->setIconName(QStringLiteral("panel-right"), Theme::IconMuted);
    m_panelBtn->setToolTip(tr("Toggle Panel"));
    connect(m_panelBtn, &QPushButton::clicked, this, &StatusBar::panelToggleRequested);
    rightRow->addWidget(m_panelBtn);
    layout->addWidget(right);
}
