#pragma once

#include <QString>

/// Copies Qt's log messages into a file next to config.ini while debug logging is on.
namespace LogFile {

void configure(bool enabled, const QString &level);
QString path();

}
