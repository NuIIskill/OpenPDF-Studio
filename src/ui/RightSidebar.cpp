#include "RightSidebar.hpp"
#include "ui/theme/Theme.hpp"

#include <QLabel>
#include <QStyle>
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

void RightSidebar::retranslateUi()
{
    for (auto &m : m_modes)
        m.textLabel->setText(tr(m.tipKey.toUtf8().constData()));
}

void RightSidebar::buildLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 16, 8, 16);
    layout->setSpacing(4);

    struct ModeItem { const char *icon; const char *label; const char *id; bool sel; };
    const ModeItem modes[] = {
        { "pencil",  QT_TR_NOOP("Edit"),     "edit",     false },
        { "upload",  QT_TR_NOOP("Export"),   "export",   false },
        { "layers",  QT_TR_NOOP("Organize"), "organize", false },
    };

    for (const auto &m : modes) {
        layout->addWidget(makeModeButton(
            QLatin1String(m.icon),
            tr(m.label),
            QLatin1String(m.id),
            m.sel));
        m_modes.back().tipKey = QLatin1String(m.label);
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

    auto *iconLabel = new QLabel(btn);
    iconLabel->setAlignment(Qt::AlignCenter);
    const QColor iconColor = selected ? Theme::IconChecked : Theme::IconNormal;
    const QPixmap px = Theme::renderSvg(iconName, iconColor, 22);
    if (!px.isNull())
        iconLabel->setPixmap(px);
    inner->addWidget(iconLabel);

    auto *textLabel = new QLabel(label, btn);
    textLabel->setObjectName(selected ? QStringLiteral("ModeLabelSelected")
                                      : QStringLiteral("ModeLabel"));
    textLabel->setAlignment(Qt::AlignCenter);
    inner->addWidget(textLabel);

    m_modes.append({ iconName, iconLabel, textLabel, btn, label, selected });

    const bool isEdit = (id == QLatin1String("edit"));
    connect(btn, &QPushButton::clicked, this, [this, id, isEdit]() {
        if (isEdit) {
            // Toggle: flip current selection state
            const bool nowOn = !m_modes[0].selected;
            applyModeStyle(0, nowOn);
        }
        Q_EMIT modeSelected(id);
    });

    return btn;
}

void RightSidebar::applyModeStyle(int i, bool selected)
{
    auto &m = m_modes[i];
    m.selected = selected;

    m.btn->setObjectName(selected ? QStringLiteral("ModeButtonSelected")
                                  : QStringLiteral("ModeButton"));
    m.btn->style()->unpolish(m.btn);
    m.btn->style()->polish(m.btn);

    m.textLabel->setObjectName(selected ? QStringLiteral("ModeLabelSelected")
                                        : QStringLiteral("ModeLabel"));
    m.textLabel->style()->unpolish(m.textLabel);
    m.textLabel->style()->polish(m.textLabel);

    const QColor iconColor = selected ? Theme::IconChecked : Theme::IconNormal;
    const QPixmap px = Theme::renderSvg(m.iconName, iconColor, 22);
    if (!px.isNull())
        m.iconLabel->setPixmap(px);
}

void RightSidebar::setMode(const QString &id)
{
    // Only Edit (index 0) has a persistent toggle state.
    // "edit" = activate, anything else (e.g. "") = deactivate.
    applyModeStyle(0, id == QLatin1String("edit"));
}
