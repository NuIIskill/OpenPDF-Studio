#include "ui/panels/LeftSidebar.hpp"

#include "app/AppSettings.hpp"
#include "ui/panels/ToolCustomizePopup.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/widgets/IconButton.hpp"

#include <QVBoxLayout>

namespace {

// A function-local static and not a namespace variable: registerTool() is
// called from static initializers, whose order is not fixed. This way the
// built-in tools are there before the first one is added.
QVector<ToolDef> &catalog()
{
    static QVector<ToolDef> tools = {
        { QStringLiteral("select"),   QStringLiteral("mouse-pointer-2"), QStringLiteral(QT_TR_NOOP("Select"))     },
        { QStringLiteral("pan"),      QStringLiteral("hand"),            QStringLiteral(QT_TR_NOOP("Pan"))        },
        { QStringLiteral("text"),     QStringLiteral("type"),            QStringLiteral(QT_TR_NOOP("Text")),       true },
        { QStringLiteral("comment"),  QStringLiteral("message-square"),  QStringLiteral(QT_TR_NOOP("Notes")),      true },
        { QStringLiteral("draw"),     QStringLiteral("pencil"),          QStringLiteral(QT_TR_NOOP("Draw")),       true },
        { QStringLiteral("image"),    QStringLiteral("image"),           QStringLiteral(QT_TR_NOOP("Image")),      true },
        { QStringLiteral("bookmark"), QStringLiteral("bookmark"),        QStringLiteral(QT_TR_NOOP("Bookmark"))   },
        { QStringLiteral("attach"),   QStringLiteral("paperclip"),       QStringLiteral(QT_TR_NOOP("Attachment")), true },
    };
    return tools;
}

} // namespace

const QVector<ToolDef> &LeftSidebar::toolCatalog()
{
    return catalog();
}

void LeftSidebar::registerTool(const ToolDef &tool)
{
    if (tool.id.isEmpty()) return;
    for (const ToolDef &t : catalog())
        if (t.id == tool.id) return;
    catalog().append(tool);
}

LeftSidebar::LeftSidebar(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("LeftSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(60);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    buildLayout();
    applyToolLayout();
}

void LeftSidebar::buildLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 12, 8, 12);
    layout->setSpacing(2);

    // One button per catalogue entry, built once. Hiding and reordering only
    // ever moves these around - no button is created or destroyed later, so
    // the connections below stay valid for the life of the sidebar.
    m_toolLayout = new QVBoxLayout;
    m_toolLayout->setContentsMargins(0, 0, 0, 0);
    m_toolLayout->setSpacing(2);
    layout->addLayout(m_toolLayout);

    for (const ToolDef &t : toolCatalog()) {
        auto *btn = new IconButton(this);
        btn->setIconName(t.icon, Theme::IconMuted);
        btn->setToolTip(tr(t.tip.toUtf8().constData()));
        btn->setToggle(true);
        btn->setCheckable(true);
        const QString id = t.id;
        connect(btn, &QPushButton::clicked, this, [this, id, btn]() {
            for (auto *b : std::as_const(m_toolButtons)) b->setChecked(b == btn);
            Q_EMIT toolSelected(id);
        });
        m_toolButtons.insert(t.id, btn);
    }

    layout->addStretch(1);

    m_customizeBtn = new IconButton(this);
    m_customizeBtn->setIconName(QStringLiteral("plus"), Theme::IconMuted);
    m_customizeBtn->setToolTip(tr("Customize Tools"));
    connect(m_customizeBtn, &QPushButton::clicked, this, &LeftSidebar::openCustomizePopup);
    layout->addWidget(m_customizeBtn);

    m_settingsBtn = new IconButton(this);
    m_settingsBtn->setIconName(QStringLiteral("settings"), Theme::IconMuted);
    m_settingsBtn->setToolTip(tr("Settings"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &LeftSidebar::settingsRequested);
    layout->addWidget(m_settingsBtn);
}

QStringList LeftSidebar::effectiveOrder() const
{
    const QStringList saved = m_settings ? m_settings->toolOrder() : QStringList{};

    QStringList order;
    for (const QString &id : saved)
        if (m_toolButtons.contains(id) && !order.contains(id))
            order << id;

    // Tools the saved order predates go back to where they ship - at the end
    // of the list, visible, rather than lost because the file is older.
    for (const ToolDef &t : toolCatalog())
        if (!order.contains(t.id))
            order << t.id;

    return order;
}

void LeftSidebar::applyToolLayout()
{
    const QStringList order  = effectiveOrder();
    const QStringList hidden = m_settings ? m_settings->hiddenTools() : QStringList{};

    while (QLayoutItem *item = m_toolLayout->takeAt(0))
        delete item;          // the item, not the button - that one we reuse

    m_visibleIds.clear();
    for (const QString &id : order) {
        IconButton *btn = m_toolButtons.value(id, nullptr);
        if (!btn)
            continue;
        const bool visible = !hidden.contains(id);
        btn->setVisible(visible);
        if (visible) {
            m_toolLayout->addWidget(btn);
            m_visibleIds << id;
        }
    }

    // The active tool may have just been switched off - fall back to Select,
    // or to whatever is left, instead of leaving nothing selected. Queued so
    // the switch lands after this rebuild, and after the constructor has
    // returned on the very first call - MainWindow connects only then.
    bool anyChecked = false;
    for (const QString &id : std::as_const(m_visibleIds))
        anyChecked = anyChecked || m_toolButtons.value(id)->isChecked();

    if (!anyChecked && !m_visibleIds.isEmpty()) {
        const QString fallback = m_visibleIds.contains(QStringLiteral("select"))
                               ? QStringLiteral("select") : m_visibleIds.first();
        setActiveTool(fallback);
        QMetaObject::invokeMethod(this, [this, fallback]() {
            Q_EMIT toolSelected(fallback);
        }, Qt::QueuedConnection);
    }
}

void LeftSidebar::openCustomizePopup()
{
    if (m_popup) {           // the + toggles the card shut again
        m_popup->close();
        return;
    }

    const QStringList hidden = m_settings ? m_settings->hiddenTools() : QStringList{};
    auto *popup = new ToolCustomizePopup(effectiveOrder(), hidden, window());
    m_popup = popup;

    connect(popup, &ToolCustomizePopup::configChanged, this,
            [this](const QStringList &order, const QStringList &hiddenIds) {
        if (m_settings) {
            m_settings->setToolOrder(order);
            m_settings->setHiddenTools(hiddenIds);
        }
        applyToolLayout();
    });
    connect(popup, &ToolCustomizePopup::resetRequested, this, [this]() {
        if (m_settings)
            m_settings->resetToolLayout();
        applyToolLayout();
    });

    popup->popupAt(m_customizeBtn);
}

void LeftSidebar::refreshTheme()
{
    for (auto *btn : std::as_const(m_toolButtons))
        btn->setIconName(btn->iconName(), Theme::IconMuted);
    if (m_customizeBtn)
        m_customizeBtn->setIconName(m_customizeBtn->iconName(), Theme::IconMuted);
    if (m_settingsBtn)
        m_settingsBtn->setIconName(m_settingsBtn->iconName(), Theme::IconMuted);
}

void LeftSidebar::retranslateUi()
{
    for (const ToolDef &t : toolCatalog())
        if (IconButton *btn = m_toolButtons.value(t.id, nullptr))
            btn->setToolTip(tr(t.tip.toUtf8().constData()));
    if (m_customizeBtn)
        m_customizeBtn->setToolTip(tr("Customize Tools"));
    if (m_settingsBtn)
        m_settingsBtn->setToolTip(tr("Settings"));
}

void LeftSidebar::setActiveTool(const QString &tool)
{
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
        it.value()->setChecked(it.key() == tool);
}

void LeftSidebar::setEditMode(bool)
{
    // Tools are always enabled regardless of mode.
}
