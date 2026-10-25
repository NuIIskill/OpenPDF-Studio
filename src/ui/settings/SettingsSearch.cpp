#include "ui/settings/SettingsSearch.hpp"
#include "ui/theme/Theme.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>

namespace {

constexpr int kDelayMs = 200;
constexpr int kFlashMs = 1600;

QStringList ownTexts(QWidget *widget)
{
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    if (auto *label = qobject_cast<QLabel *>(widget))
        return { label->text().remove(tags) };
    if (auto *button = qobject_cast<QAbstractButton *>(widget))
        return { button->text().remove(QLatin1Char('&')) };
    QStringList items;
    if (auto *combo = qobject_cast<QComboBox *>(widget))
        for (int i = 0; i < combo->count(); ++i) items << combo->itemText(i);
    return items;
}

bool isCard(const QWidget *widget)
{
    const QString name = widget->objectName();
    return name == QLatin1String("SettingsCard") || name == QLatin1String("ZoomCard")
        || widget->inherits("OptionCard") || widget->inherits("LangRow")
        || widget->inherits("ShortcutRow");
}

QWidget *cardOf(QWidget *widget, QWidget *page)
{
    for (QWidget *w = widget; w && w != page; w = w->parentWidget())
        if (isCard(w)) return w;
    return nullptr;
}

}

SettingsSearch::SettingsSearch(QStackedWidget *pages, QWidget *dialog)
    : QObject(dialog)
    , m_pages(pages)
{
    m_field = new QLineEdit(dialog);
    m_field->setObjectName(QStringLiteral("SettingsSearchField"));
    m_field->setClearButtonEnabled(true);
    m_field->setFixedHeight(34);
    m_field->installEventFilter(this);
    m_icon = m_field->addAction(QIcon(), QLineEdit::LeadingPosition);

    m_noResults = new QLabel(dialog);
    m_noResults->setObjectName(QStringLiteral("SettingsRowDesc"));
    m_noResults->setContentsMargins(10, 4, 10, 0);
    m_noResults->hide();

    m_delay.setSingleShot(true);
    m_delay.setInterval(kDelayMs);
    connect(&m_delay, &QTimer::timeout, this, &SettingsSearch::run);
    connect(m_field, &QLineEdit::textChanged, &m_delay, qOverload<>(&QTimer::start));

    auto *find = new QShortcut(QKeySequence::Find, dialog);
    connect(find, &QShortcut::activated, m_field, [this]() {
        m_field->setFocus(Qt::ShortcutFocusReason);
        m_field->selectAll();
    });

    retranslate();
    applyTheme();
}

void SettingsSearch::retranslate()
{
    m_field->setPlaceholderText(tr("Search…"));
    m_noResults->setText(tr("No results"));
}

void SettingsSearch::applyTheme()
{
    m_icon->setIcon(QIcon(Theme::renderSvg(QStringLiteral("search"), Theme::IconMuted, 16)));
}

QString SettingsSearch::query() const
{
    return m_field->text().simplified();
}

bool SettingsSearch::matches(QWidget *widget) const
{
    const QStringList words = query().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &text : ownTexts(widget)) {
        bool all = !words.isEmpty();
        for (const QString &word : words)
            all = all && text.contains(word, Qt::CaseInsensitive);
        if (all) return true;
    }
    return false;
}

QWidget *SettingsSearch::firstMatch(QWidget *page) const
{
    QWidget *any = nullptr;
    for (QWidget *w : page->findChildren<QWidget *>()) {
        if (w->isHidden() || !matches(w)) continue;
        if (QWidget *card = cardOf(w, page)) return card;
        if (!any) any = w;
    }
    return any;
}

void SettingsSearch::run()
{
    QList<int> hits;
    for (int i = 0; i < m_pages->count(); ++i)
        if (query().isEmpty() || firstMatch(m_pages->widget(i)))
            hits << i;
    m_noResults->setVisible(hits.isEmpty());
    Q_EMIT filtered(hits);
}

void SettingsSearch::reveal(int page)
{
    delete m_flash.data();
    for (int i = 0; i < m_pages->count(); ++i) {
        QWidget *w = m_pages->widget(i);
        auto *rowFilter = w->findChild<QLineEdit *>(QStringLiteral("ShortcutSearch"));
        if (!rowFilter) continue;
        bool rowHit = false;
        for (QWidget *row : w->findChildren<QWidget *>())
            if (row->inherits("ShortcutRow") && firstMatch(row)) { rowHit = true; break; }
        rowFilter->setText(rowHit ? query() : QString());
    }

    if (query().isEmpty() || page < 0 || page >= m_pages->count()) return;
    QWidget *target = firstMatch(m_pages->widget(page));
    if (!target || !isCard(target)) return;
    for (QWidget *w = target->parentWidget(); w; w = w->parentWidget())
        if (auto *scroll = qobject_cast<QScrollArea *>(w)) {
            scroll->ensureWidgetVisible(target, 0, 24);
            break;
        }
    flash(target);
}

void SettingsSearch::flash(QWidget *target)
{
    delete m_flash.data();
    auto *ring = new QFrame(target);
    ring->setAttribute(Qt::WA_TransparentForMouseEvents);
    ring->setGeometry(target->rect());
    ring->setStyleSheet(QStringLiteral(
        "QFrame { border:2px solid %1; border-radius:8px; background:transparent; }")
        .arg(Theme::DarkMode ? QStringLiteral("#60A5FA") : QStringLiteral("#3B82F6")));
    ring->show();
    ring->raise();
    m_flash = ring;
    QTimer::singleShot(kFlashMs, ring, &QObject::deleteLater);
}

bool SettingsSearch::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_field && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
            && !m_field->text().isEmpty()) {
        m_field->clear();
        return true;
    }
    return QObject::eventFilter(watched, event);
}
