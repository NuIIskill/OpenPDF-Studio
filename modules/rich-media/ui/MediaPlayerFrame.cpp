// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/ui/MediaPlayerFrame.hpp"

#include "rich-media/ui/PlayerEngine.hpp"
#include "ui/theme/Theme.hpp"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTime>
#include <QTimer>

namespace {

constexpr int kBarHeight = 34;

QPushButton *barButton(const QString &icon, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setIcon(Theme::makeIcon(icon, QColor(0xE5, 0xE7, 0xEB),
                                    QColor(0xE5, 0xE7, 0xEB),
                                    QColor(0x6B, 0x72, 0x80), 16));
    button->setFixedSize(28, 24);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; }"
        "QPushButton:hover { background: rgba(255,255,255,28); border-radius: 4px; }"));
    return button;
}

}

MediaPlayerFrame::MediaPlayerFrame(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setCursor(Qt::ArrowCursor);
    buildUi();
}

MediaPlayerFrame::~MediaPlayerFrame()
{
    if (m_engine) m_engine->stop();
}

void MediaPlayerFrame::buildUi()
{

    m_bar = new QWidget(this);
    m_bar->setStyleSheet(QStringLiteral("background: rgba(15,23,42,225);"));

    m_playPause = barButton(QStringLiteral("pause"), m_bar);
    m_position  = new QSlider(Qt::Horizontal, m_bar);
    m_position->setRange(0, 0);
    m_position->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height:3px; background:#475569; border-radius:1px; }"
        "QSlider::sub-page:horizontal { background:#3B82F6; border-radius:1px; }"
        "QSlider::handle:horizontal { background:#FFFFFF; width:11px; height:11px;"
        " margin:-4px 0; border-radius:5px; }"));

    m_time = new QLabel(QStringLiteral("0:00 / 0:00"), m_bar);
    m_time->setStyleSheet(QStringLiteral("color:#CBD5E1; font-size:11px;"
                                         "background:transparent;"));
    m_mute  = barButton(QStringLiteral("volume-2"), m_bar);
    m_close = barButton(QStringLiteral("x"), m_bar);
    m_close->setToolTip(tr("Close the player"));

    m_engine = PlayerEngine::create(this, this);

    connect(m_engine, &PlayerEngine::imageChanged, this, [this]() {
        m_source = m_engine->image();
        m_scaledStale = true;
        noteProgress();
        update();
    });
    connect(m_engine, &PlayerEngine::durationChanged, this, [this](qint64 duration) {
        m_position->setRange(0, static_cast<int>(duration));
    });
    connect(m_engine, &PlayerEngine::positionChanged, this, [this](qint64 position) {

        if (position > 0 && m_engine->drawsItself()) noteProgress();
        if (!m_seeking) m_position->setValue(static_cast<int>(position));
        m_time->setText(QStringLiteral("%1 / %2")
            .arg(formatTime(position), formatTime(m_engine->duration())));
    });
    connect(m_engine, &PlayerEngine::playingChanged, this, [this]() {

        if (m_engine->isPlaying() && m_watchdog && m_watchdog->isActive())
            m_watchdog->start();
        updatePlayIcon();
    });
    connect(m_engine, &PlayerEngine::failed, this, [this](const QString &message) {
        if (m_watchdog) m_watchdog->stop();
        Q_EMIT failed(message);
    });

    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(6000);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        Q_EMIT failed(tr("nothing was decoded within six seconds"));
    });

    connect(m_playPause, &QPushButton::clicked, this,
            [this]() { m_engine->togglePause(); });
    connect(m_mute, &QPushButton::clicked, this, [this]() {
        m_engine->setMuted(!m_engine->isMuted());
        m_mute->setIcon(Theme::makeIcon(
            m_engine->isMuted() ? QStringLiteral("volume-x")
                                : QStringLiteral("volume-2"),
            QColor(0xE5, 0xE7, 0xEB), QColor(0xE5, 0xE7, 0xEB),
            QColor(0x6B, 0x72, 0x80), 16));
    });
    connect(m_close, &QPushButton::clicked, this, &MediaPlayerFrame::closeRequested);

    connect(m_position, &QSlider::sliderPressed,  this, [this]() { m_seeking = true; });
    connect(m_position, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        m_engine->setPosition(m_position->value());
    });
}

void MediaPlayerFrame::setPoster(const QImage &poster)
{
    m_poster = poster;
    update();
}

QRectF MediaPlayerFrame::videoRect() const
{
    QSizeF source;
    if (!m_source.isNull())      source = m_source.size();
    else if (m_engine && m_engine->frameSize().isValid())
                                 source = m_engine->frameSize();
    else if (!m_poster.isNull()) source = m_poster.size();
    if (source.isEmpty() || source.height() <= 0) return rect();

    const qreal aspect = source.width() / source.height();
    qreal w = width();
    qreal h = w / aspect;
    if (h > height()) { h = height(); w = h * aspect; }
    return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

void MediaPlayerFrame::rescale()
{
    m_scaledStale = false;
    const QRect target = videoRect().toRect();
    if (m_source.isNull() || target.isEmpty()) { m_scaled = QPixmap(); return; }

    m_scaled = QPixmap::fromImage(
        m_source.scaled(target.size(), Qt::IgnoreAspectRatio,
                        Qt::FastTransformation));
}

void MediaPlayerFrame::paintEvent(QPaintEvent *)
{
    if (m_scaledStale) rescale();

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (m_engine && m_engine->drawsItself() && m_started) return;

    const QRectF target = videoRect();
    if (!m_scaled.isNull()) {
        painter.drawPixmap(target.topLeft().toPoint(), m_scaled);
    } else if (!m_poster.isNull()) {

        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, m_poster, m_poster.rect());
    }
}

void MediaPlayerFrame::layOutBar()
{
    if (!m_bar) return;
    const int barHeight = qMin(kBarHeight, qMax(20, height() / 3));
    m_bar->setGeometry(0, height() - barHeight, width(), barHeight);
    m_bar->raise();

    const int pad = 8, gap = 8;
    int x = pad;
    const auto place = [&](QWidget *w, int width) {
        w->setGeometry(x, (barHeight - w->height()) / 2, width, w->height());
        x += width + gap;
    };
    place(m_playPause, m_playPause->width());

    const int timeWidth = qMax(64, m_time->fontMetrics()
                                   .horizontalAdvance(QStringLiteral("00:00 / 00:00")) + 4);
    const int tail = timeWidth + gap + m_mute->width() + gap + m_close->width() + pad;
    const int sliderWidth = qMax(20, width() - x - tail);
    m_position->setFixedHeight(16);
    place(m_position, sliderWidth);
    m_time->setFixedHeight(barHeight - 8);
    place(m_time, timeWidth);
    place(m_mute, m_mute->width());
    place(m_close, m_close->width());
}

static QRect pictureArea(const QRectF &video, const QWidget *bar)
{
    QRect area = video.toRect();
    if (bar && bar->isVisible())
        area.setBottom(qMin(area.bottom(), bar->y() - 1));
    return area;
}

void MediaPlayerFrame::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layOutBar();
    m_scaledStale = true;
    if (m_engine && m_engine->drawsItself())
        m_engine->setVideoRect(pictureArea(videoRect(), m_bar));
}

void MediaPlayerFrame::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    if (m_engine) m_engine->togglePause();
}

void MediaPlayerFrame::noteProgress()
{
    if (m_watchdog) m_watchdog->stop();
}

void MediaPlayerFrame::updatePlayIcon()
{
    const bool playing = m_engine && m_engine->isPlaying();
    m_playPause->setIcon(Theme::makeIcon(
        playing ? QStringLiteral("pause") : QStringLiteral("play"),
        QColor(0xE5, 0xE7, 0xEB), QColor(0xE5, 0xE7, 0xEB),
        QColor(0x6B, 0x72, 0x80), 16));
}

QString MediaPlayerFrame::formatTime(qint64 milliseconds)
{
    if (milliseconds < 0) milliseconds = 0;
    const qint64 seconds = milliseconds / 1000;
    const qint64 hours   = seconds / 3600;
    const QTime time(static_cast<int>(hours), static_cast<int>((seconds / 60) % 60),
                     static_cast<int>(seconds % 60));
    return time.toString(hours > 0 ? QStringLiteral("h:mm:ss")
                                   : QStringLiteral("m:ss"));
}

void MediaPlayerFrame::play(const QString &filePath, bool showControls,
                            bool loop, bool muted)
{
    m_bar->setVisible(showControls);
    layOutBar();
    m_started = true;

    if (m_engine->drawsItself())
        m_engine->setVideoRect(pictureArea(videoRect(), m_bar));
    m_engine->play(filePath, loop, muted);
    if (m_engine->drawsItself())
        m_engine->setVideoRect(pictureArea(videoRect(), m_bar));
    m_watchdog->start();
    updatePlayIcon();
}

QString MediaPlayerFrame::engineReport() const
{
    return m_engine ? m_engine->report() : QStringLiteral("engine: none");
}

void MediaPlayerFrame::stop()
{
    if (m_watchdog) m_watchdog->stop();
    if (m_engine) m_engine->stop();
    m_started = false;
    m_source = QImage();
    m_scaled = QPixmap();
}
