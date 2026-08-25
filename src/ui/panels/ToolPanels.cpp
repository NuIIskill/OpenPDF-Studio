#include "ui/panels/ToolPanels.hpp"

namespace {

// A function-local static, like the other registers: filled from static
// initializers whose order is not fixed.
QList<ToolPanels::Panel> &registry()
{
    static QList<ToolPanels::Panel> list;
    return list;
}

} // namespace

void ToolPanels::add(Panel panel)
{
    if (panel.toolId.isEmpty() || !panel.create) return;
    registry().append(std::move(panel));
}

const QList<ToolPanels::Panel> &ToolPanels::all()
{
    return registry();
}
