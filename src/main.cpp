#include "App.hpp"
#include "ui/MainWindow.hpp"
#include "ui/DocumentView.hpp"
#include "ui/dialogs/ExportDialog.hpp"
#include "ui/dialogs/HistoryDialog.hpp"
#include "engine/edit/DocxExporter.hpp"
#include "engine/edit/PdfExporter.hpp"
#include "app/PdfPwStore.hpp"
#include "ui/panels/LeftSidebar.hpp"
#include "ui/panels/RightSidebar.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QTimer>
#include <QEventLoop>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QLocale>
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
    // Rendered from the same openpdf-studio.svg that produces the Linux PNG
    // and the Windows .ico, so all three stay in sync.  This used to be drawn
    // with QPainter using a bold "Inter" glyph, which silently fell back to a
    // different font — and so a different logo — on any machine without Inter.
    {
        QIcon appIcon;
        for (const int sz : { 16, 24, 32, 48, 64, 128, 256 })
            appIcon.addPixmap(Theme::renderSvg(QStringLiteral("openpdf-studio"),
                                               Qt::white, sz));
        qapp.setWindowIcon(appIcon);
    }

    // Renders the export dialog to a PNG so its appearance can be checked
    // without a display: OpenPDFStudio --shot-export-dialog out.png [word|image]
    if (qapp.arguments().size() >= 3
            && qapp.arguments().at(1) == QLatin1String("--shot-export-dialog")) {
        ExportDialog dlg(QStringLiteral("/tmp/demo.pdf"), 4, 0);
        if (qapp.arguments().size() >= 4)
            dlg.selectFormatForTest(qapp.arguments().at(3));
        dlg.show();
        qapp.processEvents();
        const bool ok = dlg.grab().save(qapp.arguments().at(2));
        return ok ? 0 : 3;
    }

    // Same for the history dialog, with a made-up log so the timeline has
    // something to show: OpenPDFStudio --shot-history-dialog out.png [dark]
    if (qapp.arguments().size() >= 3
            && qapp.arguments().at(1) == QLatin1String("--shot-history-dialog")) {
        using Kind = DocumentHistory::Kind;
        if (qapp.arguments().size() >= 4
                && qapp.arguments().at(3) == QLatin1String("dark"))
            Theme::apply(QStringLiteral("dark"));
        DocumentHistory history;
        history.record({ Kind::Opened, -1, 4, 0, QStringLiteral("Vertrag.pdf") }, 0);
        history.record({ Kind::PageDeleted, 3 }, 0);
        history.record({ Kind::PageRotated, 0, 1, 90 }, 0);
        history.record({ Kind::ImageInserted, 1 }, 0);
        history.record({ Kind::TextEdited, 0 }, 1);
        HistoryDialog dlg(&history, QStringLiteral("Vertrag.pdf"));
        dlg.setUndoRedoAvailable(true, false);
        dlg.show();
        qapp.processEvents();
        const bool ok = dlg.grab().save(qapp.arguments().at(2));
        return ok ? 0 : 3;
    }

    // Headless regression/export entry point for the PDF target, exercising the
    // same option path as the dialog:
    //   OpenPDFStudio --export-pdf in.pdf out.pdf [pages=1,3-4] [nocomments]
    //                 [noforms] [nofonts] [nocompress] [q=60] [pw=secret]
    const QStringList args = qapp.arguments();
    if (args.size() >= 4 && args.at(1) == QLatin1String("--export-pdf")) {
        PdfExportOptions opt;
        for (int a = 4; a < args.size(); ++a) {
            const QString o = args.at(a);
            if      (o == QLatin1String("nocomments")) opt.includeComments = false;
            else if (o == QLatin1String("noforms"))    opt.keepForms       = false;
            else if (o == QLatin1String("nofonts"))    opt.embedFonts      = false;
            else if (o == QLatin1String("nocompress")) opt.compressImages  = false;
            else if (o.startsWith(QLatin1String("q=")))
                opt.imageQuality = o.mid(2).toInt();
            else if (o.startsWith(QLatin1String("pw=")))
                opt.userPassword = o.mid(3);
            else if (o.startsWith(QLatin1String("srcpw=")))
                PdfPwStore::set(args.at(2), o.mid(6));
            else if (o.startsWith(QLatin1String("pages="))) {
                for (const QString &part : o.mid(6).split(u',', Qt::SkipEmptyParts)) {
                    const int dash = part.indexOf(u'-');
                    const int from = dash < 0 ? part.toInt() : part.left(dash).toInt();
                    const int to   = dash < 0 ? from : part.mid(dash + 1).toInt();
                    for (int p = from; p <= to; ++p) opt.pages.append(p - 1);
                }
            }
        }
        return exportPdf(args.at(2), args.at(3), opt) ? 0 : 3;
    }

    // Headless regression/export entry point. It uses the same content path as
    // the UI: OpenPDFStudio --export-docx input.pdf output.docx
    // An optional trailing pages=1,3-4 selects a subset, as the dialog does.
    if (args.size() >= 4 && args.at(1) == QLatin1String("--export-docx")) {
        QList<int> pages;
        DocxExportOptions docxOpt;
        for (int a = 4; a < args.size(); ++a) {
            if (args.at(a).startsWith(QLatin1String("srcpw="))) {
                PdfPwStore::set(args.at(2), args.at(a).mid(6));
                continue;
            }
            if (args.at(a) == QLatin1String("nocompress")) { docxOpt.compressImages = false; continue; }
            if (args.at(a).startsWith(QLatin1String("q="))) { docxOpt.imageQuality = args.at(a).mid(2).toInt(); continue; }
            if (!args.at(a).startsWith(QLatin1String("pages="))) continue;
            for (const QString &part : args.at(a).mid(6).split(u',', Qt::SkipEmptyParts)) {
                const int dash = part.indexOf(u'-');
                const int from = dash < 0 ? part.toInt() : part.left(dash).toInt();
                const int to   = dash < 0 ? from : part.mid(dash + 1).toInt();
                for (int p = from; p <= to; ++p) pages.append(p - 1);
            }
        }
        DocumentView view;
        if (!view.openFile(args.at(2))) return 2;
        const bool ok = DocxExporter::exportToDocx(
            args.at(3), view.allPageContent(pages),
            QFileInfo(args.at(2)).completeBaseName(), docxOpt);
        return ok ? 0 : 3;
    }

    // Renders the whole window, with a document open, to a PNG. Everything the
    // dialog shots cannot reach — toolbar, format bar, sidebars, the pages
    // themselves — is only checkable this way without a display:
    //   OpenPDFStudio --shot-window out.png in.pdf [dark]
    //
    // The wait is not decoration: pages are rendered by a timer after the
    // scroll area has laid them out, so grabbing immediately yields blanks.
    if (args.size() >= 4 && args.at(1) == QLatin1String("--shot-window")) {
        if (args.size() >= 5 && args.at(4) == QLatin1String("dark"))
            Theme::apply(QStringLiteral("dark"));
        App shotApp;
        shotApp.startup();
        MainWindow *win = shotApp.mainWindow();
        win->resize(1400, 900);
        win->openPath(args.at(3));
        QEventLoop settle;
        QTimer::singleShot(2500, &settle, &QEventLoop::quit);
        settle.exec();

        // Optional: enter edit mode and click a page position, so the editor
        // frame and the format bar — neither of which exists otherwise — end
        // up in the shot. The click is posted at the viewport so it travels
        // the real event filter instead of calling the handler directly:
        // that is the whole path an edit opens through.
        //   OpenPDFStudio --shot-window out.png in.pdf edit=<x>,<y>
        for (int a = 4; a < args.size(); ++a) {
            if (!args.at(a).startsWith(QLatin1String("edit="))) continue;
            const QStringList xy = args.at(a).mid(5).split(u',');
            if (xy.size() != 2) break;
            DocumentView *dv = win->findChild<DocumentView *>();
            if (!dv) break;
            // Through the sidebar signal, not DocumentView::setEditMode():
            // the window owns the edit state and it is what shows the format
            // bar and switches the sidebar. Setting it on the view alone puts
            // the two out of step — which is exactly what this shot is for.
            Q_EMIT win->rightSidebar()->modeSelected(QStringLiteral("edit"));
            QApplication::processEvents();
            // Order matters: the tool handler puts up a modal "enable edit
            // mode?" box when edit mode is still off, and nothing would ever
            // answer it here. With the mode already on it goes straight
            // through — and only then does the format bar appear.
            Q_EMIT win->leftSidebar()->toolSelected(QStringLiteral("text"));
            QApplication::processEvents();
            const QPoint at(xy.at(0).toInt(), xy.at(1).toInt());
            QWidget *vp = dv->viewport();
            for (const QEvent::Type type : { QEvent::MouseButtonPress,
                                             QEvent::MouseButtonRelease }) {
                QMouseEvent me(type, QPointF(at), vp->mapToGlobal(QPointF(at)),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &me);
            }
            QEventLoop editSettle;
            QTimer::singleShot(1500, &editSettle, &QEventLoop::quit);
            editSettle.exec();
            break;
        }

        const bool ok = win->grab().save(args.at(2));
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
