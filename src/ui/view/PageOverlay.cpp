#include "ui/view/PageOverlay.hpp"

namespace {

// A function-local static: factories are registered from static initializers,
// whose order is not fixed.
QList<PageOverlays::Factory> &factories()
{
    static QList<PageOverlays::Factory> list;
    return list;
}

} // namespace

void PageOverlays::add(Factory factory)
{
    if (factory) factories().append(std::move(factory));
}

QList<PageOverlay *> PageOverlays::createAll(PageCanvas *canvas, QObject *parent)
{
    QList<PageOverlay *> out;
    if (!canvas) return out;
    for (const Factory &make : factories())
        if (PageOverlay *overlay = make(canvas, parent))
            out.append(overlay);
    return out;
}
