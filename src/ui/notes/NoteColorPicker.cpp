#include "ui/notes/NoteColorPicker.hpp"

#include "ui/theme/Theme.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QVBoxLayout>

#include <functional>

namespace {

// A surface that reports where it is pressed or dragged, in 0..1 on both axes.
class Pad : public QWidget
{
public:
    Pad(std::function<void(QPainter &, const QRectF &)> paint,
        std::function<void(qreal, qreal)> moved, QWidget *parent)
        : QWidget(parent), m_paint(std::move(paint)), m_moved(std::move(moved))
    {
        setCursor(Qt::CrossCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_paint(painter, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    void mousePressEvent(QMouseEvent *event) override { report(event); }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) report(event);
    }

private:
    void report(QMouseEvent *event)
    {
        const QPointF at = event->position();
        m_moved(qBound(0.0, at.x() / qMax(1, width() - 1), 1.0),
                qBound(0.0, at.y() / qMax(1, height() - 1), 1.0));
    }

    std::function<void(QPainter &, const QRectF &)> m_paint;
    std::function<void(qreal, qreal)> m_moved;
};

void drawHandle(QPainter &painter, const QPointF &at, const QColor &fill)
{
    painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(at, 8, 8);
    painter.setPen(QPen(Qt::white, 2.5));
    painter.setBrush(fill);
    painter.drawEllipse(at, 6.5, 6.5);
}

}

NoteColorPicker::NoteColorPicker(const QColor &start, QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    const QColor hsv = start.isValid() ? start.toHsv() : QColor(Qt::yellow).toHsv();
    m_hue        = qMax(0, hsv.hsvHue());
    m_saturation = hsv.hsvSaturation();
    m_value      = hsv.value();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("NoteColorPicker"));
    outer->addWidget(card);

    auto *column = new QVBoxLayout(card);
    column->setContentsMargins(12, 12, 12, 12);
    column->setSpacing(10);

    m_field = new Pad([this](QPainter &p, const QRectF &r) {
        QPainterPath round;
        round.addRoundedRect(r, 8, 8);
        p.setClipPath(round);
        p.fillRect(r, QColor::fromHsv(m_hue, 255, 255));
        QLinearGradient white(r.topLeft(), r.topRight());
        white.setColorAt(0, Qt::white);
        white.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillRect(r, white);
        QLinearGradient black(r.topLeft(), r.bottomLeft());
        black.setColorAt(0, QColor(0, 0, 0, 0));
        black.setColorAt(1, Qt::black);
        p.fillRect(r, black);
        p.setClipping(false);
        drawHandle(p, QPointF(r.left() + r.width() * m_saturation / 255.0,
                              r.top() + r.height() * (1.0 - m_value / 255.0)), color());
    }, [this](qreal x, qreal y) {
        setHsv(m_hue, qRound(x * 255), qRound((1.0 - y) * 255));
    }, card);
    m_field->setFixedSize(208, 128);
    column->addWidget(m_field);

    m_strip = new Pad([this](QPainter &p, const QRectF &r) {
        const QRectF bar = r.adjusted(0, 3, 0, -3);
        QLinearGradient hues(bar.topLeft(), bar.topRight());
        for (int i = 0; i <= 6; ++i) hues.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
        p.setPen(Qt::NoPen);
        p.setBrush(hues);
        p.drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);
        const qreal x = qBound(r.left() + 8, r.left() + r.width() * m_hue / 359.0, r.right() - 8);
        drawHandle(p, QPointF(x, r.center().y()), QColor::fromHsv(m_hue, 255, 255));
    }, [this](qreal x, qreal) {
        setHsv(qRound(x * 359), m_saturation, m_value);
    }, card);
    m_strip->setFixedSize(208, 18);
    column->addWidget(m_strip);

    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    m_preview = new Pad([this](QPainter &p, const QRectF &r) {
        p.setPen(QPen(color().darker(115), 1));
        p.setBrush(color());
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
    }, [](qreal, qreal) {}, card);
    m_preview->setCursor(Qt::ArrowCursor);
    m_preview->setFixedSize(24, 24);
    row->addWidget(m_preview);
    m_hex = new QLineEdit(card);
    m_hex->setObjectName(QStringLiteral("NoteColorHex"));
    m_hex->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("#?[0-9A-Fa-f]{0,6}")), m_hex));
    m_hex->setFixedWidth(88);
    connect(m_hex, &QLineEdit::textEdited, this, [this](const QString &text) {
        QString name = text.trimmed();
        if (!name.startsWith(u'#')) name.prepend(u'#');
        const QColor typed(name);
        if (name.size() == 7 && typed.isValid()) {
            const QColor hsv = typed.toHsv();
            setHsv(qMax(0, hsv.hsvHue()), hsv.hsvSaturation(), hsv.value(), true);
            m_typed = typed;
            Q_EMIT changed(typed);
        }
    });
    row->addWidget(m_hex);
    row->addStretch();
    m_apply = new QPushButton(tr("Apply"), card);
    m_apply->setObjectName(QStringLiteral("NoteColorApply"));
    m_apply->setCursor(Qt::PointingHandCursor);
    m_apply->setDefault(true);
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        Q_EMIT chosen(color());
        close();
    });
    row->addWidget(m_apply);
    column->addLayout(row);

    setHsv(m_hue, m_saturation, m_value);
}

void NoteColorPicker::setHsv(int hue, int saturation, int value, bool fromHex)
{
    m_hue        = qBound(0, hue, 359);
    m_saturation = qBound(0, saturation, 255);
    m_value      = qBound(0, value, 255);
    // A typed color is kept exactly; going through hue and saturation would round it.
    m_typed = QColor();
    if (!fromHex) m_hex->setText(color().name().toUpper());
    m_field->update();
    m_strip->update();
    m_preview->update();
    Q_EMIT changed(color());
}

void NoteColorPicker::showBelow(QWidget *anchor)
{
    adjustSize();
    QPoint at = anchor->mapToGlobal(QPoint(anchor->width() / 2 - width() / 2, anchor->height() + 6));
    if (const QScreen *screen = anchor->screen()) {
        const QRect room = screen->availableGeometry();
        at.setX(qBound(room.left(), at.x(), qMax(room.left(), room.right() - width())));
        if (at.y() + height() > room.bottom())
            at.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 6);
    }
    move(at);
    show();
    m_hex->setFocus(Qt::PopupFocusReason);
    m_hex->selectAll();
}

void NoteColorPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        m_apply->click();
        return;
    }
    QFrame::keyPressEvent(event);
}
