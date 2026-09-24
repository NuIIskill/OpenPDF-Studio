#pragma once

#include <QStringList>
#include <optional>

/// Command-line modes that run instead of the interactive application.
namespace Cli {

std::optional<int> run(const QStringList &args);

}
