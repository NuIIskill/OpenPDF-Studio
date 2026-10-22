#include "ui/SaveIndicator.hpp"

#include "ui/theme/Theme.hpp"

#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int HoldMs = 1600;
constexpr int FadeMs = 260;
constexpr int ProgressDropPx = 48;

}

SaveIndicator::SaveIndicator(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SaveIndicator"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    m_pill = new QFrame(this);
    m_pill->setObjectName(QStringLiteral("SaveIndicatorPill"));
    outer->addWidget(m_pill);

    auto *row = new QHBoxLayout(m_pill);
    row->setContentsMargins(14, 9, 16, 9);
    row->setSpacing(8);

    m_icon = new QLabel(m_pill);
    row->addWidget(m_icon);

    m_text = new QLabel(m_pill);
    m_text->setObjectName(QStringLiteral("SaveIndicatorText"));
    row->addWidget(m_text);

    m_opacity = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacity);

    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    m_fade->setDuration(FadeMs);
    m_fade->setStartValue(1.0);
    m_fade->setEndValue(0.0);
    connect(m_fade, &QPropertyAnimation::finished, this, &QWidget::hide);

    m_hold = new QTimer(this);
    m_hold->setSingleShot(true);
    m_hold->setInterval(HoldMs);
    connect(m_hold, &QTimer::timeout, this, &SaveIndicator::fadeOut);

    parent->installEventFilter(this);
    hide();
}

void SaveIndicator::flash(const QString &fileName)
{
    showDone(fileName.isEmpty() ? tr("Saved") : tr("Saved \"%1\"").arg(fileName));
}

void SaveIndicator::showDone(const QString &text)
{
    m_progress = false;
    m_text->setText(text);
    m_icon->setPixmap(Theme::renderSvg(QStringLiteral("check"), Theme::Success,
                                       18, devicePixelRatioF()));
    m_icon->show();

    m_fade->stop();
    m_opacity->setOpacity(1.0);
    adjustSize();
    reposition();
    show();
    raise();
    m_hold->start();
}

void SaveIndicator::showProgress(const QString &text)
{
    m_progress = true;
    m_text->setText(text);
    m_icon->hide();

    m_hold->stop();
    m_fade->stop();
    m_opacity->setOpacity(1.0);
    adjustSize();
    reposition();
    show();
    raise();
}

void SaveIndicator::fadeOut()
{
    m_fade->start();
}

void SaveIndicator::reposition()
{
    QWidget *host = parentWidget();
    if (!host) return;

    move((host->width()  - width())  / 2,
         m_progress ? host->height() / 2 + ProgressDropPx
                    : (host->height() - height()) / 2);
}

bool SaveIndicator::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize
        && isVisible())
        reposition();
    return QWidget::eventFilter(watched, event);
}
