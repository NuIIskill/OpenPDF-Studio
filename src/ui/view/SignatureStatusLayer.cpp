#include "ui/view/SignatureStatusLayer.hpp"

#include "ui/sign/SignatureStatusCard.hpp"
#include "ui/sign/SignerTrust.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/view/DocumentInfoLoader.hpp"
#include "ui/view/PageCanvas.hpp"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <functional>

namespace {

constexpr int kBadge   = 22;
constexpr int kOutline = 3;

}

/// One signed field on the page: an outline while pointed at and the status badge at its corner.
class SignatureSpot : public QWidget
{
public:
    SignatureSpot(const SignatureStatus &signature, std::function<void(SignatureSpot *)> pressed,
                  QWidget *parent)
        : QWidget(parent)
        , m_signature(signature)
        , m_pressed(std::move(pressed))
    {
        setObjectName(QStringLiteral("SignatureSpot"));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }

    const SignatureStatus &signature() const { return m_signature; }
    QRect fieldRect() const { return m_field.translated(pos()); }

    void place(const QRect &field)
    {
        m_field = QRect(QPoint(kOutline, kBadge - kOutline), field.size());
        m_badge = QRect(m_field.right() + 2, 0, kBadge, kBadge);
        const QRect area = m_field.adjusted(-kOutline, -kOutline, kOutline, kOutline);
        setGeometry(QRect(field.topLeft() - m_field.topLeft(),
                          QSize(m_badge.right() + 1, area.bottom() + 1)));
        setMask(QRegion(area) + QRegion(m_badge, QRegion::Ellipse));
    }

    void setOpen(bool open)
    {
        m_open = open;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_open || underMouse()) {
            QPainterPath outline;
            outline.addRoundedRect(QRectF(m_field).adjusted(-1.5, -1.5, 1.5, 1.5), 4, 4);
            p.setPen(QPen(Theme::Primary, 2));
            p.drawPath(outline);
        }
        p.drawPixmap(m_badge.topLeft(),
                     Theme::renderSvg(SignatureStatusCard::iconName(m_signature),
                                      SignatureStatusCard::color(m_signature), kBadge,
                                      devicePixelRatioF()));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) m_pressed(this);
        event->accept();
    }

private:
    SignatureStatus                       m_signature;
    std::function<void(SignatureSpot *)>  m_pressed;
    QRect                                 m_field;
    QRect                                 m_badge;
    bool                                  m_open { false };
};

SignatureStatusLayer::SignatureStatusLayer(PageCanvas *canvas, DocumentInfoLoader *info,
                                           QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_info(info)
{
    connect(m_info, &DocumentInfoLoader::changed, this, &SignatureStatusLayer::rebuild);
}

SignatureStatusLayer::~SignatureStatusLayer() = default;

void SignatureStatusLayer::rebuild()
{
    const QString open = m_card ? m_card->field() : QString();
    clear();
    if (!m_info->loaded() || !m_info->info().signaturesChecked) return;

    for (const SignatureStatus &s : m_info->info().signatures) {
        if (s.page < 0 || s.bounds.isEmpty()) continue;
        auto *spot = new SignatureSpot(s, [this](SignatureSpot *spot) { toggleCard(spot); },
                                       m_canvas->canvasWidget());
        spot->setAttribute(Qt::WA_TransparentForMouseEvents, !m_interactive);
        m_spots.append(spot);
    }
    relayout();
    for (SignatureSpot *spot : std::as_const(m_spots))
        if (!open.isEmpty() && spot->signature().field == open) openCard(spot);
}

void SignatureStatusLayer::clear()
{
    if (m_card) {
        m_card->hide();
        m_card->deleteLater();
        m_card = nullptr;
    }
    qDeleteAll(m_spots);
    m_spots.clear();
}

void SignatureStatusLayer::toggleCard(SignatureSpot *spot)
{
    const bool same = m_card && m_card->field() == spot->signature().field;
    if (m_card) m_card->close();
    if (!same) openCard(spot);
}

void SignatureStatusLayer::openCard(SignatureSpot *spot)
{
    const SignatureStatus signature = spot->signature();
    QWidget *window = m_canvas->canvasWidget()->window();
    m_card = new SignatureStatusCard(signature, spot, m_canvas->canvasWidget());
    connect(m_card, &SignatureStatusCard::verifyRequested, this, [this]() {
        m_card->setChecking();
        m_info->reverifySignatures();
    });
    // Queued: a trust change verifies again and replaces the card while the dialog is open.
    connect(m_card, &SignatureStatusCard::detailsRequested, this, [signature, window]() {
        SignerTrust::details(signature, window);
    }, Qt::QueuedConnection);
    connect(m_card, &SignatureStatusCard::trustRequested, this, [signature, window]() {
        SignerTrust::ask(signature, window);
    }, Qt::QueuedConnection);
    const QPointer<SignatureSpot> guard(spot);
    connect(m_card, &QObject::destroyed, this, [guard]() {
        if (guard) guard->setOpen(false);
    });
    spot->setOpen(true);
    placeCard();
    m_card->show();
    m_card->raise();
    m_card->setFocus(Qt::PopupFocusReason);
}

void SignatureStatusLayer::placeCard()
{
    if (!m_card) return;
    for (const SignatureSpot *spot : std::as_const(m_spots)) {
        if (spot->signature().field != m_card->field()) continue;
        if (!spot->isVisible()) {
            m_card->hide();
            return;
        }
        const QWidget *canvas = m_canvas->canvasWidget();
        const QRect field = spot->fieldRect();
        const QSize size = m_card->sizeHint().expandedTo(m_card->minimumSize());
        int y = field.bottom() + 12;
        if (y + size.height() > canvas->height() - 8) y = field.top() - 12 - size.height();
        const int x = qBound(8, field.left(), qMax(8, canvas->width() - size.width() - 8));
        m_card->setGeometry(QRect(QPoint(x, y), size));
        m_card->raise();
        return;
    }
}

void SignatureStatusLayer::setDocument(const QString &path)
{
    Q_UNUSED(path)
    clear();
}

void SignatureStatusLayer::setActiveTool(const QString &toolId)
{
    m_interactive = toolId == QLatin1String("select") || toolId == QLatin1String("pan")
                 || toolId == QLatin1String("bookmark");
    if (!m_interactive && m_card) m_card->close();
    for (SignatureSpot *spot : std::as_const(m_spots))
        spot->setAttribute(Qt::WA_TransparentForMouseEvents, !m_interactive);
}

void SignatureStatusLayer::relayout()
{
    const qreal scale = m_canvas->screenScale();
    const int pages = m_canvas->pageCount();
    for (SignatureSpot *spot : std::as_const(m_spots)) {
        const SignatureStatus &s = spot->signature();
        const QLabel *page = s.page < pages ? m_canvas->pageLabel(s.page) : nullptr;
        if (!page) {
            spot->hide();
            continue;
        }
        const QRectF b = s.bounds;
        spot->place(QRect(page->pos() + QPoint(qRound(b.x() * scale), qRound(b.y() * scale)),
                          QSize(qRound(b.width() * scale), qRound(b.height() * scale))
                              .expandedTo(QSize(1, 1))));
        spot->raise();
        spot->show();
    }
    placeCard();
}
