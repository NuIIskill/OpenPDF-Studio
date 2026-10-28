#pragma once

#include <QStringList>

#include <functional>

/// Runs this program once more with administrator rights, behind Windows' UAC prompt.
namespace Elevation {

enum class Result { Done, Failed, Declined };

void runSelf(const QStringList &arguments, std::function<void(Result)> done);

}
