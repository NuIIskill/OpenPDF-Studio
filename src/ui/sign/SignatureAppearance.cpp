#include "ui/sign/SignatureAppearance.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QDataStream>
#include <QFontMetrics>
#include <QLocale>
#include <QPainter>

namespace {

constexpr QSize kSize(800, 240);
constexpr int   kPad       = 24;
constexpr int   kLogoWidth = 190;
const QColor    kBorder(0x25, 0x63, 0xEB);
const QColor    kText(0x11, 0x18, 0x27);
const QColor    kMuted(0x6B, 0x72, 0x80);
const QColor    kDivider(0xD1, 0xD5, 0xDB);

struct Detail {
    QString icon;
    QString text;
};

QFont sized(int px, bool bold)
{
    QFont font = QApplication::font();
    font.setPixelSize(qMax(1, px));
    font.setBold(bold);
    return font;
}

bool fits(const QString &title, const QList<Detail> &details, int px, int width)
{
    if (QFontMetrics(sized(px * 115 / 100, true)).horizontalAdvance(title) > width) return false;
    for (const Detail &d : details)
        if (px * 3 / 2 + QFontMetrics(sized(px, false)).horizontalAdvance(d.text) > width) return false;
    return true;
}

}

QImage SignatureAppearance::render(const QDateTime &time) const
{
    const QString title = showName && !name.isEmpty() ? tr("Digitally signed by %1").arg(name)
                                                      : tr("Digitally signed");
    QList<Detail> details;
    if (showDate)
        details.append({ QStringLiteral("calendar"),
                         tr("Date: %1").arg(QLocale().toString(time, QLocale::ShortFormat)) });
    if (showReason && !reason.isEmpty())
        details.append({ QStringLiteral("align-left"), tr("Reason: %1").arg(reason) });
    if (showLocation && !location.isEmpty())
        details.append({ QStringLiteral("map-pin"), tr("Location: %1").arg(location) });

    QImage img(kSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                     | QPainter::SmoothPixmapTransform);
    p.setPen(QPen(kBorder, 4));
    p.drawRoundedRect(QRectF(img.rect()).adjusted(2, 2, -2, -2), 14, 14);

    QRect area = img.rect().adjusted(kPad, kPad, -kPad, -kPad);
    if (showLogo && !logo.isNull()) {
        const QRect box(area.left(), area.top(), kLogoWidth - kPad, area.height());
        const QImage scaled = logo.scaled(box.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect target(QPoint(), scaled.size());
        target.moveCenter(box.center());
        p.drawImage(target, scaled);
        const int x = area.left() + kLogoWidth;
        p.setPen(QPen(kDivider, 2));
        p.drawLine(x, area.top() + 8, x, area.bottom() - 8);
        area.setLeft(x + kPad);
    }

    const int lines = 1 + int(details.size());
    const int lineHeight = area.height() / lines;
    int px = qMin(lineHeight * 55 / 100, 40);
    while (px > 10 && !fits(title, details, px, area.width())) --px;

    int y = area.top() + (area.height() - lines * lineHeight) / 2;
    const QFont titleFont = sized(px * 115 / 100, true);
    p.setFont(titleFont);
    p.setPen(kText);
    p.drawText(QRect(area.left(), y, area.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, area.width()));

    const QFont detailFont = sized(px, false);
    const int textLeft = area.left() + px * 3 / 2;
    for (const Detail &d : std::as_const(details)) {
        y += lineHeight;
        p.drawPixmap(area.left(), y + (lineHeight - px) / 2,
                     Theme::renderSvg(d.icon, kMuted, px));
        p.setFont(detailFont);
        p.setPen(kText);
        p.drawText(QRect(textLeft, y, area.right() - textLeft, lineHeight),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(detailFont).elidedText(d.text, Qt::ElideRight, area.right() - textLeft));
    }
    return img;
}

QDataStream &operator<<(QDataStream &out, const SignatureAppearance &a)
{
    const quint8 flags = (a.showName ? 1 : 0) | (a.showDate ? 2 : 0) | (a.showReason ? 4 : 0)
                       | (a.showLocation ? 8 : 0) | (a.showLogo ? 16 : 0);
    return out << a.name << a.reason << a.location << a.logo << a.logoName << flags;
}

QDataStream &operator>>(QDataStream &in, SignatureAppearance &a)
{
    quint8 flags = 0;
    in >> a.name >> a.reason >> a.location >> a.logo >> a.logoName >> flags;
    a.showName     = flags & 1;
    a.showDate     = flags & 2;
    a.showReason   = flags & 4;
    a.showLocation = flags & 8;
    a.showLogo     = flags & 16;
    return in;
}
