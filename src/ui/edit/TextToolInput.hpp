#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>

QT_BEGIN_NAMESPACE
class QRubberBand;
class QWidget;
QT_END_NAMESPACE

class EditController;
class HoverHighlight;

/// Mouse input of the text tool on the canvas: a click edits, a drag frames a new box, Shift frames existing text.
class TextToolInput : public QObject
{
    Q_OBJECT

public:
    TextToolInput(EditController *edit, HoverHighlight *hover, QRubberBand *band,
                  QWidget *canvas, QObject *parent = nullptr);

    void press(const QPoint &canvasPos, bool shift);
    bool move(const QPoint &canvasPos, const QPoint &scroll, bool shift);
    bool release(const QPoint &canvasPos);
    QPoint pointer() const { return m_pointer; }

private:
    void markTaken(const QList<QRect> &canvasRects);

    EditController *m_edit  { nullptr };
    HoverHighlight *m_hover { nullptr };
    QRubberBand    *m_band  { nullptr };
    QWidget        *m_canvas { nullptr };
    QWidget        *m_marks  { nullptr };
    QList<QRect>    m_taken;

    bool   m_tracking { false };
    bool   m_dragging { false };
    bool   m_lines    { false };
    QPoint m_start;
    QPoint m_pointer;
};
