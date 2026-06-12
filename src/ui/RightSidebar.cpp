#include "RightSidebar.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

RightSidebar::RightSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RightSidebar"));
    setFixedWidth(110);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    buildLayout();
}

void RightSidebar::buildLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 16, 8, 16);
    layout->setSpacing(4);

    struct ModeItem { const char *icon; const char *label; const char *id; bool sel; };
    const ModeItem modes[] = {
        { "✏",  "Bearbeiten", "edit",    true  },
        { "↑",  "Export",     "export",  false },
        { "⊞",  "Ordnen",     "organize",false },
    };

    for (const auto &m : modes) {
        layout->addWidget(makeModeButton(
            QString::fromUtf8(m.icon),
            tr(m.label),
            QLatin1String(m.id),
            m.sel));
    }

    layout->addStretch(1);
}

QWidget *RightSidebar::makeModeButton(const QString &icon, const QString &label,
                                       const QString &id, bool selected)
{
    auto *btn = new QPushButton(this);
    btn->setObjectName(selected ? QStringLiteral("ModeButtonSelected")
                                : QStringLiteral("ModeButton"));
    btn->setFixedHeight(80);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);

    auto *inner = new QVBoxLayout(btn);
    inner->setContentsMargins(0, 12, 0, 12);
    inner->setSpacing(6);
    inner->setAlignment(Qt::AlignCenter);

    auto *iconLabel = new QLabel(icon, btn);
    iconLabel->setObjectName(QStringLiteral("ModeIcon"));
    iconLabel->setAlignment(Qt::AlignCenter);
    inner->addWidget(iconLabel);

    auto *textLabel = new QLabel(label, btn);
    textLabel->setObjectName(QStringLiteral("ModeLabel"));
    textLabel->setAlignment(Qt::AlignCenter);
    inner->addWidget(textLabel);

    connect(btn, &QPushButton::clicked, this, [this, id]() {
        Q_EMIT modeSelected(id);
    });

    return btn;
}
