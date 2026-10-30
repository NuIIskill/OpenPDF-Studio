#pragma once

#include "ui/view/PageOverlay.hpp"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRectF>

class DocumentView;
class RedactionMark;

/// The areas marked for redaction: a change of the document like any other, carried out when it is saved.
class RedactionLayer : public QObject, public PageOverlay
{
    Q_OBJECT

public:
    struct Region {
        int    page { -1 };
        QRectF rect;
    };

    RedactionLayer(DocumentView *view, QObject *parent = nullptr);
    ~RedactionLayer() override;

    bool hasPending() const { return !m_regions.isEmpty(); }

    void setDocument(const QString &path) override;
    void setActiveTool(const QString &toolId) override;
    void relayout() override;
    bool writeTo(const WriteGrant &staging) override;
    QString stateKey() const override;
    QByteArray state() const override;
    void restoreState(const QByteArray &state) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Drag { None, Create, Move, Resize };

    bool handleMouse(QEvent *event);
    bool handleKey(QEvent *event);
    void updateCursor(const QPoint &pos);
    int  regionAt(const QPoint &pos) const;
    int  handleAt(const QPoint &pos) const;
    QRect  toCanvas(const Region &region) const;
    QRectF toPage(int page, const QRectF &canvasRect) const;
    QRectF pageRect(int page) const;
    bool shown() const;

    void bindPanel();
    void refreshPanel();
    void rebuildMarks();
    void select(int index);
    void remove(int index);
    void activate(int index);

    DocumentView                 *m_view { nullptr };
    QString                       m_documentPath;
    QList<Region>                 m_regions;
    QList<QPointer<RedactionMark>> m_marks;
    int                           m_selected { -1 };
    bool                          m_active { false };

    Drag    m_drag { Drag::None };
    int     m_corner { -1 };
    QPointF m_pressPoint;
    QRectF  m_pressRect;
};
