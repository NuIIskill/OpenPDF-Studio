#pragma once

#include "engine/document/WriteGrant.hpp"

#include <QByteArray>
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
                                   const WriteGrant &workingCopy, bool *wroteWorkingCopy)
    {
        Q_UNUSED(path) Q_UNUSED(page) Q_UNUSED(canvasPosition) Q_UNUSED(workingCopy)
        Q_UNUSED(wroteWorkingCopy)
        return false;
    }

    virtual bool writeTo(const WriteGrant &staging) { Q_UNUSED(staging) return true; }

    virtual QString stateKey() const { return {}; }

    virtual QByteArray state() const { return {}; }

    virtual void restoreState(const QByteArray &state) { Q_UNUSED(state) }

    void setChangeReporter(std::function<void(const QString &title, int page)> reporter)
    { m_reportChange = std::move(reporter); }

protected:
    void reportChange(const QString &title, int page) const
    { if (m_reportChange) m_reportChange(title, page); }

private:
    std::function<void(const QString &title, int page)> m_reportChange;
};

/// Layers contributed by optional parts of the program.
namespace PageOverlays {

using Factory = std::function<PageOverlay *(PageCanvas *canvas, QObject *parent)>;

void add(Factory factory);

QList<PageOverlay *> createAll(PageCanvas *canvas, QObject *parent);

}
