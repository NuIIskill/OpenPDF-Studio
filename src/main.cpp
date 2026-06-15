#include "app/App.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QLocale>
#include <QPalette>
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

    // ── Force light theme regardless of system dark mode ─────────────────
    // Fusion style respects custom palettes; without it Wayland dark mode
    // overrides every widget background via the platform theme.
    QApplication::setStyle(QStringLiteral("Fusion"));
    {
        QPalette lp;
        lp.setColor(QPalette::Window,          QColor(0xF3, 0xF4, 0xF6));
        lp.setColor(QPalette::WindowText,      QColor(0x11, 0x18, 0x27));
        lp.setColor(QPalette::Base,            Qt::white);
        lp.setColor(QPalette::AlternateBase,   QColor(0xF9, 0xFA, 0xFB));
        lp.setColor(QPalette::Text,            QColor(0x11, 0x18, 0x27));
        lp.setColor(QPalette::BrightText,      Qt::white);
        lp.setColor(QPalette::Button,          Qt::white);
        lp.setColor(QPalette::ButtonText,      QColor(0x11, 0x18, 0x27));
        lp.setColor(QPalette::Highlight,       QColor(0x25, 0x63, 0xEB));
        lp.setColor(QPalette::HighlightedText, Qt::white);
        lp.setColor(QPalette::Light,           Qt::white);
        lp.setColor(QPalette::Midlight,        QColor(0xF9, 0xFA, 0xFB));
        lp.setColor(QPalette::Mid,             QColor(0xE5, 0xE7, 0xEB));
        lp.setColor(QPalette::Dark,            QColor(0xD1, 0xD5, 0xDB));
        lp.setColor(QPalette::Shadow,          QColor(0x9C, 0xA3, 0xAF));
        lp.setColor(QPalette::ToolTipBase,     Qt::white);
        lp.setColor(QPalette::ToolTipText,     QColor(0x11, 0x18, 0x27));
        lp.setColor(QPalette::PlaceholderText, QColor(0x9C, 0xA3, 0xAF));
        QApplication::setPalette(lp);
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

    // ── Stylesheet ────────────────────────────────────────────────────────
    const QString qss = Theme::loadStyleSheet();
    if (!qss.isEmpty())
        qapp.setStyleSheet(qss);

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
