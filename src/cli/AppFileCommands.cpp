#include "cli/Commands.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

namespace Cli {

int removeAppFileCommand(const QStringList &args)
{
    // Run elevated by Settings > Add-on Packages: a helper program next to
    // this one, never the program itself or anything elsewhere.
    const QString name = args.at(2);
    const QFileInfo self(QCoreApplication::applicationFilePath());
    if (name.contains(u'/') || name.contains(u'\\') || name.startsWith(u'.')
            || !name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
            || name.compare(self.fileName(), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("Uninstall.exe"), Qt::CaseInsensitive) == 0)
        return 2;
    const QString path = self.absolutePath() + u'/' + name;
    return !QFileInfo::exists(path) || QFile::remove(path) ? 0 : 3;
}

}
