#pragma once

#include "engine/edit/EditSession.hpp"
#include "ui/draw/DrawTool.hpp"
#include "ui/view/PageCanvas.hpp"

#include <QObject>

QT_BEGIN_NAMESPACE
class QUndoStack;
class QWidget;
QT_END_NAMESPACE

class DrawingUndoCommand;

/// Freehand drawing interaction for the document canvas.
class DrawingLayer : public QObject
{
    Q_OBJECT

public:
    explicit DrawingLayer(PageCanvas *canvas, QObject *parent = nullptr);

    void setSource(EditSession *session, QUndoStack *undo);
    void setActive(bool active);
    void setTool(DrawTool tool);
    void setColor(const QColor &color);
    void setWidth(qreal widthPt);

    bool handlePress(const QPoint &canvasPos);
    bool handleMove(const QPoint &canvasPos);
    bool handleRelease();

    void clear();
    void relayout();

Q_SIGNALS:
    void pageNeedsRerender(int page);
    void strokeAdded(int page);
    void strokesRemoved(int page);

private:
    bool appendPoint(const QPoint &canvasPos);
    bool eraseAt(const QPoint &canvasPos);
    void finishGesture();
    void restoreState(const QList<EditSession::DrawStroke> &strokes);
    void pushUndo(const QList<EditSession::DrawStroke> &before,
                  const QList<EditSession::DrawStroke> &after,
                  const QString &text);
    void updatePreview();

    friend class DrawingUndoCommand;

    PageCanvas *m_canvas { nullptr };
    EditSession *m_session { nullptr };
    QUndoStack *m_undo { nullptr };
    QWidget *m_preview { nullptr };

    DrawTool m_tool { DrawTool::Pen };
    QColor m_color { QStringLiteral("#145DFF") };
    qreal m_widthPt { 3.0 };
    bool m_active { false };
    bool m_drawing { false };
    bool m_changed { false };
    int m_activePage { -1 };
    EditSession::DrawStroke m_activeStroke;
    QList<EditSession::DrawStroke> m_beforeGesture;
};
