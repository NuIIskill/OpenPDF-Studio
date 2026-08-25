#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <functional>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

class PageCanvas;

/// Something lying over the pages that the view has to carry along.
///
/// The view has such layers already (selection, images, hover); they live in
/// src/ and are addressed directly. This interface is for layers that may NOT
/// live in the Core, today the rich-media module's, which is licensed
/// differently. The Core therefore knows only these questions, not the type.
///
/// The layer attaches itself to the canvas widget it is built with. The view
/// only tells it what currently holds, never where a click went.
class PageOverlay
{
public:
    virtual ~PageOverlay() = default;

    /// A document was opened, changed or closed. An empty path means nothing
    /// is open any more.
    virtual void setDocument(const QString &path) = 0;

    /// The chosen tool, spelled as in the sidebar's catalogue.
    virtual void setActiveTool(const QString &toolId) = 0;

    /// Zoom or a relayout moved the pages.
    virtual void relayout() = 0;

    /// Would the layer take this file?
    ///
    /// Asked while the file is being dragged in, so before anything happens:
    /// the answer only decides whether the cursor shows a no-entry sign. No
    /// side effects and no questions here; handleDroppedFile() does the work.
    virtual bool acceptsDroppedFile(const QString &path) const
    {
        Q_UNUSED(path)
        return false;
    }

    /// A file was dropped on the view over page `page` (-1 when over none).
    /// True means the layer took it and the caller leaves it alone.
    ///
    /// If the layer sets `newDocument`, it wrote a new version of the document
    /// (an added page, say, which cannot be a session change). The view then
    /// opens it as a working copy, as with the page organizer.
    virtual bool handleDroppedFile(const QString &path, int page,
                                   QString *newDocument)
    {
        Q_UNUSED(path) Q_UNUSED(page) Q_UNUSED(newDocument)
        return false;
    }

    /// Saving: the backend has written `stagingPath`, nothing is swapped yet.
    /// Whatever the layer wants in the document and the backend cannot write,
    /// it writes now.
    ///
    /// On the overlay and not in a global list of passes: an overlay belongs to
    /// exactly one document view and needs no key to know which document is
    /// meant. A key would drift, because DocumentView::detachSourceFrom()
    /// swaps the content path in the middle of a save.
    ///
    /// False aborts the save and the staging file is discarded.
    virtual bool writeTo(const QString &stagingPath) { Q_UNUSED(stagingPath) return true; }
};

/// Layers contributed by optional parts of the program. A module registers a
/// factory at startup and every document view builds its own layer from it.
/// Without a factory nothing is created.
namespace PageOverlays {

/// `parent` owns the layer so it goes with the view.
using Factory = std::function<PageOverlay *(PageCanvas *canvas, QObject *parent)>;

void add(Factory factory);

/// One layer per registered factory, for `canvas`.
QList<PageOverlay *> createAll(PageCanvas *canvas, QObject *parent);

} // namespace PageOverlays
