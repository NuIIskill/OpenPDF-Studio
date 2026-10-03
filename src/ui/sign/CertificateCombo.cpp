#include "ui/sign/CertificateCombo.hpp"
#include "ui/theme/Theme.hpp"

#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QPainter>
#include <QStyledItemDelegate>

namespace {

constexpr int kHeight     = 56;
constexpr int kItemHeight = 52;
constexpr int kPadding    = 14;
constexpr int kIcon       = 28;
constexpr int kNewRole    = Qt::UserRole + 2;

void drawEntry(QPainter &p, const QRect &rect, const QString &name, const QString &detail,
               const QFont &base, const QPalette &palette, bool isNew = false)
{
    p.drawPixmap(rect.left() + kPadding, rect.center().y() - kIcon / 2,
                 Theme::renderSvg(isNew ? QStringLiteral("plus") : QStringLiteral("file-badge"),
                                  Theme::IconNormal, kIcon));

    QFont bold = base;
    bold.setBold(!isNew);
    QFont small = base;
    small.setPointSizeF(base.pointSizeF() > 0 ? base.pointSizeF() - 1 : 9);
    const QFontMetrics boldFm(bold), smallFm(small);

    const int left  = rect.left() + kPadding + kIcon + kPadding;
    const int width = rect.right() - left;
    const int total = boldFm.height() + (detail.isEmpty() ? 0 : 2 + smallFm.height());
    int y = rect.center().y() - total / 2;

    p.setFont(bold);
    p.setPen(palette.color(QPalette::Text));
    p.drawText(QRect(left, y, width, boldFm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               boldFm.elidedText(name, Qt::ElideRight, width));
    if (detail.isEmpty()) return;
    y += boldFm.height() + 2;
    p.setFont(small);
    p.setPen(palette.color(QPalette::PlaceholderText));
    p.drawText(QRect(left, y, width, smallFm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               smallFm.elidedText(detail, Qt::ElideRight, width));
}

class CertificateDelegate : public QStyledItemDelegate
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
                  index.data(CertificateCombo::DetailRole).toString(),
                  option.font, option.palette, index.data(kNewRole).toBool());
        p->restore();
    }
};

}

CertificateCombo::CertificateCombo(QWidget *parent)
    : QComboBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setItemDelegate(new CertificateDelegate(this));

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        Q_EMIT menuRequested(currentIndex(), mapToGlobal(pos));
    });
    view()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        Q_EMIT menuRequested(view()->indexAt(pos).row(), view()->viewport()->mapToGlobal(pos));
    });
}

void CertificateCombo::addNewEntry(const QString &text)
{
    addItem(text);
    setItemData(count() - 1, true, kNewRole);
}

bool CertificateCombo::isNewEntry(int index) const
{
    return itemData(index, kNewRole).toBool();
}

QSize CertificateCombo::sizeHint() const
{
    return { QComboBox::sizeHint().width(), kHeight };
}

QSize CertificateCombo::minimumSizeHint() const
{
    return { QComboBox::minimumSizeHint().width(), kHeight };
}

void CertificateCombo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.6);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(hasFocus() ? QColor(0x3B, 0x82, 0xF6) : palette().color(QPalette::Mid), 1));
    p.setBrush(palette().base());
    p.drawRoundedRect(frame, 8, 8);

    const int chevron = 18;
    p.drawPixmap(width() - kPadding - chevron, (height() - chevron) / 2,
                 Theme::renderSvg(QStringLiteral("chevron-down"), Theme::IconNormal, chevron));

    const QRect content = rect().adjusted(0, 0, -(kPadding * 2 + chevron), 0);
    if (currentIndex() < 0) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.setFont(font());
        p.drawText(content.adjusted(kPadding, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   placeholderText());
        return;
    }
    drawEntry(p, content, currentText(), currentData(DetailRole).toString(), font(), palette());
}
