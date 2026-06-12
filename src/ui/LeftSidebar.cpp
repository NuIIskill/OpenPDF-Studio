#include "LeftSidebar.hpp"

#include "ui/widgets/IconButton.hpp"

#include <QVBoxLayout>

LeftSidebar::LeftSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LeftSidebar"));
    setFixedWidth(56);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    buildLayout();
}

void LeftSidebar::buildLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 12, 8, 12);
    layout->setSpacing(4);

    // ── Tool buttons (top) ────────────────────────────────────────────────
    struct ToolDef { const char *label; const char *tip; const char *id; };
    const ToolDef tools[] = {
        { "↖", "Auswählen",  "cursor"   },
        { "T",  "Text",       "text"     },
        { "✎", "Anmerkung",  "annotate" },
        { "⊞", "Formular",   "forms"    },
    };

    for (const auto &t : tools) {
        auto *btn = new IconButton(QString::fromUtf8(t.label), this);
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

    // ── Push settings to bottom ───────────────────────────────────────────
    layout->addStretch(1);

    auto *settingsBtn = new IconButton(QStringLiteral("⚙"), this);
    settingsBtn->setToolTip(tr("Einstellungen"));
    connect(settingsBtn, &QPushButton::clicked, this, &LeftSidebar::settingsRequested);
    layout->addWidget(settingsBtn);
}

void LeftSidebar::setActiveTool(const QString &tool)
{
    const QStringList ids = { QStringLiteral("cursor"), QStringLiteral("text"),
                               QStringLiteral("annotate"), QStringLiteral("forms") };
    for (int i = 0; i < ids.size() && i < m_toolButtons.size(); ++i)
        m_toolButtons[i]->setChecked(ids[i] == tool);
}
