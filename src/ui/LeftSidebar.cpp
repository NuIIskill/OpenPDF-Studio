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
        { "mouse-pointer-2", "Auswählen",   "select"   },
        { "hand",            "Verschieben", "pan"      },
        { "type",            "Text",        "text"     },
        { "message-square",  "Kommentar",   "comment"  },
        { "pencil",          "Zeichnen",    "draw"     },
        { "image",           "Bild",        "image"    },
        { "table",           "Tabelle",     "table"    },
        { "file",            "Seite",       "page"     },
        { "bookmark",        "Lesezeichen", "bookmark" },
        { "paperclip",       "Anhang",      "attach"   },
    };

    for (const auto &t : tools) {
        auto *btn = new IconButton(this);
        btn->setIconName(QLatin1String(t.icon), Theme::IconMuted);
        btn->setToolTip(tr(t.tip));
        btn->setToggle(true);
        btn->setCheckable(true);
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

    auto *settingsBtn = new IconButton(this);
    settingsBtn->setIconName(QStringLiteral("settings"), Theme::IconMuted);
    settingsBtn->setToolTip(tr("Einstellungen"));
    connect(settingsBtn, &QPushButton::clicked, this, &LeftSidebar::settingsRequested);
    layout->addWidget(settingsBtn);
}

void LeftSidebar::setActiveTool(const QString &tool)
{
    const QStringList ids = { "select","pan","text","comment","draw",
                               "image","table","page","bookmark","attach" };
    for (int i = 0; i < ids.size() && i < m_toolButtons.size(); ++i)
        m_toolButtons[i]->setChecked(ids[i] == tool);
}
