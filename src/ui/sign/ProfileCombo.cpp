#include "ui/sign/ProfileCombo.hpp"
#include "ui/theme/Theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QStandardItemModel>
#include <QStyledItemDelegate>

namespace {

constexpr int kHeight        = 56;
constexpr int kProfileHeight = 44;
constexpr int kCommandHeight = 36;
constexpr int kPadding       = 14;
constexpr int kTile          = 32;
constexpr int kIdRole        = Qt::UserRole;
constexpr int kLogoRole      = Qt::UserRole + 1;
constexpr int kCommandRole   = Qt::UserRole + 2;
constexpr int kIconRole      = Qt::UserRole + 3;
constexpr int kCurrentRole   = Qt::UserRole + 4;
const QColor  kDanger(0xDC, 0x26, 0x26);

bool isSeparator(const QModelIndex &index)
{
    return index.data(Qt::AccessibleDescriptionRole).toString() == QLatin1String("separator");
}

void drawTile(QPainter &p, const QRect &tile, const QImage &logo, const QPalette &palette)
{
    QPainterPath clip;
    clip.addRoundedRect(QRectF(tile).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
    p.save();
    if (!logo.isNull()) {
        p.setClipPath(clip);
        p.fillRect(tile, Qt::white);
        const QImage scaled = logo.scaled(tile.size() * 2, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
        QRect target(QPoint(), scaled.size() / 2);
        target.moveCenter(tile.center());
        p.drawImage(target, scaled);
    } else {
        p.setPen(QPen(palette.color(QPalette::Mid), 1));
        p.setBrush(palette.base());
        p.drawPath(clip);
        const int icon = 20;
        p.drawPixmap(tile.center().x() - icon / 2 + 1, tile.center().y() - icon / 2 + 1,
                     Theme::renderSvg(QStringLiteral("align-left"), Theme::IconNormal, icon));
    }
    p.restore();
}

void drawProfile(QPainter &p, const QRect &rect, const QString &name, const QImage &logo,
                 bool bold, const QFont &base, const QPalette &palette)
{
    const QRect tile(rect.left() + kPadding, rect.center().y() - kTile / 2 + 1, kTile, kTile);
    drawTile(p, tile, logo, palette);

    QFont font = base;
    font.setBold(bold);
    const int left = tile.right() + kPadding;
    p.setFont(font);
    p.setPen(palette.color(QPalette::Text));
    p.drawText(QRect(left, rect.top(), rect.right() - left, rect.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(font).elidedText(name, Qt::ElideRight, rect.right() - left));
}

class ProfileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (isSeparator(index)) return { option.rect.width(), 9 };
        return { option.rect.width(),
                 index.data(kCommandRole).toInt() ? kCommandHeight : kProfileHeight };
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect;
        if (isSeparator(index)) {
            p->setPen(option.palette.color(QPalette::Mid));
            p->drawLine(r.left() + 8, r.center().y(), r.right() - 8, r.center().y());
            p->restore();
            return;
        }

        const bool enabled = index.flags() & Qt::ItemIsEnabled;
        if (enabled && (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)))
            p->fillRect(r.adjusted(4, 1, -4, -1), QColor(37, 99, 235, 30));
        if (!enabled) p->setOpacity(0.45);

        const int command = index.data(kCommandRole).toInt();
        if (command) {
            const bool danger = command == int(ProfileCombo::Command::Delete);
            const int icon = 18;
            const int x = r.left() + kPadding + (kTile - icon) / 2;
            p->drawPixmap(x, r.center().y() - icon / 2 + 1,
                          Theme::renderSvg(index.data(kIconRole).toString(),
                                           danger ? kDanger : Theme::IconNormal, icon));
            const int left = r.left() + kPadding + kTile + kPadding;
            p->setFont(option.font);
            p->setPen(option.palette.color(QPalette::Text));
            p->drawText(QRect(left, r.top(), r.right() - left, r.height()),
                        Qt::AlignLeft | Qt::AlignVCenter, index.data(Qt::DisplayRole).toString());
        } else {
            const bool current = index.data(kCurrentRole).toBool();
            const int check = 18;
            drawProfile(*p, r.adjusted(0, 0, -(kPadding * 2 + check), 0),
                        index.data(Qt::DisplayRole).toString(),
                        index.data(kLogoRole).value<QImage>(), false, option.font, option.palette);
            if (current)
                p->drawPixmap(r.right() - kPadding - check, r.center().y() - check / 2,
                              Theme::renderSvg(QStringLiteral("check"), Theme::Primary, check));
        }
        p->restore();
    }
};

}

ProfileCombo::ProfileCombo(QWidget *parent)
    : QComboBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setItemDelegate(new ProfileDelegate(this));
    connect(this, &QComboBox::activated, this, &ProfileCombo::onActivated);
}

void ProfileCombo::setProfiles(const QList<Entry> &profiles, const QString &currentId, bool canDelete)
{
    const QSignalBlocker block(this);
    clear();
    m_current = 0;
    for (const Entry &e : profiles) {
        addItem(e.name, e.id);
        const int i = count() - 1;
        setItemData(i, e.logo, kLogoRole);
        if (e.id == currentId) m_current = i;
    }
    if (count() > 0) setItemData(m_current, true, kCurrentRole);

    insertSeparator(count());
    addCommand(Command::New, QStringLiteral("plus"), tr("New profile..."), true);
    addCommand(Command::Edit, QStringLiteral("pencil"), tr("Edit profile..."), true);
    addCommand(Command::Duplicate, QStringLiteral("copy"), tr("Duplicate profile"), true);
    insertSeparator(count());
    addCommand(Command::Delete, QStringLiteral("trash-2"), tr("Delete profile"), canDelete);
    setCurrentIndex(m_current);
}

void ProfileCombo::addCommand(Command command, const QString &icon, const QString &text, bool enabled)
{
    addItem(text);
    const int i = count() - 1;
    setItemData(i, int(command), kCommandRole);
    setItemData(i, icon, kIconRole);
    if (auto *m = qobject_cast<QStandardItemModel *>(model()))
        m->item(i)->setEnabled(enabled);
}

QString ProfileCombo::currentId() const
{
    return itemData(m_current, kIdRole).toString();
}

void ProfileCombo::onActivated(int index)
{
    const int command = itemData(index, kCommandRole).toInt();
    if (!command) {
        if (index == m_current) return;
        setItemData(m_current, false, kCurrentRole);
        m_current = index;
        setItemData(m_current, true, kCurrentRole);
        Q_EMIT profileChosen(currentId());
        return;
    }
    {
        const QSignalBlocker block(this);
        setCurrentIndex(m_current);
    }
    Q_EMIT commandChosen(Command(command));
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
    if (m_current < 0 || m_current >= count()) return;
    drawProfile(p, rect().adjusted(0, 0, -(kPadding * 2 + chevron), 0), itemText(m_current),
                itemData(m_current, kLogoRole).value<QImage>(), true, font(), palette());
}
