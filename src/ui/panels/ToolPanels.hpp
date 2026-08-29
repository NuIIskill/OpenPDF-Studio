#pragma once

#include <QList>
#include <QString>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Side panels that belong to a tool.
namespace ToolPanels {

struct Panel {
    QString toolId;
    int     width { 300 };

    std::function<QWidget *(QWidget *parent)> create;
};

void add(Panel panel);

const QList<Panel> &all();

}
