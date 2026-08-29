#include "ui/panels/ToolPanels.hpp"

namespace {

QList<ToolPanels::Panel> &registry()
{
    static QList<ToolPanels::Panel> list;
    return list;
}

}

void ToolPanels::add(Panel panel)
{
    if (panel.toolId.isEmpty() || !panel.create) return;
    registry().append(std::move(panel));
}

const QList<ToolPanels::Panel> &ToolPanels::all()
{
    return registry();
}
