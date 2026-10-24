#include "ui/export/PrintPreview.hpp"

#if defined(HAVE_QT_PRINT) && defined(HAVE_PDF_RENDERING)

#include "ui/theme/Theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

/// Paints the rendered sheet centred on a backdrop and asks for a new one when its size changes.
class SheetCanvas : public QWidget
{
public:
    explicit SheetCanvas(std::function<void()> onResize)
        : m_onResize(std::move(onResize))
    {
        setObjectName(QStringLiteral("XSheetCanvas"));
        setAttribute(Qt::WA_StyledBackground);
        setMinimumSize(240, 320);
        m_resizeTimer.setSingleShot(true);
        m_resizeTimer.setInterval(60);
        connect(&m_resizeTimer, &QTimer::timeout, this, [this] { m_onResize(); });
    }

    QSizeF paperArea(const QSizeF &paperPts) const
    {
        const QSizeF room = QSizeF(size()) - QSizeF(48, 48);
        if (paperPts.isEmpty() || room.isEmpty()) return {};
        return paperPts.scaled(room, Qt::KeepAspectRatio);
    }

    void setSheet(const QImage &image, const QRectF &printableFraction)
    {
        m_image = image;
        m_printable = printableFraction;
        update();
    }

protected:
    void resizeEvent(QResizeEvent *) override { m_resizeTimer.start(); }

    void paintEvent(QPaintEvent *) override
    {
        if (m_image.isNull()) return;
        QPainter p(this);
        const QSizeF shown = m_image.deviceIndependentSize();
        const QRectF paper(QPointF((width() - shown.width()) / 2.0,
                                   (height() - shown.height()) / 2.0), shown);
        p.setPen(Qt::NoPen);
        for (int i = 1; i <= 4; ++i) {
            p.setBrush(QColor(0, 0, 0, 14));
            p.drawRect(paper.adjusted(-i, -i + 2, i, i + 2));
        }
        p.drawImage(paper, m_image);

        const QRectF area(paper.x() + m_printable.x() * paper.width(),
                          paper.y() + m_printable.y() * paper.height(),
                          m_printable.width() * paper.width(),
                          m_printable.height() * paper.height());
        if (area.isValid() && area != paper) {
            QPen dash(QColor(0, 0, 0, 70), 1, Qt::DashLine);
            p.setPen(dash);
            p.setBrush(Qt::NoBrush);
            p.drawRect(area.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }

private:
    std::function<void()> m_onResize;
    QTimer m_resizeTimer;
    QImage m_image;
    QRectF m_printable;
};

PrintPreview::PrintPreview(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("XCard"));
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(1, 1, 1, 12);
    vl->setSpacing(0);

    auto *head = new QLabel(tr("Print preview"));
    head->setObjectName(QStringLiteral("XCardTitle"));
    head->setContentsMargins(20, 14, 20, 12);
    vl->addWidget(head);

    m_canvas = new SheetCanvas([this] { render(); });
    vl->addWidget(m_canvas, 1);

    auto *nav = new QHBoxLayout;
    nav->setContentsMargins(0, 12, 0, 4);
    nav->setSpacing(16);
    const qreal dpr = devicePixelRatioF();
    m_prev = new QPushButton;
    m_next = new QPushButton;
    m_prev->setIcon(Theme::renderSvg(QStringLiteral("chevron-left"), Qt::white, 18, dpr));
    m_next->setIcon(Theme::renderSvg(QStringLiteral("chevron-right"), Qt::white, 18, dpr));
    for (QPushButton *b : { m_prev, m_next }) {
        b->setObjectName(QStringLiteral("XNav"));
        b->setFixedSize(38, 34);
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(m_prev, &QPushButton::clicked, this, [this] { showSheet(m_sheet - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] { showSheet(m_sheet + 1); });
    m_position = new QLabel;
    m_position->setObjectName(QStringLiteral("XField"));
    m_position->setAlignment(Qt::AlignCenter);
    m_position->setMinimumWidth(130);
    nav->addStretch(1);
    nav->addWidget(m_prev);
    nav->addWidget(m_position);
    nav->addWidget(m_next);
    nav->addStretch(1);
    vl->addLayout(nav);

    m_paper = new QLabel;
    m_paper->setObjectName(QStringLiteral("XMuted"));
    m_paper->setAlignment(Qt::AlignCenter);
    vl->addWidget(m_paper);
}

void PrintPreview::setSheets(const PrintSheets &sheets, const QPageLayout &layout,
                             bool countSheets)
{
    m_sheets.emplace(sheets);
    m_layout = layout;
    m_countSheets = countSheets;

    const QPageSize size = layout.pageSize();
    const bool inches = size.definitionUnits() == QPageSize::Inch;
    const QSizeF dims = layout.fullRect(inches ? QPageLayout::Inch
                                               : QPageLayout::Millimeter).size();
    const int decimals = inches ? 1 : 0;
    m_paper->setText(QStringLiteral("%1  ·  %2 × %3 %4")
                         .arg(size.name())
                         .arg(dims.width(), 0, 'f', decimals)
                         .arg(dims.height(), 0, 'f', decimals)
                         .arg(inches ? QStringLiteral("in") : QStringLiteral("mm")));
    showSheet(m_sheet);
}

void PrintPreview::showSheet(int sheet)
{
    const int count = m_sheets ? m_sheets->sheetCount() : 0;
    m_sheet = qBound(0, sheet, qMax(0, count - 1));
    m_prev->setEnabled(m_sheet > 0);
    m_next->setEnabled(m_sheet + 1 < count);
    m_position->setText(count == 0 ? QString{}
                        : m_countSheets ? tr("Sheet %1 of %2").arg(m_sheet + 1).arg(count)
                                            : tr("Page %1 of %2").arg(m_sheet + 1).arg(count));
    render();
}

void PrintPreview::render()
{
    if (!m_sheets || m_sheets->sheetCount() == 0) {
        m_canvas->setSheet({}, {});
        return;
    }
    const QRectF paper = m_layout.fullRect(QPageLayout::Point);
    QPageLayout layout = m_layout;
    layout.setUnits(QPageLayout::Point);
    const QRectF printable = paper.marginsRemoved(layout.minimumMargins());

    const QSizeF shown = m_canvas->paperArea(paper.size());
    if (shown.isEmpty()) return;
    const qreal dpr = devicePixelRatioF();
    QImage image = m_sheets->preview(m_sheet, paper.size(), printable,
                                     shown.width() / paper.width() * dpr);
    image.setDevicePixelRatio(dpr);
    m_canvas->setSheet(image, QRectF(printable.x() / paper.width(),
                                     printable.y() / paper.height(),
                                     printable.width() / paper.width(),
                                     printable.height() / paper.height()));
}

#endif
