#include "RightSidebar.hpp"
#include "ui/theme/Theme.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>

RightSidebar::RightSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RightSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(110);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    buildLayout();
}

void RightSidebar::refreshTheme()
{
    for (auto &m : m_modes) {
        const QColor color = m.selected ? Theme::IconChecked : Theme::IconNormal;
        const QPixmap px = Theme::renderSvg(m.iconName, color, 22);
        if (!px.isNull())
            m.iconLabel->setPixmap(px);
    }
}

void RightSidebar::buildLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 16, 8, 16);
    layout->setSpacing(4);

    struct ModeItem { const char *icon; const char *label; const char *id; bool sel; };
    const ModeItem modes[] = {
        { "pencil", "Bearbeiten", "edit",     true  },
        { "upload", "Export",     "export",   false },
        { "layers", "Ordnen",     "organize", false },
    };

    for (const auto &m : modes) {
        layout->addWidget(makeModeButton(
            QLatin1String(m.icon),
            tr(m.label),
            QLatin1String(m.id),
            m.sel));
    }

    layout->addStretch(1);
}

QWidget *RightSidebar::makeModeButton(const QString &iconName, const QString &label,
                                       const QString &id, bool selected)
{
    auto *btn = new QPushButton(this);
    btn->setObjectName(selected ? QStringLiteral("ModeButtonSelected")
                                : QStringLiteral("ModeButton"));
    btn->setFixedHeight(80);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);

    auto *inner = new QVBoxLayout(btn);
    inner->setContentsMargins(0, 10, 0, 10);
    inner->setSpacing(6);
    inner->setAlignment(Qt::AlignCenter);

    // Render icon as QLabel with pixmap
    auto *iconLabel = new QLabel(btn);
    iconLabel->setAlignment(Qt::AlignCenter);
    const QColor iconColor = selected ? Theme::IconChecked : Theme::IconNormal;
    const QPixmap px = Theme::renderSvg(iconName, iconColor, 22);
    if (!px.isNull())
        iconLabel->setPixmap(px);
    inner->addWidget(iconLabel);
    m_modes.append({ iconName, iconLabel, selected });

    auto *textLabel = new QLabel(label, btn);
    textLabel->setObjectName(QStringLiteral("ModeLabel"));
    textLabel->setAlignment(Qt::AlignCenter);
    if (selected)
        textLabel->setObjectName(QStringLiteral("ModeLabelSelected"));
    inner->addWidget(textLabel);

    connect(btn, &QPushButton::clicked, this, [this, id]() {
        Q_EMIT modeSelected(id);
    });

    return btn;
}
