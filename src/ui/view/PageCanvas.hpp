#pragma once

#include <QPoint>
#include <utility>

QT_BEGIN_NAMESPACE
class QLabel;
class QWidget;
QT_END_NAMESPACE

// What the view controllers need from the page canvas — nothing more.
// DocumentView implements this; the controllers never see DocumentView, so a
// controller can be built and tested without dragging the whole view along.
class PageCanvas
{
public:
    virtual ~PageCanvas() = default;

    // Parent widget for overlays (highlights, frames, the editor).
    virtual QWidget *canvasWidget() const = 0;
    // Label of a page, or nullptr when it is not built / out of range.
    virtual QLabel  *pageLabel(int page) const = 0;
    // Pages in the document. May exceed pageLabelCount() while pages are built.
    virtual int      pageCount() const = 0;
    // Page labels that currently exist.
    virtual int      pageLabelCount() const = 0;
    // PDF points → screen pixels at the current zoom.
    virtual qreal    screenScale() const = 0;
    // Page + label under a canvas position; page is -1 when there is none.
    virtual std::pair<int, QLabel *> pageAtCanvasPos(const QPoint &canvasPos) const = 0;
};
