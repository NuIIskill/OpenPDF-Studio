#include "ui/view/LoadingSpinner.hpp"

#include "ui/theme/Theme.hpp"

#include <QEvent>
#include <QPainter>
#include <QTimer>

namespace {

constexpr int kSize        = 56;
constexpr int kShowDelayMs = 150;
constexpr int kFrameMs     = 16;
constexpr int kStepDeg     = 6;

}

LoadingSpinner::LoadingSpinner(QWidget *parent)
    : QWidget(parent)
    , m_delay(new QTimer(this))
    , m_frame(new QTimer(this))
{
    setObjectName(QStringLiteral("LoadingSpinner"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFixedSize(kSize, kSize);
    hide();

    m_delay->setSingleShot(true);
    m_delay->setInterval(kShowDelayMs);
    connect(m_delay, &QTimer::timeout, this, &LoadingSpinner::showNow);

    m_frame->setInterval(kFrameMs);
    connect(m_frame, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + kStepDeg) % 360;
        update();
    });

    parent->installEventFilter(this);
}

void LoadingSpinner::setBusy(bool busy)
{
    if (busy == m_busy) return;
    m_busy = busy;
    if (busy) {
        if (!isVisible()) m_delay->start();
        return;
    }
    m_delay->stop();
    m_frame->stop();
    hide();
}

void LoadingSpinner::showNow()
{
    m_busy = true;
    m_delay->stop();
    centre();
    show();
    raise();
    m_frame->start();
    repaint();
}

bool LoadingSpinner::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize) centre();
    return QWidget::eventFilter(obj, e);
}

void LoadingSpinner::centre()
{
    if (!parentWidget()) return;
    const QSize area = parentWidget()->size();
    move((area.width() - width()) / 2, (area.height() - height()) / 2);
}

void LoadingSpinner::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor card   = Theme::DarkMode ? QColor(40, 40, 46, 230) : QColor(255, 255, 255, 235);
    const QColor border = Theme::DarkMode ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 40);
    p.setPen(QPen(border, 1));
    p.setBrush(card);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);

    const QRectF ring = QRectF(rect()).adjusted(14, 14, -14, -14);
    QColor track = Theme::Primary;
    track.setAlpha(45);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(track, 4, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(ring);

    p.setPen(QPen(Theme::Primary, 4, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(ring, -m_angle * 16, 100 * 16);
}
