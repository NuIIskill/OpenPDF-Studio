#include "RightSidebar.hpp"

#include "ToolPanel.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>

RightSidebar::RightSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RightSidebar"));
    setFixedWidth(260);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Header ─────────────────────────────────────────────────────────
    auto *header = new QWidget(this);
    header->setFixedHeight(48);
    header->setStyleSheet(QStringLiteral(
        "background: #FFFFFF; border-bottom: 1px solid #E2E8F0;"));

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 0, 14, 0);

    auto *titleLabel = new QLabel(tr("Eigenschaften"), header);
    titleLabel->setObjectName(QStringLiteral("PropertiesHeader"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);

    layout->addWidget(header);

    // ── ToolPanel ───────────────────────────────────────────────────────
    m_toolPanel = new ToolPanel(this);
    layout->addWidget(m_toolPanel, 1);
}
