#include "app/App.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QTranslator>
#include <cstdlib>

int main(int argc, char *argv[])
{
#ifdef DEFAULT_QPA_PLATFORM
    qputenv("QT_QPA_PLATFORM", DEFAULT_QPA_PLATFORM);
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

    // ── Application controller ────────────────────────────────────────────
    App app;
    app.startup();

    return qapp.exec();
}
