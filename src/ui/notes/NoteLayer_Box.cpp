#include "ui/notes/NoteLayer.hpp"

#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <utility>

void NoteLayer::hidePopup()
{
    // The box may be the one whose mouse event is being handled, so it goes later.
    if (m_popup) {
        m_popup->hide();
        m_popup->deleteLater();
    }
    m_popup = nullptr;
    m_popupId.clear();
    m_popupGrab = QPoint(-1, -1);
    m_popupDragging = false;
}

void NoteLayer::watchCanvas()
{
    if (QWidget *canvas = m_canvas->canvasWidget()) canvas->installEventFilter(this);
}

void NoteLayer::carry()
{
    // Placed from the pointer, not from where the drag began, so a scroll
    // with the wheel keeps the note under the pointer.
    const int index = indexOf(m_popupId);
    QWidget *marker = index >= 0 ? m_entries.at(index).marker : nullptr;
    if (!marker || !m_popup) return;
    QWidget *canvas = m_canvas->canvasWidget();
    marker->move(canvas->mapFromGlobal(m_lastGlobal + m_markerOffset));
    m_popup->move(canvas->mapFromGlobal(m_lastGlobal + m_popupOffset));
    marker->raise();
    m_popup->raise();
    previewAt(marker->mapToGlobal(QPoint()));
}

bool NoteLayer::eventFilter(QObject *watched, QEvent *event)
{
    // The wheel over a note's box or marker scrolls the page, also while it is carried.
    if (event->type() == QEvent::Wheel && watched != m_canvas->canvasWidget()) {
        QWidget *viewport = m_canvas->canvasWidget()->parentWidget();
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (!viewport) return QObject::eventFilter(watched, event);
        QWheelEvent forwarded(viewport->mapFromGlobal(wheel->globalPosition()),
                              wheel->globalPosition(), wheel->pixelDelta(), wheel->angleDelta(),
                              wheel->buttons(), wheel->modifiers(), wheel->phase(),
                              wheel->inverted(), wheel->source());
        QApplication::sendEvent(viewport, &forwarded);
        return true;
    }
    // The page moving under a drag, as when the wheel scrolls it.
    if (watched == m_canvas->canvasWidget()) {
        if (event->type() == QEvent::Move) {
            if (m_popupDragging) carry();
            else if (m_previewWatch) previewAt(m_previewGlobal);
        }
        return QObject::eventFilter(watched, event);
    }
    // Pulling the box beside a marker carries the note along, like its marker.
    if (watched != m_popup) return QObject::eventFilter(watched, event);
    const int index = indexOf(m_popupId);
    QWidget *marker = index >= 0 ? m_entries.at(index).marker : nullptr;
    if (!marker) return QObject::eventFilter(watched, event);
    auto *mouse = static_cast<QMouseEvent *>(event);
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        if (mouse->button() != Qt::LeftButton || !m_toolActive) break;
        m_popupGrab     = mouse->globalPosition().toPoint();
        m_lastGlobal    = m_popupGrab;
        m_markerOffset  = marker->mapToGlobal(QPoint()) - m_popupGrab;
        m_popupOffset   = m_popup->mapToGlobal(QPoint()) - m_popupGrab;
        m_popupDragging = false;
        watchCanvas();
        return true;
    case QEvent::MouseMove: {
        if (m_popupGrab.x() < 0) break;
        m_lastGlobal = mouse->globalPosition().toPoint();
        if (!m_popupDragging
                && (m_lastGlobal - m_popupGrab).manhattanLength() < QApplication::startDragDistance())
            return true;
        m_popupDragging = true;
        m_popup->setCursor(Qt::ClosedHandCursor);
        carry();
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (m_popupGrab.x() < 0) break;
        m_popupGrab = QPoint(-1, -1);
        if (!std::exchange(m_popupDragging, false)) return true;
        m_popup->setCursor(Qt::OpenHandCursor);
        if (!dropAt(m_popupId, {}, {}, marker->mapToGlobal(QPoint()))) relayout();
        return true;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void NoteLayer::showPopup(const QString &id)
{
    hidePopup();
    const int index = indexOf(id);
    if (index < 0 || !m_entries.at(index).marker) return;
    const NoteData &note = m_entries.at(index).data;
    const qreal dpr = m_canvas->canvasWidget()->devicePixelRatioF();

    m_popup = new QFrame(m_canvas->canvasWidget());
    m_popup->setObjectName(QStringLiteral("NotePopup"));
    m_popup->setCursor(Qt::OpenHandCursor);
    m_popup->installEventFilter(this);
    m_popupId = id;
    m_popup->setFixedWidth(264);
    auto *shadow = new QGraphicsDropShadowEffect(m_popup);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, Theme::DarkMode ? 110 : 45));
    m_popup->setGraphicsEffect(shadow);

    auto *layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(10, 10, 10, 12);
    layout->setSpacing(6);
    auto *top = new QHBoxLayout;
    top->setSpacing(6);
    auto *grip = new QLabel(m_popup);
    grip->setPixmap(Theme::renderSvg(QStringLiteral("grip-vertical"), Theme::IconMuted, 14, dpr));
    grip->setToolTip(QCoreApplication::translate("NotesPanel", "Drag onto the page to move the note"));
    grip->setFixedWidth(14);
    top->addWidget(grip);
    auto *dot = new QLabel(QStringLiteral("●"), m_popup);
    dot->setObjectName(QStringLiteral("NoteDot"));
    dot->setFixedWidth(12);
    dot->setStyleSheet(QStringLiteral("color: %1;").arg(note.color.name()));
    top->addWidget(dot);
    auto *title = new QLabel(note.title, m_popup);
    title->setObjectName(QStringLiteral("NotePopupTitle"));
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    top->addWidget(title, 1);
    auto *close = new QPushButton(m_popup);
    close->setObjectName(QStringLiteral("NotesClose"));
    close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
    close->setFixedSize(24, 24);
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &NoteLayer::deactivate);
    top->addWidget(close);
    layout->addLayout(top);

    auto *body = new QLabel(note.text.isEmpty()
        ? QCoreApplication::translate("NotesPanel", "Write a note…") : note.text, m_popup);
    body->setObjectName(note.text.isEmpty() ? QStringLiteral("NotePopupHint")
                                            : QStringLiteral("NotePopupBody"));
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(true);
    // Body and footer start under the title, after the grip and the dot.
    constexpr int kUnderTitle = 14 + 6 + 12 + 6;
    body->setContentsMargins(kUnderTitle, 0, 0, 0);
    layout->addWidget(body);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(kUnderTitle, 2, 0, 0);
    footer->setSpacing(4);
    auto *where = new QLabel(m_popup);
    where->setObjectName(QStringLiteral("NoteCardMeta"));
    where->setTextFormat(Qt::PlainText);
    if (!note.textRects.isEmpty()) {
        auto *link = new QLabel(m_popup);
        link->setPixmap(Theme::renderSvg(QStringLiteral("link"), Theme::IconMuted, 13, dpr));
        footer->addWidget(link);
        where->ensurePolished();
        const QString quote = where->fontMetrics().elidedText(note.quote.simplified(),
                                                               Qt::ElideRight, 170);
        where->setText(QCoreApplication::translate("NotesPanel", "“%1” · p. %2")
                           .arg(quote).arg(note.page + 1));
        where->setToolTip(note.quote);
    } else {
        where->setText(tr("Page %1").arg(note.page + 1));
    }
    footer->addWidget(where);
    footer->addStretch();
    layout->addLayout(footer);
    m_popup->adjustSize();

    const QRect canvasRect = m_canvas->canvasWidget()->rect();
    const QWidget *marker = m_entries.at(index).marker;
    QPoint pos = marker->geometry().topRight() + QPoint(8, -6);
    if (pos.x() + m_popup->width() > canvasRect.right())
        pos.setX(marker->x() - m_popup->width() - 8);
    pos.setY(qBound(canvasRect.top(), pos.y(),
                    qMax(canvasRect.top(), canvasRect.bottom() - m_popup->height())));
    m_popup->move(pos);
    m_popup->show();
    m_popup->raise();
}
