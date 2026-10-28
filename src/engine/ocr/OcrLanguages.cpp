#include "engine/ocr/OcrLanguages.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

namespace {

QString bundledDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/share/tessdata");
}

}

const QStringList &OcrLanguages::offered()
{
    // The languages of the user interface, in Tesseract's codes.
    static const QStringList codes {
        QStringLiteral("deu"), QStringLiteral("eng"), QStringLiteral("fra"),
        QStringLiteral("spa"), QStringLiteral("ita"), QStringLiteral("nld"),
        QStringLiteral("pol"), QStringLiteral("por"), QStringLiteral("rus"),
        QStringLiteral("jpn"), QStringLiteral("kor"), QStringLiteral("chi_sim"),
    };
    return codes;
}

QString OcrLanguages::dataDir()
{
#ifdef Q_OS_WIN
    // Once the user changed the languages, their own folder holds all of them.
    const QString own = userDir();
    return QFileInfo(own).isDir() ? own : bundledDir();
#else
    QStringList candidates;
    const QString prefix = qEnvironmentVariable("TESSDATA_PREFIX");
    if (!prefix.isEmpty())
        candidates << prefix << prefix + QStringLiteral("/tessdata");
    // Fedora, Debian and Ubuntu (Tesseract 5 and 4), Arch, own builds.
    candidates << QStringLiteral("/usr/share/tesseract/tessdata")
               << QStringLiteral("/usr/share/tesseract-ocr/5/tessdata")
               << QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata")
               << QStringLiteral("/usr/share/tessdata")
               << QStringLiteral("/usr/local/share/tessdata");
    for (const QString &dir : std::as_const(candidates))
        if (!QDir(dir).entryList({ QStringLiteral("*.traineddata") }, QDir::Files).isEmpty())
            return dir;
    return {};
#endif
}

QStringList OcrLanguages::installed()
{
    const QString dir = dataDir();
    if (dir.isEmpty()) return {};
    QStringList found;
    for (const QString &code : offered())
        if (QFileInfo::exists(dir + u'/' + code + QStringLiteral(".traineddata")))
            found << code;
    return found;
}

QString OcrLanguages::userDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
         + QStringLiteral("/tessdata");
}

bool OcrLanguages::seedUserDir()
{
    // Tesseract reads one folder, so the bundled languages move along into it.
    const QString own = userDir();
    if (QFileInfo(own).isDir()) return true;
    if (!QDir().mkpath(own)) return false;
    const QDir bundled(bundledDir());
    for (const QString &name : bundled.entryList({ QStringLiteral("*.traineddata") }, QDir::Files))
        QFile::copy(bundled.filePath(name), own + u'/' + name);
    return true;
}

QUrl OcrLanguages::source(const QString &code)
{
    // The fast models of Tesseract 4.1.0, the same ones the packages carry.
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/tesseract-ocr/"
                               "tessdata_fast/4.1.0/%1.traineddata").arg(code));
}

QByteArray OcrLanguages::sha256(const QString &code)
{
    static const QHash<QString, QByteArray> sums {
        { QStringLiteral("deu"),     "19d219bbb6672c869d20a9636c6816a81eb9a71796cb93ebe0cb1530e2cdb22d" },
        { QStringLiteral("eng"),     "7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2" },
        { QStringLiteral("fra"),     "ced037562e8c80c13122dece28dd477d399af80911a28791a66a63ac1e3445ca" },
        { QStringLiteral("spa"),     "6f2e04d02774a18f01bed44b1111f2cd7f3ba7ac9dc4373cd3f898a40ea6b464" },
        { QStringLiteral("ita"),     "b8f89e1e785118dac4d51ae042c029a64edb5c3ee42ef73027a6d412748d8827" },
        { QStringLiteral("nld"),     "ced0e5e046a84c908a6aa7accbef9a232c4a5d9a8276691b81c6ee64d02963f6" },
        { QStringLiteral("pol"),     "c4476cdbc0e33d898d32345122b7be1cbf85ace15f920f06c7714756e1ef79b2" },
        { QStringLiteral("por"),     "c4932b937207a9514b7514d518b931a99938c02a28a5a5a553f8599ed58b7deb" },
        { QStringLiteral("rus"),     "e16e5e036cce1d9ec2b00063cf8b54472625b9e14d893a169e2b0dedeb4df225" },
        { QStringLiteral("jpn"),     "1f5de9236d2e85f5fdf4b3c500f2d4926f8d9449f28f5394472d9e8d83b91b4d" },
        { QStringLiteral("kor"),     "6b85e11d9bbf07863b97b3523b1b112844c43e713df8b66418a081fd1060b3b2" },
        { QStringLiteral("chi_sim"), "a5fcb6f0db1e1d6d8522f39db4e848f05984669172e584e8d76b6b3141e1f730" },
    };
    return sums.value(code);
}

bool OcrLanguages::remove(const QString &code)
{
    if (!seedUserDir()) return false;
    const QString file = userDir() + u'/' + code + QStringLiteral(".traineddata");
    return !QFileInfo::exists(file) || QFile::remove(file);
}
