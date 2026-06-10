#include "TopToolbar.hpp"

#include "ui/widgets/IconButton.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>

TopToolbar::TopToolbar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TopToolbar"));
    setFixedHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buildLayout();
}

void TopToolbar::setFileName(const QString &name)
{
    m_fileNameLabel->setText(name.isEmpty() ? tr("Kein Dokument") : name);
}

void TopToolbar::setZoom(int percent)
{
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(percent));
}

void TopToolbar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(4);

    // ── App name ──────────────────────────────────────────────────────────
    auto *appName = new QLabel(QStringLiteral("OpenPDF Studio"), this);
    appName->setObjectName(QStringLiteral("AppNameLabel"));
    layout->addWidget(appName);

    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // ── File name ─────────────────────────────────────────────────────────
    m_fileNameLabel = new QLabel(tr("Kein Dokument"), this);
    m_fileNameLabel->setObjectName(QStringLiteral("FileNameLabel"));
    layout->addWidget(m_fileNameLabel);

    layout->addStretch(1);

    // ── Zoom controls ─────────────────────────────────────────────────────
    m_zoomOutBtn = new IconButton(QStringLiteral("−"), this);
    m_zoomOutBtn->setToolTip(tr("Verkleinern"));
    connect(m_zoomOutBtn, &QPushButton::clicked,
            this, &TopToolbar::zoomOutRequested);

    m_zoomLabel = new QLabel(QStringLiteral("100 %"), this);
    m_zoomLabel->setObjectName(QStringLiteral("ZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(52);

    m_zoomInBtn = new IconButton(QStringLiteral("+"), this);
    m_zoomInBtn->setToolTip(tr("Vergrößern"));
    connect(m_zoomInBtn, &QPushButton::clicked,
            this, &TopToolbar::zoomInRequested);

    layout->addWidget(m_zoomOutBtn);
    layout->addWidget(m_zoomLabel);
    layout->addWidget(m_zoomInBtn);

    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // ── Tool buttons ──────────────────────────────────────────────────────
    struct ToolDef { const char *label; const char *tip; const char *id; };
    const ToolDef tools[] = {
        { "↖",  "Auswählen",   "cursor"   },
        { "T",  "Text",        "text"     },
        { "✎",  "Anmerkung",   "annotate" },
        { "⊞",  "Formular",    "forms"    },
    };

    IconButton *toolBtns[4];
    for (int i = 0; i < 4; ++i) {
        auto *btn = new IconButton(QString::fromUtf8(tools[i].label), this);
        btn->setToolTip(tr(tools[i].tip));
        btn->setToggle(true);
        btn->setCheckable(true);
        const QString id = QLatin1String(tools[i].id);
        connect(btn, &QPushButton::clicked, this, [this, id]() {
            Q_EMIT toolSelected(id);
        });
        layout->addWidget(btn);
        toolBtns[i] = btn;
    }
    m_cursorBtn   = toolBtns[0];
    m_textBtn     = toolBtns[1];
    m_annotateBtn = toolBtns[2];
    m_formsBtn    = toolBtns[3];

    // Default selection: cursor tool
    m_cursorBtn->setChecked(true);

    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // ── Settings button ───────────────────────────────────────────────────
    m_settingsBtn = new IconButton(QStringLiteral("⚙"), this);
    m_settingsBtn->setToolTip(tr("Einstellungen"));
    connect(m_settingsBtn, &QPushButton::clicked,
            this, &TopToolbar::settingsRequested);
    layout->addWidget(m_settingsBtn);
}

QWidget *TopToolbar::makeSeparator()
{
    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("ToolbarSeparator"));
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Plain);
    sep->setFixedWidth(1);
    sep->setFixedHeight(24);
    sep->setStyleSheet(QStringLiteral(
        "QFrame { background: #E2E8F0; border: none; }"));
    return sep;
}
