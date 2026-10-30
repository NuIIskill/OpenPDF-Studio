#include "ui/redact/RedactionLayer.hpp"

#include "ui/DocumentView.hpp"
#include "ui/redact/RedactionPanel.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/redact/RedactionWriter.hpp"

#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>

namespace {

constexpr int    kPad      = 5;
constexpr int    kBadge    = 22;
constexpr int    kGrip     = 6;
constexpr qreal  kSmallest = 3.0;

const QString kToolId = QStringLiteral("redact");

constexpr quint32 kStateVersion = 1;

QPointer<RedactionLayer> g_bound;

}

/// One marked area on the canvas: the bar, its number and, when selected, its frame and corners.
class RedactionMark : public QWidget
{
public:
    explicit RedactionMark(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void place(const QRect &area, int number, bool selected)
    {
        m_number   = number;
        m_selected = selected;
        setGeometry(area.adjusted(-kPad, -kBadge, kPad, kPad));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(kPad, kBadge, -kPad, -kPad);
        painter.fillRect(area, Qt::black);

        const QRectF badge(area.left(), area.top() - 19, 18, 16);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_selected ? QColor(0x25, 0x63, 0xEB) : QColor(0x6B, 0x72, 0x80));
        painter.drawRoundedRect(badge, 3, 3);
        QFont font = painter.font();
        font.setPixelSize(10);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, QString::number(m_number));

        if (!m_selected) return;
        painter.setPen(QPen(QColor(0x25, 0x63, 0xEB), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(area.adjusted(-3, -3, 3, 3));
        painter.setPen(QPen(QColor(0x25, 0x63, 0xEB), 1.0));
        painter.setBrush(Qt::white);
        for (const QPointF corner : { area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight() })
            painter.drawRect(QRectF(corner - QPointF(3, 3), QSizeF(6, 6)));
    }

private:
    int  m_number { 0 };
    bool m_selected { false };
};

RedactionLayer::RedactionLayer(DocumentView *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
{
    if (QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr) canvas->installEventFilter(this);
    if (m_view) m_view->installEventFilter(this);
}

RedactionLayer::~RedactionLayer()
{
    for (const QPointer<RedactionMark> &mark : std::as_const(m_marks)) delete mark.data();
}

void RedactionLayer::setDocument(const QString &path)
{
    m_documentPath = path;
    m_regions.clear();
    m_selected = -1;
    m_drag     = Drag::None;
    rebuildMarks();
    refreshPanel();
}

void RedactionLayer::setActiveTool(const QString &toolId)
{
    m_active = (toolId == kToolId);
    QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr;
    if (m_active) {
        if (canvas) {
            canvas->setMouseTracking(true);
            canvas->setCursor(Qt::CrossCursor);
        }
        bindPanel();
    } else {
        if (canvas && m_drag == Drag::None) canvas->unsetCursor();
        m_drag     = Drag::None;
        m_selected = -1;
    }
    relayout();
}

void RedactionLayer::relayout()
{
    const bool visible = shown();
    for (int i = 0; i < m_marks.size() && i < m_regions.size(); ++i) {
        RedactionMark *mark = m_marks.at(i);
        if (!mark) continue;
        const QRect area = toCanvas(m_regions.at(i));
        mark->setVisible(visible && !area.isNull());
        if (!visible || area.isNull()) continue;
        mark->place(area, i + 1, i == m_selected);
        mark->raise();
    }
}

bool RedactionLayer::writeTo(const WriteGrant &staging)
{
    if (m_regions.isEmpty()) return true;
    if (!RedactionWriter::available()) {
        qWarning() << "[Redaction] nothing can be redacted without qpdf";
        return false;
    }
    RedactionWriter::Areas areas;
    for (const Region &region : std::as_const(m_regions)) areas[region.page].append(region.rect);
    const WriteGrant redacted = staging.beside(QStringLiteral(".redacted"));
    if (RedactionWriter::write(staging.path(), redacted, areas, PdfPwStore::get(m_documentPath))
            != RedactionWriter::Result::Written) {
        QFile::remove(redacted.path());
        return false;
    }
    QFile::remove(staging.path());
    return QFile::rename(redacted.path(), staging.path());
}

QString RedactionLayer::stateKey() const
{
    return kToolId;
}

QByteArray RedactionLayer::state() const
{
    if (m_regions.isEmpty()) return {};
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << kStateVersion << qint32(m_regions.size());
    for (const Region &region : m_regions) out << qint32(region.page) << region.rect;
    return bytes;
}

void RedactionLayer::restoreState(const QByteArray &state)
{
    if (state == this->state()) return;
    QList<Region> regions;
    if (!state.isEmpty()) {
        QDataStream in(state);
        quint32 version = 0;
        qint32 count = 0;
        in >> version >> count;
        if (version != kStateVersion || in.status() != QDataStream::Ok) return;
        for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
            Region region;
            qint32 page = -1;
            in >> page >> region.rect;
            region.page = page;
            regions.append(region);
        }
        if (in.status() != QDataStream::Ok) return;
    }
    m_regions  = regions;
    m_selected = -1;
    m_drag     = Drag::None;
    rebuildMarks();
    refreshPanel();
}

bool RedactionLayer::shown() const
{
    return m_view && m_view->pageCount() > 0
        && m_view->viewMode() == DocumentView::ViewMode::Single;
}

bool RedactionLayer::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_active || !shown()) return false;
    if (watched == m_view) return handleKey(event);
    if (m_view && watched == m_view->canvasWidget()) return handleMouse(event);
    return false;
}

bool RedactionLayer::handleKey(QEvent *event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) return false;
    const int key = static_cast<QKeyEvent *>(event)->key();
    const bool removes = m_selected >= 0 && (key == Qt::Key_Delete || key == Qt::Key_Backspace);
    const bool clears  = m_selected >= 0 && key == Qt::Key_Escape;
    if (!removes && !clears) return false;
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (removes) remove(m_selected);
    else         select(-1);
    return true;
}

bool RedactionLayer::handleMouse(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        const QPoint pos = mouse->pos();
        m_view->setFocus(Qt::MouseFocusReason);
        if (const int corner = handleAt(pos); corner >= 0) {
            m_drag      = Drag::Resize;
            m_corner    = corner;
            m_pressRect = m_regions.at(m_selected).rect;
            return true;
        }
        if (const int index = regionAt(pos); index >= 0) {
            select(index);
            m_drag       = Drag::Move;
            m_pressRect  = m_regions.at(index).rect;
            m_pressPoint = toPage(m_regions.at(index).page, QRectF(pos, QSizeF())).topLeft();
            return true;
        }
        const int page = m_view->pageAtCanvasPos(pos).first;
        if (page < 0) {
            select(-1);
            return true;
        }
        m_pressPoint = toPage(page, QRectF(pos, QSizeF())).topLeft();
        m_regions.append({ page, QRectF(m_pressPoint, QSizeF()) });
        m_selected = int(m_regions.size()) - 1;
        m_drag     = Drag::Create;
        rebuildMarks();
        return true;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_drag == Drag::None || m_selected < 0) {
            updateCursor(mouse->pos());
            return m_drag != Drag::None;
        }
        Region &region = m_regions[m_selected];
        const QRectF bounds = pageRect(region.page);
        const QPointF point = toPage(region.page, QRectF(mouse->pos(), QSizeF())).topLeft();
        const QPointF clamped(qBound(bounds.left(), point.x(), bounds.right()),
                              qBound(bounds.top(), point.y(), bounds.bottom()));
        if (m_drag == Drag::Create) {
            region.rect = QRectF(m_pressPoint, clamped).normalized();
        } else if (m_drag == Drag::Move) {
            QRectF moved = m_pressRect.translated(point - m_pressPoint);
            moved.moveLeft(qBound(bounds.left(), moved.left(), bounds.right() - moved.width()));
            moved.moveTop(qBound(bounds.top(), moved.top(), bounds.bottom() - moved.height()));
            region.rect = moved;
        } else {
            const QPointF fixed = m_corner == 0 ? m_pressRect.bottomRight()
                                : m_corner == 1 ? m_pressRect.bottomLeft()
                                : m_corner == 2 ? m_pressRect.topRight()
                                                : m_pressRect.topLeft();
            region.rect = QRectF(fixed, clamped).normalized();
        }
        relayout();
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || m_drag == Drag::None) return false;
        const Drag drag = m_drag;
        m_drag = Drag::None;
        if (m_selected >= 0) {
            const Region region = m_regions.at(m_selected);
            if (drag == Drag::Create && (region.rect.width() < kSmallest || region.rect.height() < kSmallest)) {
                m_regions.removeAt(m_selected);
                m_selected = -1;
                rebuildMarks();
            } else if (drag == Drag::Create) {
                reportChange(tr("Redaction area added"), region.page);
            } else if (region.rect != m_pressRect) {
                reportChange(drag == Drag::Move ? tr("Redaction area moved")
                                                : tr("Redaction area resized"), region.page);
            }
        }
        refreshPanel();
        updateCursor(mouse->pos());
        return true;
    }
    case QEvent::MouseButtonDblClick:
        return true;
    default:
        return false;
    }
}

void RedactionLayer::updateCursor(const QPoint &pos)
{
    QWidget *canvas = m_view->canvasWidget();
    const int corner = handleAt(pos);
    if (corner == 0 || corner == 3)  canvas->setCursor(Qt::SizeFDiagCursor);
    else if (corner >= 0)            canvas->setCursor(Qt::SizeBDiagCursor);
    else if (regionAt(pos) >= 0)     canvas->setCursor(Qt::SizeAllCursor);
    else                             canvas->setCursor(Qt::CrossCursor);
}

int RedactionLayer::regionAt(const QPoint &pos) const
{
    for (int i = int(m_regions.size()) - 1; i >= 0; --i)
        if (toCanvas(m_regions.at(i)).contains(pos)) return i;
    return -1;
}

int RedactionLayer::handleAt(const QPoint &pos) const
{
    if (m_selected < 0 || m_selected >= m_regions.size()) return -1;
    const QRect area = toCanvas(m_regions.at(m_selected));
    const QPoint corners[] = { area.topLeft(), area.topRight() + QPoint(1, 0),
                               area.bottomLeft() + QPoint(0, 1), area.bottomRight() + QPoint(1, 1) };
    for (int i = 0; i < 4; ++i)
        if (std::abs(pos.x() - corners[i].x()) <= kGrip && std::abs(pos.y() - corners[i].y()) <= kGrip)
            return i;
    return -1;
}

QRect RedactionLayer::toCanvas(const Region &region) const
{
    const QLabel *label = m_view ? m_view->pageLabel(region.page) : nullptr;
    const qreal scale = m_view ? m_view->screenScale() : 0.0;
    if (!label || scale <= 0.0) return {};
    const QRectF r = region.rect;
    return QRectF(label->pos().x() + r.left() * scale, label->pos().y() + r.top() * scale,
                  r.width() * scale, r.height() * scale).toAlignedRect();
}

QRectF RedactionLayer::toPage(int page, const QRectF &canvasRect) const
{
    const QLabel *label = m_view ? m_view->pageLabel(page) : nullptr;
    const qreal scale = m_view ? m_view->screenScale() : 0.0;
    if (!label || scale <= 0.0) return {};
    return QRectF((canvasRect.left() - label->pos().x()) / scale,
                  (canvasRect.top() - label->pos().y()) / scale,
                  canvasRect.width() / scale, canvasRect.height() / scale);
}

QRectF RedactionLayer::pageRect(int page) const
{
    const QLabel *label = m_view ? m_view->pageLabel(page) : nullptr;
    return label ? toPage(page, QRectF(label->geometry())) : QRectF();
}

void RedactionLayer::bindPanel()
{
    RedactionPanel *panel = RedactionPanel::instance();
    if (!panel) return;
    if (g_bound != this) {
        if (g_bound) QObject::disconnect(panel, nullptr, g_bound.data(), nullptr);
        connect(panel, &RedactionPanel::regionActivated, this, &RedactionLayer::activate);
        connect(panel, &RedactionPanel::removeRequested, this, &RedactionLayer::remove);
        g_bound = this;
    }
    refreshPanel();
}

void RedactionLayer::refreshPanel()
{
    RedactionPanel *panel = RedactionPanel::instance();
    if (!panel || g_bound != this) return;
    QList<int> pages;
    for (const Region &region : std::as_const(m_regions)) pages.append(region.page);
    panel->setRegions(pages, m_selected);
}

void RedactionLayer::rebuildMarks()
{
    QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr;
    while (m_marks.size() > m_regions.size()) delete m_marks.takeLast().data();
    while (canvas && m_marks.size() < m_regions.size()) m_marks.append(new RedactionMark(canvas));
    relayout();
}

void RedactionLayer::select(int index)
{
    m_selected = (index >= 0 && index < m_regions.size()) ? index : -1;
    relayout();
    refreshPanel();
}

void RedactionLayer::remove(int index)
{
    if (index < 0 || index >= m_regions.size()) return;
    const int page = m_regions.at(index).page;
    m_regions.removeAt(index);
    if (m_selected == index)     m_selected = -1;
    else if (m_selected > index) --m_selected;
    rebuildMarks();
    refreshPanel();
    reportChange(tr("Redaction area removed"), page);
}

void RedactionLayer::activate(int index)
{
    if (index < 0 || index >= m_regions.size()) return;
    select(index);
    const QRect area = toCanvas(m_regions.at(index));
    if (!area.isNull()) m_view->ensureVisible(area.center().x(), area.center().y(), 60, 160);
}
