#include "ui/panels/RightSidebar.hpp"
#include "ui/theme/Theme.hpp"

#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

RightSidebar::RightSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RightSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(8, 16, 8, 16);
    vl->setSpacing(4);

    struct ModeItem { const char *icon; const char *label; const char *id; };
    const ModeItem modes[] = {
        { "pencil",  QT_TR_NOOP("Edit"),     "edit"     },
        { "upload",  QT_TR_NOOP("Export"),   "export"   },
        { "layers",  QT_TR_NOOP("Organize"), "organize" },
        { "history", QT_TR_NOOP("History"),  "history"  },
    };

    for (const auto &m : modes) {
        auto *btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("ModeButton"));
        btn->setFixedSize(kWidth - 16, 80);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setAutoRaise(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIconSize(QSize(22, 22));
        btn->setText(tr(m.label));

        const QPixmap px = Theme::renderSvg(QLatin1String(m.icon), Theme::IconNormal, 22);
        if (!px.isNull()) btn->setIcon(QIcon(px));

        m_modes.append({ QLatin1String(m.icon), btn, QLatin1String(m.label), false });

        const QString id = QLatin1String(m.id);
        connect(btn, &QToolButton::clicked, this, [this, id]() {
            Q_EMIT modeSelected(id);
        });
        vl->addWidget(btn, 0, Qt::AlignHCenter);
    }
    vl->addStretch(1);
}

void RightSidebar::setMode(const QString &id)
{
    for (auto &md : m_modes) {
        const bool active = (id == md.tipKey.toLower());
        if (md.selected == active) continue;
        md.selected = active;

        md.btn->setObjectName(active ? QStringLiteral("ModeButtonSelected")
                                     : QStringLiteral("ModeButton"));
        md.btn->style()->unpolish(md.btn);
        md.btn->style()->polish(md.btn);

        const QColor col = active ? Theme::IconChecked : Theme::IconNormal;
        const QPixmap px = Theme::renderSvg(md.iconName, col, 22);
        if (!px.isNull()) md.btn->setIcon(QIcon(px));
    }
}

void RightSidebar::refreshTheme()
{
    for (auto &m : m_modes) {
        const QColor col = m.selected ? Theme::IconChecked : Theme::IconNormal;
        const QPixmap px = Theme::renderSvg(m.iconName, col, 22);
        if (!px.isNull()) m.btn->setIcon(QIcon(px));
    }
}

void RightSidebar::retranslateUi()
{
    for (auto &m : m_modes)
        m.btn->setText(tr(m.tipKey.toUtf8().constData()));
}
