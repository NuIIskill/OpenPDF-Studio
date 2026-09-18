#pragma once

#include <QList>
#include <QPoint>
#include <QString>
#include <QtGlobal>

#include <functional>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

class PageCanvas;

/// Something lying over the pages that the view has to carry along.
class PageOverlay
{
public:
    virtual ~PageOverlay() = default;

    virtual void setDocument(const QString &path) = 0;

    virtual void setActiveTool(const QString &toolId) = 0;

    virtual void relayout() = 0;

    virtual bool acceptsDroppedFile(const QString &path) const
    {
        Q_UNUSED(path)
        return false;
    }

    virtual bool handleDroppedFile(const QString &path, int page,
                                   const QPoint &canvasPosition,
                                   QString *newDocument)
    {
        Q_UNUSED(path) Q_UNUSED(page) Q_UNUSED(canvasPosition) Q_UNUSED(newDocument)
        return false;
    }

    virtual bool writeTo(const QString &stagingPath) { Q_UNUSED(stagingPath) return true; }
};

/// Layers contributed by optional parts of the program.
namespace PageOverlays {

using Factory = std::function<PageOverlay *(PageCanvas *canvas, QObject *parent)>;

void add(Factory factory);

QList<PageOverlay *> createAll(PageCanvas *canvas, QObject *parent);

}
