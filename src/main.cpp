#include "App.hpp"
#include "ui/MainWindow.hpp"
#include "ui/DocumentView.hpp"
#include "ui/export/ExportDialog.hpp"
#include "ui/organizer/PdfOrganizerDialog.hpp"
#include "ui/history/HistoryDialog.hpp"
#include "engine/edit/DocxExporter.hpp"
#include "engine/edit/PdfExporter.hpp"
#include "ui/PresentationWindow.hpp"
#include <QTextStream>
#include "app/PdfPwStore.hpp"
#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#endif
#include "ui/panels/LeftSidebar.hpp"
#include "ui/panels/RightSidebar.hpp"
#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "app/AppConfig.hpp"
#include "drm/LicenseNotice.hpp"
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
#include <QMessageBox>
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
        const QString savedTheme = AppConfig::store().value(
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

    // The expiry notice as the user gets to see it, without waiting 30 days:
    //   OPENPDF_USAGE=business OpenPDFStudio --shot-license-notice out.png [dark|light]
    // Exit 3 means no notice was due — evaluation still running, personal use,
    // or a key on record. The flag declares nothing of its own: it reports the
    // state it finds, so it can be used to check that state.
    if (qapp.arguments().size() >= 3
            && qapp.arguments().at(1) == QLatin1String("--shot-license-notice")) {
        if (qapp.arguments().size() >= 4)
            Theme::apply(qapp.arguments().at(3));
        QWidget host;   // parent only, never shown
        LicenseNotice::showExpiryReminderIfDue(&host, {});
        qapp.processEvents();
        for (QWidget *w : qapp.topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(w))
                return box->grab().save(qapp.arguments().at(2)) ? 0 : 3;
        return 3;
    }

    // Settings dialog on any of its pages, named by the English nav label:
    //   OpenPDFStudio --shot-settings out.png ["License Key"] [dark|light]
    // The License Key page exists only where the state says business use —
    // OPENPDF_USAGE=business in front of it is how that is arranged for a shot.
    if (qapp.arguments().size() >= 3
            && qapp.arguments().at(1) == QLatin1String("--shot-settings")) {
        if (qapp.arguments().size() >= 5)
            Theme::apply(qapp.arguments().at(4));
        AppSettings settings;
        SettingsPanel dlg(&settings);
        if (qapp.arguments().size() >= 4)
            dlg.selectPageForTest(qapp.arguments().at(3));
        dlg.show();
        qapp.processEvents();
        const bool ok = dlg.grab().save(qapp.arguments().at(2));
        return ok ? 0 : 3;
    }

    // Runs the organizer's save path on an unchanged page list, so a refactor
    // of it can be checked against the bytes it produced before:
    //   OpenPDFStudio --organize-save in.pdf out.pdf
    if (qapp.arguments().size() >= 4
            && qapp.arguments().at(1) == QLatin1String("--organize-save")) {
        PdfOrganizerDialog dlg(qapp.arguments().at(2));
        return dlg.writeForTest(qapp.arguments().at(3)) ? 0 : 3;
    }

    // Renders the page organizer with a document loaded:
    //   OpenPDFStudio --shot-organizer out.png in.pdf [dark]
    if (qapp.arguments().size() >= 4
            && qapp.arguments().at(1) == QLatin1String("--shot-organizer")) {
        if (qapp.arguments().size() >= 5)
            Theme::apply(qapp.arguments().at(4));
        PdfOrganizerDialog dlg(qapp.arguments().at(3));
        dlg.resize(1200, 800);
        dlg.show();
        QEventLoop settle;
        QTimer::singleShot(1500, &settle, &QEventLoop::quit);
        settle.exec();
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

    // One PNG per page, straight off the renderer — no window, no toolbar, no
    // theme. That is what makes it comparable across platforms and across PDF
    // backends, which --shot-window is not: that one grabs the whole UI.
    //   OpenPDFStudio --export-images in.pdf out.png [pages=1,3-4] [q=85] [srcpw=secret]
    // With more than one page the name gains a _page_N suffix, exactly as the
    // export dialog writes it.
    if (args.size() >= 4 && args.at(1) == QLatin1String("--export-images")) {
        QList<int> pages;
        int quality = 85;
        for (int a = 4; a < args.size(); ++a) {
            const QString o = args.at(a);
            if (o.startsWith(QLatin1String("srcpw="))) {
                PdfPwStore::set(args.at(2), o.mid(6));
                continue;
            }
            if (o.startsWith(QLatin1String("q="))) { quality = o.mid(2).toInt(); continue; }
            if (!o.startsWith(QLatin1String("pages="))) continue;
            for (const QString &part : o.mid(6).split(u',', Qt::SkipEmptyParts)) {
                const int dash = part.indexOf(u'-');
                const int from = dash < 0 ? part.toInt() : part.left(dash).toInt();
                const int to   = dash < 0 ? from : part.mid(dash + 1).toInt();
                for (int p = from; p <= to; ++p) pages.append(p - 1);
            }
        }
        DocumentView view;
        if (!view.openFile(args.at(2))) return 2;
        return view.exportPagesToImages(args.at(3), quality, pages) ? 0 : 3;
    }

    // Text selection, straight off the backend and printed as text. Selection
    // is the one core feature the export entry points cannot reach, and it is
    // also the one whose logic differs most between backends — Qt has an API
    // for it, Poppler has to rebuild the reading order from a word list. This
    // is where the two get compared.
    //   OpenPDFStudio --select-text in.pdf [page=1] [from=x,y] [to=x,y] [srcpw=secret]
    // Without from/to the whole page is selected.
    if (args.size() >= 3 && args.at(1) == QLatin1String("--select-text")) {
#ifdef HAVE_PDF_RENDERING
        int page = 1;
        std::optional<QPointF> from, to;
        const auto parsePoint = [](const QString &v) -> std::optional<QPointF> {
            const QStringList xy = v.split(u',');
            if (xy.size() != 2) return std::nullopt;
            return QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
        };
        for (int a = 3; a < args.size(); ++a) {
            const QString o = args.at(a);
            if      (o.startsWith(QLatin1String("srcpw="))) PdfPwStore::set(args.at(2), o.mid(6));
            else if (o.startsWith(QLatin1String("page=")))  page = o.mid(5).toInt();
            else if (o.startsWith(QLatin1String("from=")))  from = parsePoint(o.mid(5));
            else if (o.startsWith(QLatin1String("to=")))    to   = parsePoint(o.mid(3));
        }

        DocumentSource src;
        if (!src.open(args.at(2), nullptr)) return 2;
        auto *backend = src.backend();
        if (!backend) return 3;
        const PdfBackend::Selection sel = backend->selectPage(page - 1, from, to);
        QTextStream out(stdout);
        out << "backend=" << backend->name() << "\n";
        out << "rects=" << sel.rects.size() << "\n";
        for (const QRectF &r : sel.rects) {
            out << QStringLiteral("rect %1,%2 %3x%4\n")
                       .arg(r.x(), 0, 'f', 1).arg(r.y(), 0, 'f', 1)
                       .arg(r.width(), 0, 'f', 1).arg(r.height(), 0, 'f', 1);
        }
        out << "text<<\n" << sel.text << "\n>>text\n";
        return sel.text.isEmpty() && sel.rects.isEmpty() ? 1 : 0;
#else
        return 3;
#endif
    }

    // Renders the whole window, with a document open, to a PNG. Everything the
    // dialog shots cannot reach — toolbar, format bar, sidebars, the pages
    // themselves — is only checkable this way without a display:
    //   OpenPDFStudio --shot-window out.png in.pdf [dark]
    //
    // The wait is not decoration: pages are rendered by a timer after the
    // scroll area has laid them out, so grabbing immediately yields blanks.
    // Replaces the text line at a point and saves — the whole edit-and-save
    // path without a window. Saving is the one thing the other entry points
    // never touch, and the piece that differs most between backends.
    //   OpenPDFStudio --apply-edit in.pdf out.pdf at=x,y text=Ersetzung [page=1] [srcpw=…]
    //   OpenPDFStudio --apply-edit in.pdf out.pdf field=Feldname text=Wert
    // The coordinates are PDF points with the origin top-left, the same as
    // --select-text reports.
    if (args.size() >= 4 && args.at(1) == QLatin1String("--apply-edit")) {
#ifdef HAVE_PDF_RENDERING
        int     page = 1;
        QPointF at;
        QString replacement;
        QString fieldName;
        for (int a = 4; a < args.size(); ++a) {
            const QString o = args.at(a);
            if      (o.startsWith(QLatin1String("srcpw="))) PdfPwStore::set(args.at(2), o.mid(6));
            else if (o.startsWith(QLatin1String("page=")))  page = o.mid(5).toInt();
            else if (o.startsWith(QLatin1String("text=")))  replacement = o.mid(5);
            else if (o.startsWith(QLatin1String("field="))) fieldName = o.mid(6);
            else if (o.startsWith(QLatin1String("at="))) {
                const QStringList xy = o.mid(3).split(u',');
                if (xy.size() == 2) at = QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
            }
        }

        DocumentSource src;
        if (!src.open(args.at(2), nullptr)) return 2;
        auto *backend = src.backend();
        if (!backend) return 3;

        QTextStream out(stdout);
        out << "backend=" << backend->name() << "\n";

        // Formularfeld: kein Textobjekt, sondern der Wert eines Widgets.
        if (!fieldName.isEmpty()) {
            EditSession fieldSession;
            EditSession::Edit fieldEdit;
            fieldEdit.page      = page - 1;
            fieldEdit.formField = fieldName;
            fieldEdit.newText   = replacement;
            fieldSession.addEdit(fieldEdit);
            return backend->saveWithEdits(args.at(3), fieldSession) ? 0 : 3;
        }

        // Genau der Weg, den der Inline-Editor geht: Zeile am Punkt suchen,
        // ihre Glyphenkästen als Löschflächen nehmen, Ersatztext eintragen.
        const TextBlock block = backend->textAt(page - 1, at);
        if (!block.isValid()) {
            QTextStream(stdout) << "kein Text an dieser Stelle\n";
            return 4;
        }

        EditSession session;
        EditSession::Edit edit;
        edit.page         = page - 1;
        edit.pdfBounds    = block.pdfBounds;
        edit.originalText = block.text;
        edit.newText      = replacement;
        edit.eraseRects   = backend->glyphRects(page - 1, block.pdfBounds);
        session.addEdit(edit);

        out << "ersetzt<<\n" << block.text << "\n>>ersetzt\n";
        return backend->saveWithEdits(args.at(3), session) ? 0 : 3;
#else
        return 3;
#endif
    }

    // Presentation mode as a PNG. It opens the document a second time, through
    // its own backend, and nothing else in the app reaches that code — which is
    // why it could stay broken on the Poppler build for so long without anyone
    // noticing (it rendered a black screen).
    //   OpenPDFStudio --shot-presentation out.png in.pdf [page=N]
    if (args.size() >= 4 && args.at(1) == QLatin1String("--shot-presentation")) {
        int page = 1;
        for (int a = 4; a < args.size(); ++a)
            if (args.at(a).startsWith(QLatin1String("page=")))
                page = args.at(a).mid(5).toInt();

        auto *pw = new PresentationWindow(args.at(3), page - 1);
        pw->resize(1280, 900);
        QEventLoop settle;
        QTimer::singleShot(1500, &settle, &QEventLoop::quit);
        settle.exec();
        const bool ok = pw->grab().save(args.at(2), "PNG");
        pw->close();
        return ok ? 0 : 3;
    }

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

        // Optional: open the "Customize Tools" card, which only exists while
        // its button is pressed and so never shows up in a plain shot.
        //   OpenPDFStudio --shot-window out.png in.pdf tools
        if (args.contains(QLatin1String("tools"))) {
            win->leftSidebar()->openCustomizePopup();
            QEventLoop cardSettle;
            QTimer::singleShot(600, &cardSettle, &QEventLoop::quit);
            cardSettle.exec();
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
