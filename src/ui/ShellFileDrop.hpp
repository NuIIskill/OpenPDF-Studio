#pragma once

#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Files dropped from the Windows shell as WM_DROPFILES, the only way into an elevated window.
namespace ShellFileDrop {

void enable(QWidget *window, std::function<void(const QStringList &)> open);

}
