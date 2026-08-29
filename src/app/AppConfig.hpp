#pragma once

#include <QString>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

namespace AppConfig {

QSettings &store();

QString path();

bool isPortable();

}
