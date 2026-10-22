#include "ui/edit/TextToolInput.hpp"

#include "ui/edit/EditController.hpp"
#include "ui/view/HoverHighlight.hpp"

#include <QPainter>
#include <QRect>
#include <QRubberBand>
#include <QWidget>

namespace {

/// The text a Shift frame takes, painted as selected text.
class TakenMarks : public QWidget
{
public:
    using QWidget::QWidget;

    void setRects(const QList<QRect> &rects, const QPoint &origin)
    {
        m_rects.clear();
        for (const QRect &r : rects) m_rects.append(r.translated(-origin));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        for (const QRect &r : std::as_const(m_rects)) p.fillRect(r, QColor(59, 130, 246, 90));
    }

private:
    QList<QRect> m_rects;
};

}

TextToolInput::TextToolInput(EditController *edit, HoverHighlight *hover, QRubberBand *band,
                             QWidget *canvas, QObject *parent)
    : QObject(parent)
    , m_edit(edit)
    , m_hover(hover)
    , m_band(band)
    , m_canvas(canvas)
{}

void TextToolInput::press(const QPoint &canvasPos, bool shift)
{
    m_start    = canvasPos;
    m_pointer  = canvasPos;
    m_tracking = true;
    m_dragging = false;
    m_lines    = shift;
    if (m_lines) {
        m_hover->hide();
#ifdef HAVE_PDF_RENDERING
        m_edit->startFrame();
#endif
    }
}

void TextToolInput::markTaken(const QList<QRect> &canvasRects)
{
    // While a frame is drawn, the text it takes shows as selected, all of it
    // painted by one widget that repaints only when the text taken changes.
    if (canvasRects == m_taken) return;
    m_taken = canvasRects;
    if (!m_marks) {
        m_marks = new TakenMarks(m_canvas);
        m_marks->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    if (m_taken.isEmpty()) { m_marks->hide(); return; }
    QRect area;
    for (const QRect &r : std::as_const(m_taken)) area = area.united(r);
    static_cast<TakenMarks *>(m_marks)->setRects(m_taken, area.topLeft());
    m_marks->setGeometry(area);
    if (m_marks->isHidden()) {
        m_marks->raise();
        m_marks->show();
    }
    m_marks->update();
}

bool TextToolInput::move(const QPoint &canvasPos, const QPoint &scroll, bool shift)
{
    m_pointer = canvasPos;
    if (!m_tracking) {
        if (shift) m_hover->hide();
        else       m_hover->showAt(canvasPos);
        return false;
    }
    m_hover->hide();
    if (!m_dragging && (canvasPos - m_start).manhattanLength() > 6)
        m_dragging = true;
    if (m_dragging) {
        m_band->setGeometry(QRect(m_start - scroll, canvasPos - scroll).normalized());
        m_band->show();
#ifdef HAVE_PDF_RENDERING
        if (m_lines) markTaken(m_edit->framedText(m_start, canvasPos));
#endif
    }
    return true;
}

bool TextToolInput::release(const QPoint &canvasPos)
{
    if (!m_tracking) return false;
    m_tracking = false;
    const bool dragged = m_dragging;
    m_dragging = false;
    const QRect band = m_band->geometry();
    m_band->hide();
    markTaken({});

    if (m_lines && dragged) {
        // Shift: the text inside the drawn frame is edited, whatever the detection found.
        m_lines = false;
#ifdef HAVE_PDF_RENDERING
        m_edit->handleFrame(m_start, canvasPos);
#endif
        return true;
    }
    m_lines = false;
    if (dragged) {
#ifdef HAVE_PDF_RENDERING
        if (band.width() > 30 && band.height() > 15)
            m_edit->createTextFrame(band);
#endif
        return true;
    }
#ifdef HAVE_PDF_RENDERING
    m_edit->handleClick(m_start);
#endif
    return true;
}
