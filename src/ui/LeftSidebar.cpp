#include "LeftSidebar.hpp"
#include "ui/widgets/IconButton.hpp"
#include "ui/theme/Theme.hpp"

#include <QVBoxLayout>

LeftSidebar::LeftSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LeftSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(60);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    buildLayout();
}

void LeftSidebar::buildLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 12, 8, 12);
    layout->setSpacing(2);

    struct ToolDef { const char *icon; const char *tip; const char *id; };
    const ToolDef tools[] = {
        { "mouse-pointer-2", QT_TR_NOOP("Select"),     "select"   },
        { "hand",            QT_TR_NOOP("Pan"),        "pan"      },
        { "type",            QT_TR_NOOP("Text"),       "text"     },
        { "message-square",  QT_TR_NOOP("Comment"),   "comment"  },
        { "pencil",          QT_TR_NOOP("Draw"),       "draw"     },
        { "image",           QT_TR_NOOP("Image"),      "image"    },
        { "table",           QT_TR_NOOP("Table"),      "table"    },
        { "file",            QT_TR_NOOP("Page"),       "page"     },
        { "bookmark",        QT_TR_NOOP("Bookmark"),   "bookmark" },
        { "paperclip",       QT_TR_NOOP("Attachment"), "attach"   },
    };

    for (const auto &t : tools) {
        auto *btn = new IconButton(this);
        btn->setIconName(QLatin1String(t.icon), Theme::IconMuted);
        btn->setToolTip(tr(t.tip));
        btn->setToggle(true);
        btn->setCheckable(true);
        m_toolTips.append(QLatin1String(t.tip));
        const QString id = QLatin1String(t.id);
        connect(btn, &QPushButton::clicked, this, [this, id, btn]() {
            for (auto *b : m_toolButtons) b->setChecked(b == btn);
            Q_EMIT toolSelected(id);
        });
        layout->addWidget(btn);
        m_toolButtons.append(btn);
    }

    m_toolButtons.first()->setChecked(true);
    layout->addStretch(1);

    m_settingsBtn = new IconButton(this);
    m_settingsBtn->setIconName(QStringLiteral("settings"), Theme::IconMuted);
    m_settingsBtn->setToolTip(tr("Settings"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &LeftSidebar::settingsRequested);
    layout->addWidget(m_settingsBtn);
}

void LeftSidebar::refreshTheme()
{
    for (auto *btn : m_toolButtons)
        btn->setIconName(btn->iconName(), Theme::IconMuted);
    if (m_settingsBtn)
        m_settingsBtn->setIconName(m_settingsBtn->iconName(), Theme::IconMuted);
}

void LeftSidebar::retranslateUi()
{
    for (int i = 0; i < m_toolButtons.size() && i < m_toolTips.size(); ++i)
        m_toolButtons[i]->setToolTip(tr(m_toolTips[i].toUtf8().constData()));
    if (m_settingsBtn)
        m_settingsBtn->setToolTip(tr("Settings"));
}

void LeftSidebar::setActiveTool(const QString &tool)
{
    const QStringList ids = { "select","pan","text","comment","draw",
                               "image","table","page","bookmark","attach" };
    for (int i = 0; i < ids.size() && i < m_toolButtons.size(); ++i)
        m_toolButtons[i]->setChecked(ids[i] == tool);
}
