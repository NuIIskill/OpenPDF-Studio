#pragma once

#include <QList>
#include <QString>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Side panels that belong to a tool.
///
/// The window carries one already: the text panel that appears while the text
/// tool is chosen. This list is the same for tools that do not come from the
/// Core. As with PageOverlay, the module registers with the Core, not the
/// other way round.
namespace ToolPanels {

struct Panel {
    QString toolId;   ///< id from the tool catalogue, e.g. "video"
    int     width { 300 };
    /// Called once while the window is built.
    std::function<QWidget *(QWidget *parent)> create;
};

void add(Panel panel);

const QList<Panel> &all();

} // namespace ToolPanels
