#include "Theme.hpp"

#include <QFile>
#include <QDebug>

namespace Theme {

QString loadStyleSheet()
{
    QFile file(QStringLiteral(":/theme/Style.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Theme: could not open :/theme/Style.qss";
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace Theme
