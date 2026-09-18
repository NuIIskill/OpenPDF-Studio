#pragma once

#include <QPoint>
#include <utility>

QT_BEGIN_NAMESPACE
class QLabel;
class QWidget;
QT_END_NAMESPACE

/// Defines page-canvas operations required by view controllers.
class PageCanvas
{
public:
    virtual ~PageCanvas() = default;

    virtual QWidget *canvasWidget() const = 0;

    virtual QLabel  *pageLabel(int page) const = 0;

    virtual int      pageCount() const = 0;

    virtual int      pageLabelCount() const = 0;

    virtual qreal    screenScale() const = 0;

    virtual std::pair<int, QLabel *> pageAtCanvasPos(const QPoint &canvasPos) const = 0;
};
