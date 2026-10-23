#pragma once

#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Files dropped from the Windows shell on a window whose OLE drop target is missing.
namespace ShellFileDrop {

void enable(QWidget *window, std::function<void(const QStringList &)> open);

}
