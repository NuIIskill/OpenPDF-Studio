#pragma once

#include "engine/forms/FormSpec.hpp"

#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRectF>

#include <optional>

class DocumentView;
class FormFrame;
class FormLayer;

/// The form design tool: fields are drawn on the page, picked, moved, resized and removed.
class FormDesigner : public QObject
{
    Q_OBJECT

public:
    FormDesigner(FormLayer *layer, DocumentView *view, QObject *parent = nullptr);
    ~FormDesigner() override;

    void setActive(bool active);
    void rebuild();
    void relayout();

    QString selected() const { return m_field; }
    void select(const QString &field, int index = 0);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class FormFrame;
    enum class Drag { None, Swallow, Create, Drop, Move, Resize };

    bool shown() const;
    bool canvasMouse(QEvent *event, const QPoint &pos);
    void framePress(int frame, const QPoint &canvasPos);
    void dragTo(const QPoint &canvasPos);
    void release();
    void dragKind(FormSpec::Kind kind, const QPoint &global);
    void dropKind(FormSpec::Kind kind, const QPoint &global);
    void add(FormSpec::Kind kind, QRectF bounds);
    QRectF pageRect(int page) const;
    QRect toCanvas(int page, const QRectF &bounds) const;
    void setPlacing(std::optional<FormSpec::Kind> kind);
    void bindPanel();
    void refreshPanel();

    FormLayer                  *m_layer;
    DocumentView               *m_view;
    QList<QPointer<FormFrame>>  m_frames;
    QPointer<FormFrame>         m_sketch;
    bool                        m_active { false };
    std::optional<FormSpec::Kind> m_placing;
    FormSpec::Kind              m_lastKind { FormSpec::Kind::Text };
    QString                     m_field;
    int                         m_index { 0 };

    Drag    m_drag { Drag::None };
    int     m_frame { -1 };
    int     m_page { -1 };
    int     m_corner { -1 };
    QPoint  m_pressPos;
    QRectF  m_pressBounds;
    QRectF  m_dragBounds;
};
