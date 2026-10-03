#include "ui/sign/SignatureCard.hpp"
#include "ui/theme/Theme.hpp"

#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int   kHeight  = 146;
constexpr int   kBadge   = 20;
constexpr int   kPreviewH = 56;

QImage loadPreviewImage(const QString &path)
{
    QImageReader reader(path);
    const QSize size = reader.size();
    const QSize box(600, 160);
    if (size.width() > box.width() || size.height() > box.height())
        reader.setScaledSize(size.scaled(box, Qt::KeepAspectRatio));
    return reader.read();
}
constexpr qreal kRadius  = 8.0;

}

SignatureCard::SignatureCard(const SignatureStore::Entry &entry, QWidget *parent)
    : QFrame(parent)
    , m_entry(entry)
    , m_image(loadPreviewImage(entry.path))
{
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(14, 34, 14, 12);
    vl->setSpacing(2);

    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFixedHeight(kPreviewH);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    if (Theme::DarkMode)
        m_preview->setStyleSheet(QStringLiteral("background: #F3F4F6; border-radius: 6px;"));
    m_name = new QLabel;
    m_name->setObjectName(QStringLiteral("SCardName"));
    m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_name->setToolTip(displayName());
    auto *date = new QLabel(QLocale().toString(entry.created, QStringLiteral("d. MMM yyyy, HH:mm")));
    date->setObjectName(QStringLiteral("SMuted"));
    date->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    vl->addWidget(m_preview);
    vl->addStretch(1);
    vl->addWidget(m_name);
    vl->addWidget(date);

    m_more = new QToolButton(this);
    m_more->setObjectName(QStringLiteral("SCardMenu"));
    m_more->setIcon(Theme::renderSvg(QStringLiteral("more-vertical"), Theme::IconNormal, 18));
    m_more->setIconSize(QSize(18, 18));
    m_more->setFixedSize(26, 26);
    m_more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_more);
    connect(menu->addAction(tr("Rename")), &QAction::triggered, this, &SignatureCard::renameRequested);
    connect(menu->addAction(tr("Delete")), &QAction::triggered, this, &SignatureCard::deleteRequested);
    m_more->setMenu(menu);
    placeMenuButton();
}

void SignatureCard::setSelected(bool selected)
{
    m_selected = selected;
    placeMenuButton();
    update();
}

void SignatureCard::placeMenuButton()
{
    m_more->move(width() - m_more->width() - (m_selected ? kBadge + 10 : 6), 6);
}

void SignatureCard::updatePreview()
{
    const QSize box(width() - 28, kPreviewH);
    if (m_image.isNull() || box.isEmpty()) return;
    const qreal dpr = devicePixelRatioF();
    QPixmap px = QPixmap::fromImage(m_image.scaled(box * dpr, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
    px.setDevicePixelRatio(dpr);
    m_preview->setPixmap(px);
}

void SignatureCard::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    placeMenuButton();
    updatePreview();
    m_name->setText(m_name->fontMetrics().elidedText(displayName(), Qt::ElideRight, width() - 28));
}

QString SignatureCard::displayName() const
{
    return m_entry.name.isEmpty() ? tr("Untitled signature") : m_entry.name;
}

void SignatureCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal pen = m_selected ? 2.0 : 1.0;
    const QRectF r = QRectF(rect()).adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2);
    p.setPen(QPen(m_selected ? Theme::Primary : palette().color(QPalette::Mid), pen));
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(r, kRadius, kRadius);

    if (!m_selected) return;
    const QRectF badge(width() - kBadge - 8, 8, kBadge, kBadge);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Primary);
    p.drawEllipse(badge);
    QPainterPath check;
    check.moveTo(badge.left() + kBadge * 0.28, badge.top() + kBadge * 0.52);
    check.lineTo(badge.left() + kBadge * 0.44, badge.top() + kBadge * 0.68);
    check.lineTo(badge.left() + kBadge * 0.74, badge.top() + kBadge * 0.36);
    p.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(check);
}

void SignatureCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        Q_EMIT clicked();
    QFrame::mousePressEvent(event);
}
