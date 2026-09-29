#include "ui/view/DigitalSignatureLayer.hpp"
#include "ui/view/PageCanvas.hpp"

#include <QDataStream>
#include <QIODevice>
#include <QLabel>
#include <QPainter>

namespace {

constexpr quint32 kStateVersion = 2;

}

DigitalSignatureLayer::DigitalSignatureLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{
}

DigitalSignatureLayer::~DigitalSignatureLayer() = default;

void DigitalSignatureLayer::add(const SignRequest &request, const SignatureAppearance &appearance)
{
    Pending pending { request, appearance, nullptr };
    addPreview(pending);
    m_pending.append(pending);
    relayout();
    reportChange(tr("Digital signature added"), request.page);
}

void DigitalSignatureLayer::addPreview(Pending &pending)
{
    if (pending.request.appearance.isNull() || pending.request.bounds.isEmpty()) return;
    pending.preview = new QLabel(m_canvas->canvasWidget());
    pending.preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    pending.preview->setToolTip(tr("Signed when the document is saved"));
    pending.preview->show();
}

SignError DigitalSignatureLayer::signInto(const QString &stagingPath)
{
    m_lastError = SignError::None;
    SignatureManager manager;
    for (const Pending &pending : std::as_const(m_pending)) {
        SignRequest request = pending.request;
        if (!request.appearance.isNull())
            request.appearance = pending.appearance.render(QDateTime::currentDateTime());
        m_lastError = manager.sign(stagingPath, stagingPath, request);
        if (m_lastError != SignError::None) break;
    }
    return m_lastError;
}

void DigitalSignatureLayer::clear()
{
    for (const Pending &pending : std::as_const(m_pending))
        delete pending.preview;
    m_pending.clear();
}

void DigitalSignatureLayer::setDocument(const QString &path)
{
    Q_UNUSED(path)
    clear();
    m_lastError = SignError::None;
}

void DigitalSignatureLayer::setActiveTool(const QString &toolId)
{
    Q_UNUSED(toolId)
}

void DigitalSignatureLayer::relayout()
{
    const qreal scale = m_canvas->screenScale();
    for (const Pending &pending : std::as_const(m_pending)) {
        if (!pending.preview) continue;
        const QLabel *page = m_canvas->pageLabel(pending.request.page);
        if (!page) {
            pending.preview->hide();
            continue;
        }
        const QRectF b = pending.request.bounds;
        const QRect r(page->pos() + QPoint(qRound(b.x() * scale), qRound(b.y() * scale)),
                      QSize(qRound(b.width() * scale), qRound(b.height() * scale)).expandedTo(QSize(1, 1)));
        if (pending.preview->size() != r.size()) {
            const qreal dpr = pending.preview->devicePixelRatioF();
            QPixmap px = QPixmap::fromImage(pending.request.appearance.scaled(
                r.size() * dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            px.setDevicePixelRatio(dpr);
            pending.preview->setPixmap(px);
        }
        pending.preview->setGeometry(r);
        pending.preview->raise();
        pending.preview->show();
    }
}

QString DigitalSignatureLayer::stateKey() const
{
    return QStringLiteral("digital-signatures");
}

QByteArray DigitalSignatureLayer::state() const
{
    if (m_pending.isEmpty()) return {};
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << kStateVersion << quint32(m_pending.size());
    for (const Pending &pending : m_pending) {
        const SignRequest &r = pending.request;
        out << r.certificateId << qint32(r.page) << r.bounds << r.appearance
            << r.name << r.reason << r.location << pending.appearance;
    }
    return bytes;
}

void DigitalSignatureLayer::restoreState(const QByteArray &state)
{
    if (state == this->state()) return;
    clear();
    if (state.isEmpty()) return;

    QDataStream in(state);
    quint32 version = 0, count = 0;
    in >> version >> count;
    if (version != kStateVersion) return;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        Pending pending;
        qint32 page = 0;
        in >> pending.request.certificateId >> page >> pending.request.bounds
           >> pending.request.appearance >> pending.request.name >> pending.request.reason
           >> pending.request.location >> pending.appearance;
        pending.request.page = page;
        if (in.status() != QDataStream::Ok) break;
        addPreview(pending);
        m_pending.append(pending);
    }
    relayout();
}
