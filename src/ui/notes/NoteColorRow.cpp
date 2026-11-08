#include "ui/notes/NoteColorRow.hpp"

#include "ui/notes/NoteColorPicker.hpp"
#include "ui/notes/NoteDrag.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/widgets/ContextMenu.hpp"

#include <QAbstractButton>
#include <QConicalGradient>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QtMath>

#include <functional>
#include <utility>
#include <QVBoxLayout>

namespace {

constexpr int kSwatch = 22;

class Swatch : public QAbstractButton
{
public:
    Swatch(const QColor &color, bool mixer, QWidget *parent)
        : QAbstractButton(parent), m_color(color), m_mixer(mixer)
    {
        setCheckable(!mixer && color.isValid());
        setFixedSize(kSwatch, kSwatch);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(mixer ? Qt::TabFocus : Qt::ClickFocus);
    }

    void setRemovable(std::function<void()> remove) { m_remove = std::move(remove); }

    QColor color() const { return m_color; }
    void setColor(const QColor &color)
    {
        m_color = color;
        update();
    }
    void setShift(qreal shift)
    {
        if (qFuzzyCompare(m_shift + 1, shift + 1)) return;
        m_shift = shift;
        update();
    }
    void setCurrent(bool current)
    {
        m_current = current;
        update();
    }
    void pop()
    {
        // A short swell says "this one is taken".
        auto *swell = new QVariantAnimation(this);
        swell->setDuration(260);
        swell->setStartValue(1.0);
        swell->setKeyValueAt(0.45, 1.35);
        swell->setEndValue(1.0);
        connect(swell, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_scale = v.toReal();
            update();
        });
        swell->start(QAbstractAnimation::DeleteWhenStopped);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(1.2, 1.2, -1.2, -1.2);
        const bool taken = isChecked() || m_current;
        if (taken || underMouse() || hasFocus()) {
            const QColor ring = taken
                ? (Theme::DarkMode ? QColor(0x60, 0xA5, 0xFA) : QColor(0x25, 0x63, 0xEB))
                : (Theme::DarkMode ? QColor(0x6B, 0x72, 0x80) : QColor(0xD1, 0xD5, 0xDB));
            painter.setPen(QPen(ring, taken ? 2.2 : 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(box);
        }
        QRectF dot = box.adjusted(4, 4, -4, -4).translated(m_shift, 0);
        if (!m_mixer && !m_color.isValid()) {
            // An empty place: a grey dashed ring where a color can be let go.
            QPen dash(Theme::DarkMode ? QColor(0x6B, 0x72, 0x80) : QColor(0xC4, 0xC8, 0xCE), 1.2);
            dash.setDashPattern({ 2.0, 2.0 });
            painter.setPen(dash);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(dot.adjusted(0.5, 0.5, -0.5, -0.5));
            return;
        }
        if (m_scale != 1.0) {
            const QPointF c = dot.center();
            dot = QRectF(c - QPointF(dot.width(), dot.height()) * m_scale / 2,
                         dot.size() * m_scale);
        }
        if (m_mixer) {
            // A thin ring of every hue says "mix your own"; inside, the color mixed.
            const QRectF outer = dot.adjusted(-2, -2, 2, 2);
            QConicalGradient hues(outer.center(), 90);
            for (int i = 0; i <= 6; ++i) hues.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 200, 245));
            painter.setPen(Qt::NoPen);
            painter.setBrush(hues);
            painter.drawEllipse(outer);
            painter.setBrush(m_color.isValid() ? m_color
                                               : (Theme::DarkMode ? QColor(0x3D, 0x3D, 0x3D) : QColor(Qt::white)));
            painter.drawEllipse(outer.adjusted(2.2, 2.2, -2.2, -2.2));
            if (!m_color.isValid()) {
                painter.setPen(QPen(Theme::IconMuted, 1.4));
                const QPointF c = dot.center();
                painter.drawLine(c - QPointF(2.5, 0), c + QPointF(2.5, 0));
                painter.drawLine(c - QPointF(0, 2.5), c + QPointF(0, 2.5));
            }
            return;
        }
        painter.setPen(QPen(m_color.darker(115), 1));
        painter.setBrush(m_color);
        painter.drawEllipse(dot);
        if (m_remove && m_badgeShown) {
            // A small x in the corner takes the color out of the collection.
            const QRectF badge = badgeRect();
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::DarkMode ? QColor(0xE5, 0xE7, 0xEB) : QColor(0x4B, 0x55, 0x63));
            painter.drawEllipse(badge);
            painter.setPen(QPen(Theme::DarkMode ? QColor(0x2B, 0x2B, 0x2B) : QColor(Qt::white), 1.2));
            const QRectF x = badge.adjusted(2.6, 2.6, -2.6, -2.6);
            painter.drawLine(x.topLeft(), x.bottomRight());
            painter.drawLine(x.topRight(), x.bottomLeft());
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_remove && m_badgeShown && badgeRect().contains(event->position())) {
            m_onBadge = true;
            event->accept();
            return;
        }
        QAbstractButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (std::exchange(m_onBadge, false)) {
            if (badgeRect().contains(event->position()) && m_remove) m_remove();
            event->accept();
            return;
        }
        QAbstractButton::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (m_remove && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
            m_remove();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        // The x comes only after resting on the bubble, so passing over to click it never hits it.
        if (m_remove) QTimer::singleShot(700, this, [this]() {
            if (underMouse()) {
                m_badgeShown = true;
                update();
            }
        });
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QAbstractButton::leaveEvent(event);
        m_badgeShown = false;
        update();
    }

private:
    QRectF badgeRect() const { return QRectF(width() - 9.5, 0.5, 9, 9); }

    std::function<void()> m_remove;
    bool   m_onBadge { false };
    bool   m_badgeShown { false };
    QColor m_color;
    bool   m_mixer { false };
    bool   m_current { false };
    qreal  m_shift { 0 };
    qreal  m_scale { 1 };
};

QPixmap ghostOf(const QColor &color, qreal dpr)
{
    QPixmap pixmap(QSize(kSwatch, kSwatch) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(color);
    painter.drawEllipse(QRectF(1.5, 1.5, kSwatch - 3, kSwatch - 3));
    return pixmap;
}

}

NoteColorRow::NoteColorRow(QWidget *parent)
    : QWidget(parent)
{
    m_lines = new QVBoxLayout(this);
    m_lines->setContentsMargins(0, 0, 0, 0);
    m_lines->setSpacing(2);
    m_palette = defaultPalette();
    // Swatches shake from side to side: as a hint the first time, and as
    // places to let go while a color is dragged.
    m_shake = new QVariantAnimation(this);
    m_shake->setStartValue(0.0);
    m_shake->setEndValue(1.0);
    connect(m_shake, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { shake(v.toReal()); });
    connect(m_shake, &QVariantAnimation::finished, this, [this]() { shake(-1); });
    m_again = new QTimer(this);
    m_again->setInterval(2200);
    connect(m_again, &QTimer::timeout, this, [this]() { if (!m_targets) startShake(); });
    rebuild();
}

void NoteColorRow::setHint(int stage)
{
    if (stage == m_hint) return;
    m_hint = stage;
    rebuild();
    if (m_hint > 0) {
        startShake();
        m_again->start();
    } else {
        m_again->stop();
        if (!m_targets) {
            m_shake->stop();
            shake(-1);
        }
    }
}

void NoteColorRow::setDropTargets(bool on)
{
    if (on == m_targets) return;
    m_targets = on;
    m_shake->stop();
    shake(-1);
    if (on || m_hint > 0) startShake();
}

void NoteColorRow::startShake()
{
    m_shake->stop();
    m_shake->setDuration(m_targets ? 520 : 750);
    m_shake->setLoopCount(m_targets ? -1 : 1);
    m_shake->start();
}

QList<QAbstractButton *> NoteColorRow::shaking() const
{
    // While a color is dragged, every swatch it can land on; as a hint, first
    // the mixer, and once a color is taken the others.
    if (m_targets) return m_swatches + m_empties;
    if (m_hint == 2) return m_swatches;
    if (m_hint == 1 && m_mixer) return { m_mixer };
    return {};
}

void NoteColorRow::shake(qreal progress)
{
    const QList<QAbstractButton *> moving = progress < 0 ? QList<QAbstractButton *>() : shaking();
    const qreal amplitude = m_targets ? 2.0 : 3.5;
    const qreal fade = m_targets ? 1.0 : 1.0 - progress;
    const qreal shift = progress < 0 ? 0.0
        : amplitude * fade * qSin(progress * 2 * M_PI * (m_targets ? 2 : 3));
    for (QAbstractButton *swatch : m_swatches + m_empties)
        static_cast<Swatch *>(swatch)->setShift(moving.contains(swatch) ? shift : 0.0);
    if (m_mixer) static_cast<Swatch *>(m_mixer)->setShift(moving.contains(m_mixer) ? shift : 0.0);
}

void NoteColorRow::pop(const QColor &color)
{
    for (QAbstractButton *swatch : std::as_const(m_swatches))
        if (static_cast<Swatch *>(swatch)->color().rgb() == color.rgb()) {
            static_cast<Swatch *>(swatch)->pop();
            return;
        }
    if (m_mixer && m_mixed.isValid() && m_mixed.rgb() == color.rgb()) static_cast<Swatch *>(m_mixer)->pop();
}

QList<QColor> NoteColorRow::colors()
{
    return { QColor(0xFA, 0xCC, 0x15), QColor(0xFB, 0x92, 0x3C), QColor(0xF4, 0x72, 0xB6),
             QColor(0x4A, 0xDE, 0x80), QColor(0x60, 0xA5, 0xFA), QColor(0xA7, 0x8B, 0xFA) };
}

QList<QColor> NoteColorRow::defaultPalette()
{
    QList<QColor> palette = colors();
    while (palette.size() < MaxColors) palette << QColor();
    return palette;
}

QColor NoteColorRow::standard()
{
    return colors().constFirst();
}

void NoteColorRow::setColor(const QColor &color)
{
    m_current = color;
    bool onSwatch = false;
    for (QAbstractButton *swatch : std::as_const(m_swatches)) {
        const bool same = color.isValid() && static_cast<Swatch *>(swatch)->color().rgb() == color.rgb();
        swatch->setChecked(same);
        onSwatch |= same;
    }
    if (m_mixer)
        static_cast<Swatch *>(m_mixer)->setCurrent(!onSwatch && m_mixed.isValid() && color.isValid()
                                                   && m_mixed.rgb() == color.rgb());
}

void NoteColorRow::setPalette(const QList<QColor> &colors)
{
    QList<QColor> places = colors.mid(0, MaxColors);
    while (places.size() < MaxColors) places << QColor();
    if (places == m_palette) return;
    m_palette = places;
    rebuild();
}

void NoteColorRow::setMixedColor(const QColor &color)
{
    if (color == m_mixed) return;
    m_mixed = color;
    rebuild();
}

void NoteColorRow::rebuild()
{
    while (QLayoutItem *item = m_lines->takeAt(0)) {
        if (QLayout *line = item->layout()) {
            // Later: a swatch may be the one whose click is being handled.
            while (QLayoutItem *inner = line->takeAt(0)) {
                if (QWidget *widget = inner->widget()) {
                    widget->hide();
                    widget->deleteLater();
                }
                delete inner;
            }
        }
        delete item;
    }
    m_swatches.clear();

    // Twelve places in two lines of six, the mixer at the end of the first;
    // a place without a color is a grey ring where one can be dropped.
    constexpr int kPerLine = 6;
    m_empties.clear();
    QList<QWidget *> places;
    for (int i = 0; i < MaxColors; ++i) {
        if (m_palette.value(i).isValid()) {
            places << addSwatch(m_palette.at(i), i);
            continue;
        }
        auto *empty = new Swatch(QColor(), false, this);
        empty->setObjectName(QStringLiteral("NoteColorEmpty"));
        empty->setProperty("swatchSlot", i);
        empty->setCursor(Qt::ArrowCursor);
        empty->setFocusPolicy(Qt::NoFocus);
        empty->setToolTip(tr("Drag a color here to keep it"));
        empty->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(empty, &QWidget::customContextMenuRequested, this, [this, empty, i](const QPoint &at) {
            ContextMenu menu(this);
            QAction *mixHere = menu.addAction("plus", tr("Mix a color here"));
            QAction *reset   = menu.addAction("refresh-cw", tr("Restore the standard colors"));
            QAction *chosen  = menu.exec(empty->mapToGlobal(at));
            if (chosen == mixHere) mixInto(i, empty);
            else if (chosen == reset) Q_EMIT resetRequested();
        });
        m_empties.append(empty);
        places << empty;
    }

    auto *mixer = new Swatch(m_mixed, true, this);
    mixer->setToolTip(tr("Own color: click to mix one, then drag it onto a note or into the colors to keep it"));
    connect(mixer, &QAbstractButton::clicked, this, &NoteColorRow::mix);
    auto *drag = new NoteDrag(mixer);
    if (m_mixed.isValid()) drag->setGhost(ghostOf(m_mixed, devicePixelRatioF()));
    else drag->setEnabled(false);
    connect(drag, &NoteDrag::started, this, &NoteColorRow::dragStarted);
    connect(drag, &NoteDrag::dropped, this, [this](const QPoint &at) {
        Q_EMIT dragEnded();
        Q_EMIT dropped(m_mixed, at + QPoint(kSwatch / 2, kSwatch / 2), -1);
    });
    m_mixer = mixer;

    for (int start = 0; start < places.size(); start += kPerLine) {
        auto *line = new QHBoxLayout;
        line->setSpacing(2);
        for (int i = start; i < start + kPerLine; ++i) line->addWidget(places.at(i));
        if (start == 0) line->addWidget(mixer);
        line->addStretch();
        m_lines->addLayout(line);
    }

    if (m_hint > 0) {
        auto *hint = new QLabel(m_hint == 1 ? tr("Click the colorful bubble to mix a color")
                                            : tr("Drag a color onto a note, or onto a bubble to replace it"),
                                this);
        hint->setObjectName(QStringLiteral("NoteColorHint"));
        hint->setWordWrap(true);
        auto *line = new QHBoxLayout;
        line->addWidget(hint);
        m_lines->addLayout(line);
    }

    setColor(m_current);
}

QAbstractButton *NoteColorRow::addSwatch(const QColor &color, int slot)
{
    auto *swatch = new Swatch(color, false, this);
    // A color let go on a swatch takes its place; the panel reads where it landed.
    swatch->setProperty("swatchSlot", slot);
    const QStringList names { tr("Yellow"), tr("Orange"), tr("Pink"),
                              tr("Green"), tr("Blue"), tr("Purple") };
    const QList<QColor> defaults = colors();
    const int known = int(defaults.indexOf(color));
    swatch->setToolTip((known >= 0 ? names.value(known) : color.name().toUpper())
                       + QLatin1Char('\n') + tr("Del or the x removes it"));
    connect(swatch, &QAbstractButton::clicked, this, [this, color]() {
        setColor(color);
        Q_EMIT picked(color);
    });
    swatch->setRemovable([this, slot]() { Q_EMIT removed(slot); });
    auto *drag = new NoteDrag(swatch);
    drag->setGhost(ghostOf(color, devicePixelRatioF()));
    connect(drag, &NoteDrag::started, this, &NoteColorRow::dragStarted);
    connect(drag, &NoteDrag::dropped, this, [this, color, slot](const QPoint &at) {
        Q_EMIT dragEnded();
        Q_EMIT dropped(color, at + QPoint(kSwatch / 2, kSwatch / 2), slot);
    });
    swatch->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(swatch, &QWidget::customContextMenuRequested, this, [this, swatch, slot](const QPoint &at) {
        ContextMenu menu(this);
        QAction *remove = menu.addDangerAction("trash-2", tr("Remove color"));
        QAction *reset  = menu.addAction("refresh-cw", tr("Restore the standard colors"));
        QAction *chosen = menu.exec(swatch->mapToGlobal(at));
        if (chosen == remove) Q_EMIT removed(slot);
        else if (chosen == reset) Q_EMIT resetRequested();
    });
    m_swatches.append(swatch);
    return swatch;
}

void NoteColorRow::mixInto(int slot, QWidget *anchor)
{
    auto *picker = new NoteColorPicker(m_current.isValid() ? m_current : standard(), this);
    connect(picker, &NoteColorPicker::chosen, this, [this, slot](const QColor &chosen) {
        Q_EMIT filled(slot, chosen);
    });
    picker->showBelow(anchor);
}

void NoteColorRow::mix()
{
    const QColor start = m_mixed.isValid() ? m_mixed : (m_current.isValid() ? m_current : standard());
    auto *picker = new NoteColorPicker(start, this);
    // The bubble shows what is being mixed, and goes back to its color if nothing is taken.
    QPointer<QAbstractButton> bubble = m_mixer;
    connect(picker, &NoteColorPicker::changed, this, [bubble](const QColor &color) {
        if (bubble) static_cast<Swatch *>(bubble.data())->setColor(color);
    });
    connect(picker, &QObject::destroyed, [bubble, before = m_mixed]() {
        if (bubble) static_cast<Swatch *>(bubble.data())->setColor(before);
    });
    connect(picker, &NoteColorPicker::chosen, this, [this](const QColor &chosen) {
        setMixedColor(chosen);
        Q_EMIT mixed(chosen);
        Q_EMIT picked(chosen);
    });
    picker->showBelow(m_mixer);
    static_cast<Swatch *>(m_mixer)->setColor(picker->color());
}
