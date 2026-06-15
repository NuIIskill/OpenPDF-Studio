#include "app/App.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <cstdlib>

int main(int argc, char *argv[])
{
    // ── Wayland-only: must be set before QApplication is constructed ──────
    qputenv("QT_QPA_PLATFORM", "wayland");

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

    // ── Locale / translations (stub) ─────────────────────────────────────
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = QStringLiteral("OpenPDFStudio_") + locale;
        if (translator.load(QStringLiteral(":/i18n/") + baseName)) {
            qapp.installTranslator(&translator);
            break;
        }
    }

    // ── Application controller ────────────────────────────────────────────
    App app;
    app.startup();

    return qapp.exec();
}
