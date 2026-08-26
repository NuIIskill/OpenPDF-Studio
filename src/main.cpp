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
#include <QAbstractButton>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QEventLoop>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QLocale>
#include <QPixmap>
#include <QMessageBox>
#include <QPushButton>
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
        EditSession::Edit blank;
        blank.page         = page - 1;
        blank.pdfBounds    = block.pdfBounds;
        blank.sourceRect   = block.pdfBounds;
        blank.originalText = block.text;
        blank.eraseRects   = backend->glyphRects(page - 1, block.pdfBounds);
        session.addEdit(blank);

        EditSession::Edit edit;
        edit.page         = page - 1;
        edit.pdfBounds    = block.pdfBounds;
        edit.sourceRect   = block.pdfBounds;
        edit.originalText = block.text;
        edit.newText      = replacement;
        session.addEdit(edit);

        out << QStringLiteral("block=%1,%2,%3,%4\n")
                   .arg(block.pdfBounds.x(), 0, 'f', 1)
                   .arg(block.pdfBounds.y(), 0, 'f', 1)
                   .arg(block.pdfBounds.width(), 0, 'f', 1)
                   .arg(block.pdfBounds.height(), 0, 'f', 1);
        out << "ersetzt<<\n" << block.text << "\n>>ersetzt\n";

        for (int a = 4; a < args.size(); ++a) {
            if (!args.at(a).startsWith(QLatin1String("preview="))) continue;
            const QImage shot = backend->renderPage(page - 1, 2.0, &session);
            if (shot.isNull() || !shot.save(args.at(a).mid(8), "PNG")) return 3;
            out << "vorschau=" << args.at(a).mid(8) << "\n";
            break;
        }
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


            for (int r = 4; r < args.size(); ++r) {
                if (!args.at(r).startsWith(QLatin1String("move="))) continue;
                const QStringList d = args.at(r).mid(5).split(u',');
                if (d.size() != 2) break;
                auto *ed = dv->findChild<QTextEdit *>(QStringLiteral("InlineEditor"));
                QWidget *frame = ed ? ed->parentWidget() : nullptr;
                if (!frame) break;
                const QPoint grab(frame->width() / 2, 3);
                const QPoint to = grab + QPoint(d.at(0).toInt(), d.at(1).toInt());
                const auto send = [&](QEvent::Type t, const QPoint &at) {
                    QMouseEvent me(t, QPointF(at), frame->mapToGlobal(QPointF(at)),
                                   Qt::LeftButton,
                                   t == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                   : Qt::LeftButton,
                                   Qt::NoModifier);
                    QApplication::sendEvent(frame, &me);
                    QApplication::processEvents();
                };
                send(QEvent::MouseButtonPress, grab);
                send(QEvent::MouseMove, grab + (to - grab) / 2);
                send(QEvent::MouseMove, to);
                send(QEvent::MouseButtonRelease, to);
                QEventLoop settle;
                QTimer::singleShot(800, &settle, &QEventLoop::quit);
                settle.exec();
                break;
            }

            for (int r = 4; r < args.size(); ++r) {
                if (!args.at(r).startsWith(QLatin1String("type="))) continue;
                if (auto *ed = dv->findChild<QTextEdit *>(
                        QStringLiteral("InlineEditor"))) {
                    ed->selectAll();
                    ed->insertPlainText(args.at(r).mid(5));
                    QApplication::processEvents();
                }
                break;
            }
            if (args.contains(QStringLiteral("commit"))) {
                QWidget *vp = dv->viewport();
                const QPoint away(vp->width() - 30, vp->height() - 30);
                for (const QEvent::Type type : { QEvent::MouseButtonPress,
                                                 QEvent::MouseButtonRelease }) {
                    QMouseEvent me(type, QPointF(away), vp->mapToGlobal(QPointF(away)),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &me);
                }
                QEventLoop done;
                QTimer::singleShot(1200, &done, &QEventLoop::quit);
                done.exec();
                QTextStream(stdout) << "undo=" << dv->undoStack()->count() << "\n";

                for (int r2 = 4; r2 < args.size(); ++r2) {
                    if (!args.at(r2).startsWith(QLatin1String("then="))) continue;
                    const QStringList xy2 = args.at(r2).mid(5).split(u',');
                    if (xy2.size() != 2) break;
                    const QPoint again(xy2.at(0).toInt(), xy2.at(1).toInt());
                    QWidget *vp2 = dv->viewport();
                    for (const QEvent::Type type : { QEvent::MouseButtonPress,
                                                     QEvent::MouseButtonRelease }) {
                        QMouseEvent me(type, QPointF(again),
                                       vp2->mapToGlobal(QPointF(again)),
                                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(vp2, &me);
                    }
                    QEventLoop opened;
                    QTimer::singleShot(1200, &opened, &QEventLoop::quit);
                    opened.exec();
                    const QPoint off(vp2->width() - 30, vp2->height() - 30);
                    for (const QEvent::Type type : { QEvent::MouseButtonPress,
                                                     QEvent::MouseButtonRelease }) {
                        QMouseEvent me(type, QPointF(off), vp2->mapToGlobal(QPointF(off)),
                                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(vp2, &me);
                    }
                    QEventLoop closed;
                    QTimer::singleShot(1200, &closed, &QEventLoop::quit);
                    closed.exec();
                    QTextStream(stdout) << "undo2=" << dv->undoStack()->count() << "\n";
                    break;
                }
            }

            for (int r = 4; r < args.size(); ++r) {
                if (!args.at(r).startsWith(QLatin1String("size="))) continue;
                for (const QString &step : args.at(r).mid(5).split(u',')) {
                    const QStringList wh = step.split(u'x');
                    if (wh.size() != 2) continue;
                    win->resize(wh.at(0).toInt(), wh.at(1).toInt());
                    QApplication::processEvents();
                    QEventLoop sizeSettle;
                    QTimer::singleShot(700, &sizeSettle, &QEventLoop::quit);
                    sizeSettle.exec();
                }
                break;
            }

            for (int z = 4; z < args.size(); ++z) {
                if (!args.at(z).startsWith(QLatin1String("zoom="))) continue;
                for (const QString &step : args.at(z).mid(5).split(u','))
                    if (const int pct = step.toInt(); pct > 0) {
                        dv->setZoom(pct);
                        QApplication::processEvents();
                        QEventLoop zoomSettle;
                        QTimer::singleShot(700, &zoomSettle, &QEventLoop::quit);
                        zoomSettle.exec();
                    }
                break;
            }
            break;
        }

        // Optional: pick a tool by its catalogue id, edit mode included where
        // the tool needs it — otherwise the handler puts up a modal question
        // that nothing here would answer.
        //   OpenPDFStudio --shot-window out.png in.pdf tool=video
        for (int a = 4; a < args.size(); ++a) {
            if (!args.at(a).startsWith(QLatin1String("tool="))) continue;
            const QString toolId = args.at(a).mid(5);
            Q_EMIT win->rightSidebar()->modeSelected(QStringLiteral("edit"));
            QApplication::processEvents();
            Q_EMIT win->leftSidebar()->toolSelected(toolId);
            QApplication::processEvents();
            QEventLoop toolSettle;
            QTimer::singleShot(600, &toolSettle, &QEventLoop::quit);
            toolSettle.exec();
            break;
        }

        // Optional: drag a rectangle on the page canvas, the way a tool that
        // frames an area is actually used. Coordinates are canvas pixels.
        //   OpenPDFStudio --shot-window out.png in.pdf tool=video drag=100,80,400,250
        for (int a = 4; a < args.size(); ++a) {
            if (!args.at(a).startsWith(QLatin1String("drag="))) continue;
            const QStringList xy = args.at(a).mid(5).split(u',');
            if (xy.size() != 4) break;
            DocumentView *dv = win->findChild<DocumentView *>();
            if (!dv) break;
            QWidget *canvas = dv->canvasWidget();
            if (!canvas) break;
            const QPoint from(xy.at(0).toInt(), xy.at(1).toInt());
            const QPoint to(xy.at(2).toInt(), xy.at(3).toInt());
            const struct { QEvent::Type type; QPoint at; } steps[] = {
                { QEvent::MouseButtonPress,   from },
                { QEvent::MouseMove,          QPoint((from.x() + to.x()) / 2,
                                                     (from.y() + to.y()) / 2) },
                { QEvent::MouseMove,          to },
                { QEvent::MouseButtonRelease, to },
            };
            for (const auto &step : steps) {
                QMouseEvent me(step.type, QPointF(step.at),
                               canvas->mapToGlobal(QPointF(step.at)),
                               step.type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                               Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &me);
                QApplication::processEvents();
            }
            QEventLoop dragSettle;
            QTimer::singleShot(800, &dragSettle, &QEventLoop::quit);
            dragSettle.exec();
            break;
        }

        // Optional: fill a named text field and press a named button, so a
        // flow that runs through a panel can be exercised without a display.
        // Both by objectName, so this works for any panel.
        //   OpenPDFStudio --shot-window out.png in.pdf set=Feld=Wert press=Knopf
        for (int a = 4; a < args.size(); ++a) {
            if (args.at(a).startsWith(QLatin1String("set="))) {
                const QString assignment = args.at(a).mid(4);
                const int split = assignment.indexOf(u'=');
                if (split <= 0) continue;
                const QString name  = assignment.left(split);
                const QString value = assignment.mid(split + 1);
                if (auto *field = win->findChild<QLineEdit *>(name))
                    field->setText(value);
                else
                    qWarning() << "[shot] no field named" << name;
                QApplication::processEvents();
            } else if (args.at(a).startsWith(QLatin1String("press="))) {
                const QString name = args.at(a).mid(6);
                if (auto *button = win->findChild<QAbstractButton *>(name))
                    button->click();
                else
                    qWarning() << "[shot] no button named" << name;
                QApplication::processEvents();
            } else {
                continue;
            }
            QEventLoop settleStep;
            QTimer::singleShot(400, &settleStep, &QEventLoop::quit);
            settleStep.exec();
        }

        // Optional: answer modal questions by themselves, so a run without a
        // person at the keyboard does not stop at the first one.
        //   OpenPDFStudio --shot-window out.png in.pdf autoconfirm=Play once
        // Without a text the default button is pressed.
        for (int a = 4; a < args.size(); ++a) {
            if (!args.at(a).startsWith(QLatin1String("autoconfirm"))) continue;
            const QString wanted = args.at(a).section(u'=', 1);
            auto *poll = new QTimer(win);
            QObject::connect(poll, &QTimer::timeout, win, [wanted]() {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box) return;
                if (wanted.isEmpty()) {
                    if (QPushButton *fallback = box->defaultButton()) fallback->click();
                    return;
                }
                const QList<QAbstractButton *> buttons = box->buttons();
                for (QAbstractButton *button : buttons)
                    if (button->text().remove(u'&') == wanted) { button->click(); return; }
                // No button by that name: this is a different question, so
                // answer it with its default and keep the run going.
                if (QPushButton *fallback = box->defaultButton()) fallback->click();
                else if (!buttons.isEmpty())                      buttons.first()->click();
            });
            poll->start(100);
            break;
        }

        // Optional: click at a canvas position, and send a key to whatever has
        // the focus afterwards. Unlike drag=, the click is delivered to the
        // widget actually under that point — an overlay child, if one is there.
        //   OpenPDFStudio --shot-window out.png in.pdf click=320,480 key=Delete
        for (int a = 4; a < args.size(); ++a) {
            if (args.at(a).startsWith(QLatin1String("click="))) {
                const QStringList xy = args.at(a).mid(6).split(u',');
                if (xy.size() != 2) continue;
                DocumentView *dv = win->findChild<DocumentView *>();
                QWidget *canvas = dv ? dv->canvasWidget() : nullptr;
                if (!canvas) continue;
                const QPoint at(xy.at(0).toInt(), xy.at(1).toInt());
                QWidget *target = canvas->childAt(at);
                const QPoint local = target ? target->mapFrom(canvas, at) : at;
                if (!target) target = canvas;
                for (const QEvent::Type type : { QEvent::MouseButtonPress,
                                                 QEvent::MouseButtonRelease }) {
                    QMouseEvent me(type, QPointF(local), target->mapToGlobal(QPointF(local)),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &me);
                }
            } else if (args.at(a).startsWith(QLatin1String("drop="))) {
                // Drag a file onto the view without a pointing device.
                DocumentView *dv = win->findChild<DocumentView *>();
                if (!dv) continue;
                QMimeData mime;
                mime.setUrls({ QUrl::fromLocalFile(args.at(a).mid(5)) });
                const QPointF at(dv->viewport()->width() / 2.0,
                                 dv->viewport()->height() / 2.0);
                QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &mime,
                                      Qt::LeftButton, Qt::NoModifier);
                // Through QObject: QScrollArea makes event() protected while
                // QObject::event is public and virtual.
                // To the VIEWPORT, not the view: a QAbstractScrollArea passes
                // its acceptDrops on to the viewport, and from there the events
                // reach the view's handlers through its filter. Sent to the
                // view directly, the class swallows them.
                QWidget *vp = dv->viewport();
                QApplication::sendEvent(vp, &enter);
                if (!enter.isAccepted()) {
                    qWarning() << "[shot] drag refused:" << args.at(a).mid(5);
                    continue;
                }
                QDragMoveEvent move(at.toPoint(), Qt::CopyAction, &mime,
                                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &move);
                QDropEvent drop(at, Qt::CopyAction, &mime,
                                Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
                QApplication::sendEvent(vp, &drop);
                QApplication::processEvents();
                QEventLoop dropSettle;
                QTimer::singleShot(2000, &dropSettle, &QEventLoop::quit);
                dropSettle.exec();
                continue;
            } else if (args.at(a) == QLatin1String("view=grid")) {
                if (DocumentView *dv = win->findChild<DocumentView *>())
                    dv->setViewMode(DocumentView::ViewMode::Grid);
                QApplication::processEvents();
                continue;
            } else if (args.at(a).startsWith(QLatin1String("scroll="))) {
                if (DocumentView *dv = win->findChild<DocumentView *>())
                    dv->verticalScrollBar()->setValue(
                        dv->verticalScrollBar()->value() + args.at(a).mid(7).toInt());
                QApplication::processEvents();
                continue;
            } else if (args.at(a).startsWith(QLatin1String("wait="))) {
                QEventLoop pause;
                QTimer::singleShot(args.at(a).mid(5).toInt(), &pause, &QEventLoop::quit);
                pause.exec();
                continue;
            } else if (args.at(a).startsWith(QLatin1String("key="))) {
                const QKeySequence sequence(args.at(a).mid(4));
                if (sequence.isEmpty()) continue;
                QWidget *target = QApplication::focusWidget();
                if (!target) target = win;
                const QKeyCombination combination = sequence[0];
                for (const QEvent::Type type : { QEvent::KeyPress, QEvent::KeyRelease }) {
                    QKeyEvent ke(type, combination.key(), combination.keyboardModifiers());
                    QApplication::sendEvent(target, &ke);
                }
            } else {
                continue;
            }
            QApplication::processEvents();
            QEventLoop inputSettle;
            QTimer::singleShot(400, &inputSettle, &QEventLoop::quit);
            inputSettle.exec();
        }

        // Optional: save the document, the same call the Save button makes.
        //   OpenPDFStudio --shot-window out.png in.pdf save=/tmp/ergebnis.pdf
        for (int a = 4; a < args.size(); ++a) {
            if (!args.at(a).startsWith(QLatin1String("save="))) continue;
            DocumentView *dv = win->findChild<DocumentView *>();
            if (!dv) break;
            const bool saved = dv->saveToFile(args.at(a).mid(5));
            qWarning() << "[shot] saved:" << saved << args.at(a).mid(5);
            QEventLoop saveSettle;
            QTimer::singleShot(1200, &saveSettle, &QEventLoop::quit);
            saveSettle.exec();
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
