#include "app/App.hpp"
#include "ui/MainWindow.hpp"
#include "ui/DocumentView.hpp"
#include "engine/edit/DocxExporter.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QTranslator>
#include <cstdlib>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <string>

// On Windows, the cross-compiled fontconfig DLL has Linux paths hardcoded
// as its system config location.  Setting FONTCONFIG_FILE before QApplication
// is created (which loads Qt6Gui.dll and triggers fontconfig init) prevents
// fontconfig from crashing when it can't find its config at the Linux path.
// We point it to a fonts.conf deployed alongside the exe (in etc/fonts/).
static void initFontconfigWindows()
{
    char buf[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return;
    std::string exePath(buf);
    const auto lastSep = exePath.find_last_of("\\/");
    const std::string exeDir = (lastSep != std::string::npos)
                                   ? exePath.substr(0, lastSep)
                                   : ".";
    // Try the bundled fonts.conf first; fall back to letting fontconfig use
    // the built-in config (which still has WINDOWSFONTDIR so fonts are found).
    const std::string fcConf = exeDir + "\\etc\\fonts\\fonts.conf";
    SetEnvironmentVariableA("FONTCONFIG_FILE", fcConf.c_str());
    // The conf.avail dir sits next to fonts.conf
    const std::string fcPath = exeDir + "\\etc\\fonts";
    SetEnvironmentVariableA("FONTCONFIG_PATH", fcPath.c_str());
    // Suppress the Linux-only mmap optimization (not reliable on Windows)
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

    // Opt in to high-DPI scaling (default in Qt 6, but explicit for clarity)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication qapp(argc, argv);
    qapp.setApplicationName(QStringLiteral("OpenPDF Studio"));
    qapp.setApplicationDisplayName(QStringLiteral("OpenPDF Studio"));
    qapp.setOrganizationName(QStringLiteral("OpenPDF"));
    qapp.setOrganizationDomain(QStringLiteral("openpdf.io"));
    qapp.setApplicationVersion(QStringLiteral(APP_VERSION));

    // ── Fusion style + theme (palette + QSS) ─────────────────────────────
    QApplication::setStyle(QStringLiteral("Fusion"));
    {
        QSettings startupSettings(QStringLiteral("OpenPDF"), QStringLiteral("OpenPDFStudio"));
        const QString savedTheme = startupSettings.value(
            QStringLiteral("appearance/theme"), QStringLiteral("system")).toString();
        Theme::apply(savedTheme);
    }

    // ── Application font ──────────────────────────────────────────────────
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

    // ── Locale / translations ─────────────────────────────────────────────
    // Use the locale-aware overload: tries openpdf_de_DE.qm → openpdf_de.qm → openpdf.qm
    QTranslator translator;
    if (translator.load(QLocale::system(),
                        QStringLiteral("openpdf"),
                        QStringLiteral("_"),
                        QStringLiteral(":/i18n"))) {
        qapp.installTranslator(&translator);
    }

    // ── App icon (blue rounded square + white "O") ────────────────────────
    {
        QIcon appIcon;
        for (const int sz : { 16, 24, 32, 48, 64, 128, 256 }) {
            QPixmap px(sz, sz);
            px.fill(Qt::transparent);
            QPainter p(&px);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#2563EB")));
            const qreal r = sz * 0.22;
            p.drawRoundedRect(QRectF(0, 0, sz, sz), r, r);
            p.setPen(Qt::white);
            QFont f;
            f.setBold(true);
            f.setPixelSize(qRound(sz * 0.56));
            f.setFamily(QStringLiteral("Inter"));
            p.setFont(f);
            p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, QStringLiteral("O"));
            p.end();
            appIcon.addPixmap(px);
        }
        qapp.setWindowIcon(appIcon);
    }

    // Headless regression/export entry point. It uses the same content path as
    // the UI: OpenPDFStudio --export-docx input.pdf output.docx
    const QStringList args = qapp.arguments();
    if (args.size() == 4 && args.at(1) == QLatin1String("--export-docx")) {
        DocumentView view;
        if (!view.openFile(args.at(2))) return 2;
        const bool ok = DocxExporter::exportToDocx(
            args.at(3), view.allPageContent(), QFileInfo(args.at(2)).completeBaseName());
        return ok ? 0 : 3;
    }

    // ── Application controller ────────────────────────────────────────────
    App app;
    app.startup();

    // Open a PDF passed on the command line (file association / debugging).
    if (args.size() > 1 && QFileInfo::exists(args.at(1)))
        app.mainWindow()->openPath(args.at(1));

    return qapp.exec();
}
