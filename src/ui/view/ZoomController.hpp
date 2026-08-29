#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QVBoxLayout;
class QWheelEvent;
QT_END_NAMESPACE

class PageCanvas;
class PageLayoutEngine;

class ZoomController : public QObject
{
    Q_OBJECT

public:
    ZoomController(QScrollArea *area, PageCanvas *canvas, QVBoxLayout *canvasLayout,
                   PageLayoutEngine *engine, QObject *parent = nullptr);

    int  zoom() const { return m_zoom; }

    void setZoom(int percent);

    void applyZoom(int percent, const QPoint &viewportAnchor);

    void setSettings(int step, bool ctrlWheel, bool toPointer,
                     const QString &wheelAction);

    bool handleWheel(QWheelEvent *e);

    void updateScrollRange();

Q_SIGNALS:

    void zoomChanged(int percent);

    void viewportChanged();

    void zoomApplied(int percent);

private:
    QScrollArea      *m_area   { nullptr };
    PageCanvas       *m_canvas { nullptr };
    QVBoxLayout      *m_layout { nullptr };
    PageLayoutEngine *m_engine { nullptr };

    int     m_zoom              { 100 };
    int     m_zoomStep          { 10 };
    bool    m_ctrlWheelEnabled  { true };
    bool    m_zoomToPointer     { true };
    QString m_wheelAction       { QStringLiteral("scroll") };
};
