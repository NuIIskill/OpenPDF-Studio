#include "ui/panels/ToolCustomizePopup.hpp"

#include "ui/panels/LeftSidebar.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

constexpr int kRowHeight  = 39;
constexpr int kListWidth  = 244;
constexpr int kMaxRows    = 12;
constexpr int kGripX      = 6;
constexpr int kIconX      = 30;
constexpr int kTextX      = 58;
constexpr int kIconSize   = 18;
constexpr int kCheckSize  = 18;
constexpr int kCheckRight = 10;

constexpr int kIdRole   = Qt::UserRole;
constexpr int kIconRole = Qt::UserRole + 1;

QRect checkRect(const QRect &row)
{
    return QRect(row.right() - kCheckRight - kCheckSize,
                 row.center().y() - kCheckSize / 2 + 1,
                 kCheckSize, kCheckSize);
}

class ToolRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return { kListWidth, kRowHeight };
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const bool dark    = Theme::DarkMode;
        const bool on      = idx.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        const bool hovered = opt.state & QStyle::State_MouseOver;

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        if (hovered) {
            p->setPen(Qt::NoPen);
            p->setBrush(dark ? QColor(0x4A, 0x4A, 0x4A) : QColor(0xF3, 0xF4, 0xF6));
            p->drawRoundedRect(opt.rect.adjusted(2, 1, -2, -1), 7, 7);
        }

        const QColor grip = dark ? QColor(0x7A, 0x7A, 0x7A) : QColor(0x9C, 0xA3, 0xAF);
        const QColor tint = on ? (dark ? QColor(0xD8, 0xD8, 0xD8) : QColor(0x37, 0x41, 0x51))
                               : (dark ? QColor(0x7A, 0x7A, 0x7A) : QColor(0x9C, 0xA3, 0xAF));

        const int cy = opt.rect.center().y();
        drawIcon(p, QStringLiteral("grip-vertical"), grip,
                 QRect(opt.rect.left() + kGripX, cy - 8, 16, 16));
        drawIcon(p, idx.data(kIconRole).toString(), tint,
                 QRect(opt.rect.left() + kIconX, cy - kIconSize / 2, kIconSize, kIconSize));

        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() : 10.0);
        p->setFont(f);
        p->setPen(on ? (dark ? QColor(0xE5, 0xE5, 0xE5) : QColor(0x11, 0x18, 0x27))
                     : (dark ? QColor(0x8A, 0x8A, 0x8A) : QColor(0x9C, 0xA3, 0xAF)));
        const QRect textRect(opt.rect.left() + kTextX, opt.rect.top(),
                             opt.rect.width() - kTextX - kCheckSize - kCheckRight - 8,
                             opt.rect.height());
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(idx.data().toString(),
                                                Qt::ElideRight, textRect.width()));

        paintCheckbox(p, checkRect(opt.rect), on, dark);
        p->restore();
    }

private:
    static void paintCheckbox(QPainter *p, const QRect &box, bool on, bool dark)
    {
        p->setRenderHint(QPainter::Antialiasing, true);
        if (on) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x25, 0x63, 0xEB));
            p->drawRoundedRect(box, 4, 4);

            QPainterPath tick;
            tick.moveTo(box.left() + box.width() * 0.26, box.top() + box.height() * 0.52);
            tick.lineTo(box.left() + box.width() * 0.44, box.top() + box.height() * 0.70);
            tick.lineTo(box.left() + box.width() * 0.75, box.top() + box.height() * 0.31);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(tick);
        } else {
            p->setBrush(dark ? QColor(0x3A, 0x3A, 0x3A) : QColor(0xFF, 0xFF, 0xFF));
            p->setPen(QPen(dark ? QColor(0x6B, 0x72, 0x80) : QColor(0xD1, 0xD5, 0xDB), 1.4));
            p->drawRoundedRect(QRectF(box).adjusted(0.7, 0.7, -0.7, -0.7), 4, 4);
        }
    }

    void drawIcon(QPainter *p, const QString &name, const QColor &color,
                  const QRect &target) const
    {
        if (name.isEmpty())
            return;
        const qreal dpr = p->device()->devicePixelRatioF();
        const QString key = name + QLatin1Char('/') + color.name() +
                            QLatin1Char('/') + QString::number(target.width()) +
                            QLatin1Char('@') + QString::number(dpr);
        auto it = m_cache.constFind(key);
        if (it == m_cache.constEnd())
            it = m_cache.insert(key, Theme::renderSvg(name, color, target.width(), dpr));
        p->drawPixmap(target, *it);
    }

    mutable QHash<QString, QPixmap> m_cache;
};

class ToolListWidget : public QListWidget
{
    Q_OBJECT

public:
    explicit ToolListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setObjectName(QStringLiteral("ToolCustomizeList"));
        setItemDelegate(new ToolRowDelegate(this));
        setFrameShape(QFrame::NoFrame);
        setMouseTracking(true);
        setUniformItemSizes(true);

        setSelectionMode(QAbstractItemView::SingleSelection);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        setDragDropOverwriteMode(false);
        setDropIndicatorShown(true);
        viewport()->setCursor(Qt::OpenHandCursor);

        connect(model(), &QAbstractItemModel::rowsRemoved,
                this, &ToolListWidget::reportIfSettled);
        connect(model(), &QAbstractItemModel::rowsMoved,
                this, &ToolListWidget::reportIfSettled);
    }

    void seal() { m_rows = count(); }

Q_SIGNALS:
    void orderChanged();
    void checkToggled();

protected:

    void mousePressEvent(QMouseEvent *event) override
    {
        const QModelIndex idx = indexAt(event->pos());
        if (event->button() == Qt::LeftButton && idx.isValid() &&
            checkRect(visualRect(idx)).contains(event->pos())) {
            QListWidgetItem *it = item(idx.row());
            it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked
                                                              : Qt::Checked);
            update(idx);
            Q_EMIT checkToggled();
            return;
        }
        QListWidget::mousePressEvent(event);
    }

private:
    void reportIfSettled()
    {
        if (m_rows > 0 && count() == m_rows)
            Q_EMIT orderChanged();
    }

    int m_rows { 0 };
};

}

ToolCustomizePopup::ToolCustomizePopup(const QStringList &order,
                                       const QStringList &hidden,
                                       QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ToolCustomizeCard"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    buildUi();
    fill(order, hidden);
}

void ToolCustomizePopup::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(0);

    auto *head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(0);

    auto *title = new QLabel(tr("Customize Tools"), this);
    title->setObjectName(QStringLiteral("ToolCustomizeTitle"));
    head->addWidget(title);
    head->addStretch(1);

    auto *closeBtn = new QPushButton(this);
    closeBtn->setObjectName(QStringLiteral("ToolCustomizeClose"));
    closeBtn->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted,
                                      Theme::IconMuted, Theme::IconDisabled, 16));
    closeBtn->setIconSize(QSize(16, 16));
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFlat(true);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    head->addWidget(closeBtn);
    root->addLayout(head);

    auto *hint = new QLabel(tr("Drag to reorder"), this);
    hint->setObjectName(QStringLiteral("ToolCustomizeHint"));
    root->addWidget(hint);
    root->addSpacing(6);

    m_list = new ToolListWidget(this);
    m_list->setFixedWidth(kListWidth);
    root->addWidget(m_list);
    root->addSpacing(4);

    auto *reset = new QPushButton(tr("Reset to default"), this);
    reset->setObjectName(QStringLiteral("ToolCustomizeReset"));
    reset->setCursor(Qt::PointingHandCursor);
    reset->setFlat(true);
    connect(reset, &QPushButton::clicked, this, [this]() {
        Q_EMIT resetRequested();
        close();
    });
    root->addWidget(reset, 0, Qt::AlignLeft);

    auto *list = static_cast<ToolListWidget *>(m_list);
    connect(list, &ToolListWidget::orderChanged, this, &ToolCustomizePopup::emitConfig);
    connect(list, &ToolListWidget::checkToggled, this, &ToolCustomizePopup::emitConfig);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, Theme::DarkMode ? 160 : 45));
    setGraphicsEffect(shadow);
}

void ToolCustomizePopup::fill(const QStringList &order, const QStringList &hidden)
{
    QHash<QString, const ToolDef *> byId;
    for (const ToolDef &t : LeftSidebar::toolCatalog())
        byId.insert(t.id, &t);

    for (const QString &id : order) {
        const ToolDef *def = byId.value(id, nullptr);
        if (!def)
            continue;
        auto *item = new QListWidgetItem(m_list);

        item->setData(Qt::DisplayRole, LeftSidebar::tr(def->tip.toUtf8().constData()));
        item->setData(kIdRole, def->id);
        item->setData(kIconRole, def->icon);
        item->setCheckState(hidden.contains(id) ? Qt::Unchecked : Qt::Checked);
    }

    static_cast<ToolListWidget *>(m_list)->seal();

    const int rows = qMin(m_list->count(), kMaxRows);
    m_list->setFixedHeight(rows * kRowHeight + 4);
}

void ToolCustomizePopup::emitConfig()
{
    QStringList order;
    QStringList hidden;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem *item = m_list->item(i);
        const QString id = item->data(kIdRole).toString();
        order << id;
        if (item->checkState() != Qt::Checked)
            hidden << id;
    }
    Q_EMIT configChanged(order, hidden);
}

void ToolCustomizePopup::popupAt(QWidget *anchor)
{
    QWidget *host = parentWidget();
    if (!host || !anchor)
        return;

    adjustSize();
    const QSize card = sizeHint();

    QWidget *bar = anchor->parentWidget() ? anchor->parentWidget() : anchor;
    QPoint pos(host->mapFromGlobal(bar->mapToGlobal(QPoint(bar->width(), 0))).x() + 10,
               host->mapFromGlobal(anchor->mapToGlobal(QPoint(0, 0))).y());

    pos.ry() += anchor->height() - card.height();
    pos.setY(qBound(8, pos.y(), qMax(8, host->height() - card.height() - 8)));
    pos.setX(qBound(8, pos.x(), qMax(8, host->width() - card.width() - 8)));

    setGeometry(QRect(pos, card));
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    qApp->installEventFilter(this);
}

bool ToolCustomizePopup::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        close();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && !isAncestorOf(w) && w != this)
            close();
    }
    return QFrame::eventFilter(watched, event);
}

void ToolCustomizePopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

void ToolCustomizePopup::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}

#include "ToolCustomizePopup.moc"
