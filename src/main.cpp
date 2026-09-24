#include "App.hpp"
#include "app/AppConfig.hpp"
#include "app/AppSettings.hpp"
#include "cli/Cli.hpp"
#include "ui/MainWindow.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QSettings>
#include <QTranslator>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <string>

static void initFontconfigWindows()
{
    char buf[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return;
    std::string exePath(buf);
    const auto lastSep = exePath.find_last_of("\\/");
    const std::string exeDir = (lastSep != std::string::npos)
                                   ? exePath.substr(0, lastSep)
                                   : ".";

    const std::string fcConf = exeDir + "\\etc\\fonts\\fonts.conf";
    SetEnvironmentVariableA("FONTCONFIG_FILE", fcConf.c_str());

    const std::string fcPath = exeDir + "\\etc\\fonts";
    SetEnvironmentVariableA("FONTCONFIG_PATH", fcPath.c_str());

    SetEnvironmentVariableA("FONTCONFIG_USE_MMAP", "false");
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    initFontconfigWindows();
#endif

#ifdef DEFAULT_QPA_PLATFORM
    if (qgetenv("QT_QPA_PLATFORM").isEmpty())
        qputenv("QT_QPA_PLATFORM", "wayland");
#endif

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication qapp(argc, argv);
    qapp.setApplicationName(QStringLiteral("OpenPDF Studio"));
    qapp.setApplicationDisplayName(QStringLiteral("OpenPDF Studio"));
    qapp.setOrganizationName(QStringLiteral("OpenPDF"));
    qapp.setOrganizationDomain(QStringLiteral("openpdf.io"));
    qapp.setApplicationVersion(QStringLiteral(APP_VERSION));

    QApplication::setStyle(QStringLiteral("Fusion"));
    {
        const QString savedTheme = AppConfig::store().value(
            QStringLiteral("appearance/theme"), QStringLiteral("system")).toString();
        Theme::apply(savedTheme);
    }

    {
        const QStringList candidates = { "Inter", "Noto Sans", "Segoe UI", "Helvetica Neue" };
        for (const QString &f : candidates) {
            if (QFontDatabase::families().contains(f)) {
                QFont font(f);
                font.setPointSize(10);
                font.setHintingPreference(QFont::PreferNoHinting);
                QApplication::setFont(font);
                break;
            }
        }
    }

    QTranslator translator;
    {
        const QString lang = AppConfig::store()
                                 .value(QStringLiteral("appearance/language"),
                                        AppSettings::systemDefaultLanguage())
                                 .toString();
        if (lang != QLatin1String("en")
                && translator.load(QStringLiteral(":/i18n/openpdf_%1.qm").arg(lang)))
            qapp.installTranslator(&translator);
    }

    {
        QIcon appIcon;
        for (const int sz : { 16, 24, 32, 48, 64, 128, 256 })
            appIcon.addPixmap(Theme::renderSvg(QStringLiteral("openpdf-studio"),
                                               Qt::white, sz));
        qapp.setWindowIcon(appIcon);
    }

    if (const auto rc = Cli::run(qapp.arguments()))
        return *rc;

    const QStringList args = qapp.arguments();

    App app;
    app.startup();

    if (args.size() > 1 && QFileInfo::exists(args.at(1)))
        app.mainWindow()->openPath(args.at(1));

    app.mainWindow()->restoreSession();

    return qapp.exec();
}
