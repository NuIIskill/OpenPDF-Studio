#include "ui/sign/SignOption.hpp"
#include "ui/theme/Theme.hpp"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kBox     = 20;
constexpr int kSpacing = 12;
constexpr int kGap     = 4;

}

SignOption::SignOption(const QString &title, const QString &description, QWidget *parent)
    : QAbstractButton(parent)
    , m_description(description)
{
    setCheckable(true);
    setText(title);
    setCursor(Qt::PointingHandCursor);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

QFont SignOption::descriptionFont() const
{
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() - 1 : 9);
    return f;
}

QRect SignOption::descriptionRect(int width) const
{
    const int left = kBox + kSpacing;
    const int top  = QFontMetrics(font()).height() + kGap;
    return QFontMetrics(descriptionFont())
        .boundingRect(QRect(left, top, qMax(1, width - left), 1000),
                      Qt::TextWordWrap, m_description);
}

QSize SignOption::sizeHint() const
{
    const int textWidth = qMax(QFontMetrics(font()).horizontalAdvance(text()),
                               m_description.isEmpty() ? 0 : 180);
    const int width = kBox + kSpacing + textWidth;
    return { width, heightForWidth(width) };
}

int SignOption::heightForWidth(int width) const
{
    if (m_description.isEmpty()) return qMax(kBox, QFontMetrics(font()).height());
    return qMax(kBox, descriptionRect(width).bottom() + 1);
}

void SignOption::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange)
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QAbstractButton::changeEvent(event);
}

// The stacked dialog pages do not pass height-for-width on, so the wrapped
// description asks for its height itself.
void SignOption::resizeEvent(QResizeEvent *event)
{
    QAbstractButton::resizeEvent(event);
    const int needed = heightForWidth(width());
    if (minimumHeight() != needed) setMinimumHeight(needed);
}

void SignOption::paintEvent(QPaintEvent *)
{
    const bool dark = Theme::DarkMode;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.5);

    const QFontMetrics fm(font());
    const QRectF box(0.5, (fm.height() - kBox) / 2.0 + 0.5, kBox - 1, kBox - 1);
    if (isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x25, 0x63, 0xEB));
        p.drawRoundedRect(box, 5, 5);
        p.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal x = box.left(), y = box.top(), w = box.width(), h = box.height();
        QPainterPath check;
        check.moveTo(x + w * 0.26, y + h * 0.52);
        check.lineTo(x + w * 0.44, y + h * 0.70);
        check.lineTo(x + w * 0.76, y + h * 0.32);
        p.drawPath(check);
    } else {
        p.setPen(QPen(QColor(dark ? QStringLiteral("#4B5563") : QStringLiteral("#D1D5DB")), 1.5));
        p.setBrush(palette().base());
        p.drawRoundedRect(box, 5, 5);
    }

    const int left = kBox + kSpacing;
    p.setPen(palette().color(QPalette::Text));
    p.setFont(font());
    p.drawText(QRect(left, 0, width() - left, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, text());

    p.setPen(palette().color(QPalette::PlaceholderText));
    p.setFont(descriptionFont());
    p.drawText(descriptionRect(width()), Qt::TextWordWrap, m_description);
}
