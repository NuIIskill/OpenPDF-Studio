#include "ui/sign/ProfileCombo.hpp"
#include "ui/theme/Theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace {

constexpr int kHeight     = 56;
constexpr int kItemHeight = 52;
constexpr int kPadding    = 14;
constexpr int kTile       = 36;
constexpr int kNewRole    = Qt::UserRole + 2;

void drawTile(QPainter &p, const QRect &tile, const QImage &logo, bool isNew)
{
    QPainterPath clip;
    clip.addRoundedRect(QRectF(tile), 6, 6);
    p.save();
    p.setClipPath(clip);
    if (!logo.isNull()) {
        p.fillRect(tile, Qt::white);
        const QImage scaled = logo.scaled(tile.size() * 2, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
        QRect target(QPoint(), scaled.size() / 2);
        target.moveCenter(tile.center());
        p.drawImage(target, scaled);
    } else {
        p.fillRect(tile, QColor(37, 99, 235, isNew ? 20 : 36));
        const int icon = 20;
        p.drawPixmap(tile.center().x() - icon / 2 + 1, tile.center().y() - icon / 2 + 1,
                     Theme::renderSvg(isNew ? QStringLiteral("plus") : QStringLiteral("signature"),
                                      isNew ? Theme::IconNormal : Theme::Primary, icon));
    }
    p.restore();
}

void drawEntry(QPainter &p, const QRect &rect, const QString &name, const QImage &logo,
               bool isNew, const QFont &base, const QPalette &palette)
{
    const QRect tile(rect.left() + kPadding, rect.center().y() - kTile / 2 + 1, kTile, kTile);
    drawTile(p, tile, logo, isNew);

    QFont font = base;
    font.setBold(!isNew);
    const QFontMetrics fm(font);
    const int left = tile.right() + kPadding;
    p.setFont(font);
    p.setPen(palette.color(QPalette::Text));
    p.drawText(QRect(left, rect.top(), rect.right() - left, rect.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(name, Qt::ElideRight, rect.right() - left));
}

class ProfileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return { option.rect.width(), kItemHeight };
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver))
            p->fillRect(option.rect, QColor(37, 99, 235, 30));
        drawEntry(*p, option.rect.adjusted(0, 0, -kPadding, 0),
                  index.data(Qt::DisplayRole).toString(),
                  index.data(ProfileCombo::LogoRole).value<QImage>(),
                  index.data(kNewRole).toBool(), option.font, option.palette);
        p->restore();
    }
};

}

ProfileCombo::ProfileCombo(QWidget *parent)
    : QComboBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setItemDelegate(new ProfileDelegate(this));
}

void ProfileCombo::addProfile(const QString &id, const QString &name, const QImage &logo)
{
    addItem(name, id);
    setItemData(count() - 1, logo, LogoRole);
}

void ProfileCombo::addNewEntry(const QString &text)
{
    addItem(text);
    setItemData(count() - 1, true, kNewRole);
}

bool ProfileCombo::isNewEntry(int index) const
{
    return itemData(index, kNewRole).toBool();
}

QSize ProfileCombo::sizeHint() const
{
    return { QComboBox::sizeHint().width(), kHeight };
}

QSize ProfileCombo::minimumSizeHint() const
{
    return { QComboBox::minimumSizeHint().width(), kHeight };
}

void ProfileCombo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.6);

    p.setPen(QPen(hasFocus() ? QColor(0x3B, 0x82, 0xF6) : palette().color(QPalette::Mid), 1));
    p.setBrush(palette().base());
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    const int chevron = 18;
    p.drawPixmap(width() - kPadding - chevron, (height() - chevron) / 2,
                 Theme::renderSvg(QStringLiteral("chevron-down"), Theme::IconNormal, chevron));
    if (currentIndex() < 0) return;
    drawEntry(p, rect().adjusted(0, 0, -(kPadding * 2 + chevron), 0), currentText(),
              currentData(LogoRole).value<QImage>(), isNewEntry(currentIndex()), font(), palette());
}
