#include "ui/view/FindController.hpp"

#include "engine/document/DocumentSource.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/view/PageCanvas.hpp"
#include "ui/widgets/IconButton.hpp"

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

FindController::FindController(PageCanvas *canvas, QWidget *viewport,
                               DocumentSource *source, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_viewport(viewport)
    , m_source(source)
{
    m_panel = new QWidget(viewport);
    m_panel->setObjectName(QStringLiteral("DocumentFindOverlay"));
    m_panel->setAttribute(Qt::WA_StyledBackground, true);

    auto *outer = new QVBoxLayout(m_panel);
    outer->setContentsMargins(8, 8, 8, 8);

    m_bar = new QFrame(m_panel);
    m_bar->setObjectName(QStringLiteral("DocumentFindBar"));
    m_bar->setFixedHeight(48);
    auto *row = new QHBoxLayout(m_bar);
    row->setContentsMargins(12, 6, 7, 6);
    row->setSpacing(3);

    m_edit = new QLineEdit(m_bar);
    m_edit->setObjectName(QStringLiteral("DocumentFindInput"));
    m_edit->setFixedHeight(34);
    m_searchAction = m_edit->addAction(
        Theme::makeIcon(QStringLiteral("search"), Theme::IconMuted),
        QLineEdit::LeadingPosition);
    m_edit->installEventFilter(this);
    row->addWidget(m_edit, 1);

    m_counter = new QLabel(m_bar);
    m_counter->setObjectName(QStringLiteral("DocumentFindCounter"));
    m_counter->setAlignment(Qt::AlignCenter);
    m_counter->setMinimumWidth(42);
    row->addWidget(m_counter);

    const auto makeButton = [this, row](const QString &icon) {
        auto *button = new IconButton(m_bar);
        button->setObjectName(QStringLiteral("DocumentFindButton"));
        button->setIconName(icon, Theme::IconNormal);
        button->setFixedSize(32, 32);
        button->setIconSize(QSize(18, 18));
        button->setFocusPolicy(Qt::NoFocus);
        row->addWidget(button);
        return button;
    };
    m_previous = makeButton(QStringLiteral("chevron-up"));
    m_next     = makeButton(QStringLiteral("chevron-down"));
    m_close    = makeButton(QStringLiteral("x"));

    auto *shadow = new QGraphicsDropShadowEffect(m_bar);
    shadow->setBlurRadius(22);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 45));
    m_bar->setGraphicsEffect(shadow);
    outer->addWidget(m_bar);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(180);

    connect(m_edit, &QLineEdit::textChanged, this, &FindController::scheduleSearch);
    connect(m_timer, &QTimer::timeout, this, &FindController::performSearch);
    connect(m_previous, &QPushButton::clicked,
            this, [this]() { activateRelative(-1); });
    connect(m_next, &QPushButton::clicked,
            this, [this]() { activateRelative(1); });
    connect(m_close, &QPushButton::clicked, this, &FindController::close);

    retranslateUi();
    updateCounter();
    m_panel->hide();
}

void FindController::open()
{
    positionPanel();
    m_panel->show();
    m_panel->raise();
    m_edit->setFocus(Qt::ShortcutFocusReason);
    m_edit->selectAll();
    if (!m_edit->text().isEmpty() && m_matches.isEmpty())
        performSearch();
}

void FindController::close()
{
    m_timer->stop();
    m_panel->hide();
    m_matches.clear();
    m_active = -1;
    updateCounter();
    updateOverlays();
    m_viewport->setFocus(Qt::ShortcutFocusReason);
}

void FindController::documentChanged()
{
    m_timer->stop();
    m_matches.clear();
    m_active = -1;
    updateCounter();
    updateOverlays();
    if (m_panel->isVisible() && !m_edit->text().isEmpty())
        m_timer->start();
}

void FindController::scheduleSearch()
{
    m_timer->stop();
    m_matches.clear();
    m_active = -1;
    updateCounter();
    updateOverlays();
    if (!m_edit->text().trimmed().isEmpty()) m_timer->start();
}

void FindController::performSearch()
{
    m_timer->stop();
    m_matches.clear();
    m_active = -1;

#ifdef HAVE_PDF_RENDERING
    const QString needle = m_edit->text().trimmed();
    if (!needle.isEmpty() && m_source && m_source->backend()) {
        const QList<PdfBackend::TextMatch> found = m_source->backend()->findText(needle);
        m_matches.reserve(found.size());
        for (const PdfBackend::TextMatch &match : found)
            m_matches.append({ match.page, match.rects });
    }
#endif

    if (!m_matches.isEmpty()) activate(0);
    else {
        updateCounter();
        updateOverlays();
    }
}

void FindController::activateRelative(int delta)
{
    if (m_timer->isActive()) {
        performSearch();
        return;
    }
    if (m_matches.isEmpty()) return;
    activate((m_active + delta + m_matches.size()) % m_matches.size());
}

void FindController::activate(int index)
{
    if (index < 0 || index >= m_matches.size()) return;
    m_active = index;
    updateCounter();
    updateOverlays();

    QRectF bounds;
    for (const QRectF &rect : m_matches[index].rects)
        bounds = bounds.isNull() ? rect : bounds.united(rect);
    Q_EMIT matchActivated(m_matches[index].page, bounds);
}

void FindController::updateCounter()
{
    const int current = m_active >= 0 ? m_active + 1 : 0;
    m_counter->setText(QStringLiteral("%1/%2").arg(current).arg(m_matches.size()));
}

void FindController::updateOverlays()
{
    constexpr int kMaxOverlays = 800;
    int used = 0;
    const qreal scale = m_canvas->screenScale();

    const auto placeMatch = [this, scale, &used](int index) {
        if (index < 0 || index >= m_matches.size()) return;
        const Match &match = m_matches[index];
        const QLabel *label = m_canvas->pageLabel(match.page);
        if (!label) return;

        for (const QRectF &rect : match.rects) {
            if (used >= kMaxOverlays) break;
            QWidget *overlay = nullptr;
            if (used < m_overlays.size()) {
                overlay = m_overlays[used];
            } else {
                overlay = new QWidget(m_canvas->canvasWidget());
                overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                m_overlays.append(overlay);
            }
            const QRectF canvasRect(rect.topLeft() * scale + QPointF(label->pos()),
                                    rect.size() * scale);
            overlay->setGeometry(canvasRect.toAlignedRect().adjusted(-1, 0, 1, 0));
            overlay->setStyleSheet(index == m_active
                ? QStringLiteral("background-color: rgba(245, 158, 11, 190);")
                : QStringLiteral("background-color: rgba(250, 204, 21, 135);"));
            overlay->raise();
            overlay->show();
            ++used;
        }
    };

    placeMatch(m_active);
    for (int i = 0; i < m_matches.size() && used < kMaxOverlays; ++i)
        if (i != m_active) placeMatch(i);

    for (int i = used; i < m_overlays.size(); ++i)
        m_overlays[i]->hide();
}

void FindController::positionPanel()
{
    constexpr int kPreferredWidth = 470;
    const int width = qMax(280, qMin(kPreferredWidth, m_viewport->width() - 32));
    m_panel->setGeometry(qMax(0, (m_viewport->width() - width) / 2), 10, width, 64);
}

void FindController::relayout()
{
    positionPanel();
    updateOverlays();
    if (m_panel->isVisible()) m_panel->raise();
}

void FindController::retranslateUi()
{
    m_edit->setPlaceholderText(tr("Find in document"));
    m_previous->setToolTip(tr("Previous match (Shift+Enter)"));
    m_next->setToolTip(tr("Next match (Enter)"));
    m_close->setToolTip(tr("Close (Esc)"));
}

void FindController::refreshTheme()
{
    m_searchAction->setIcon(
        Theme::makeIcon(QStringLiteral("search"), Theme::IconMuted));
    m_previous->setIconName(QStringLiteral("chevron-up"), Theme::IconNormal);
    m_next->setIconName(QStringLiteral("chevron-down"), Theme::IconNormal);
    m_close->setIconName(QStringLiteral("x"), Theme::IconNormal);
}

bool FindController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            activateRelative(key->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
