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
#include "ui/edit/InlineEditor.hpp"

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

    if (qapp.arguments().size() >= 3
            && qapp.arguments().at(1) == QLatin1String("--shot-license-notice")) {
        if (qapp.arguments().size() >= 4)
            Theme::apply(qapp.arguments().at(3));
        QWidget host;
        LicenseNotice::showExpiryReminderIfDue(&host, {});
        qapp.processEvents();
        for (QWidget *w : qapp.topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(w))
                return box->grab().save(qapp.arguments().at(2)) ? 0 : 3;
        return 3;
    }

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

    if (qapp.arguments().size() >= 4
            && qapp.arguments().at(1) == QLatin1String("--organize-save")) {
        PdfOrganizerDialog dlg(qapp.arguments().at(2));
        return dlg.writeForTest(qapp.arguments().at(3)) ? 0 : 3;
    }

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

        if (!fieldName.isEmpty()) {
            EditSession fieldSession;
            EditSession::Edit fieldEdit;
            fieldEdit.page      = page - 1;
            fieldEdit.formField = fieldName;
            fieldEdit.newText   = replacement;
            fieldSession.addEdit(fieldEdit);
            return backend->saveWithEdits(args.at(3), fieldSession) ? 0 : 3;
        }

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

        for (int a = 4; a < args.size(); ++a) {
            if (!args.at(a).startsWith(QLatin1String("edit="))) continue;
            const QStringList xy = args.at(a).mid(5).split(u',');
            if (xy.size() != 2) break;
            DocumentView *dv = win->findChild<DocumentView *>();
            if (!dv) break;

            Q_EMIT win->rightSidebar()->modeSelected(QStringLiteral("edit"));
            QApplication::processEvents();

            Q_EMIT win->leftSidebar()->toolSelected(QStringLiteral("text"));
            QApplication::processEvents();
            for (int z = 4; z < args.size(); ++z) {
                if (!args.at(z).startsWith(QLatin1String("preseite="))) continue;
                dv->goToPage(args.at(z).mid(9).toInt() - 1);
                QApplication::processEvents();
                QEventLoop ps;
                QTimer::singleShot(900, &ps, &QEventLoop::quit);
                ps.exec();
                break;
            }
            for (int z = 4; z < args.size(); ++z) {
                if (!args.at(z).startsWith(QLatin1String("prezoom="))) continue;
                dv->setZoom(args.at(z).mid(8).toInt());
                QApplication::processEvents();
                QEventLoop zs;
                QTimer::singleShot(900, &zs, &QEventLoop::quit);
                zs.exec();
                break;
            }
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
                if (!args.at(r).startsWith(QLatin1String("resize="))) continue;

                QString spec = args.at(r).mid(7);
                QString griff = QStringLiteral("se");
                if (const int dp = spec.indexOf(u':'); dp > 0) {
                    griff = spec.left(dp);
                    spec  = spec.mid(dp + 1);
                }
                const QStringList d = spec.split(u',');
                if (d.size() != 2) break;
                auto *ed = dv->findChild<QTextEdit *>(QStringLiteral("InlineEditor"));
                QWidget *frame = ed ? ed->parentWidget() : nullptr;
                if (!frame) break;

                const int W = frame->width(), H = frame->height();
                const int L = 14, R = W - 15, T = 14, B = H - 15;
                QPoint grab(R, B);
                if      (griff == QLatin1String("e"))  grab = QPoint(R, H / 2);
                else if (griff == QLatin1String("s"))  grab = QPoint(W / 2, B);
                else if (griff == QLatin1String("w"))  grab = QPoint(L, H / 2);
                else if (griff == QLatin1String("n"))  grab = QPoint(W / 2, T);
                else if (griff == QLatin1String("sw")) grab = QPoint(L, B);
                else if (griff == QLatin1String("ne")) grab = QPoint(R, T);
                const QPoint weg(d.at(0).toInt(), d.at(1).toInt());
                const QPoint g0 = frame->mapToGlobal(grab);
                const auto send = [&](QEvent::Type t, const QPoint &lokal,
                                      const QPoint &global) {
                    QMouseEvent me(t, QPointF(lokal), QPointF(global),
                                   Qt::LeftButton,
                                   t == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                   : Qt::LeftButton,
                                   Qt::NoModifier);
                    QApplication::sendEvent(frame, &me);
                    QApplication::processEvents();
                };

                {
                    QTextStream out(stdout);
                    out << "rahmen im fenster="
                        << frame->mapTo(win, QPoint(0,0)).x() << ","
                        << frame->mapTo(win, QPoint(0,0)).y() << " "
                        << frame->width() << "x" << frame->height()
                        << "  fenster=" << win->width() << "x" << win->height() << "\n";
                    const QPoint ecken[8] = {
                        {10, 10}, {W/2, 10}, {W-11, 10},
                        {10, H/2}, {W-11, H/2},
                        {10, H-11}, {W/2, H-11}, {W-11, H-11} };
                    const char *namen[8] = {"nw","n","ne","w","e","sw","s","se"};
                    for (int k = 0; k < 8; ++k) {
                        QWidget *u = frame->childAt(ecken[k]);
                        out << "  " << namen[k] << " lokal=" << ecken[k].x() << ","
                            << ecken[k].y() << " kind="
                            << (u ? u->metaObject()->className() : "(keins, also Rahmen)")
                            << "\n";
                    }
                }

                if (args.contains(QStringLiteral("echt"))) {
                    QWidget *ziel = QApplication::widgetAt(g0);
                    {
                        QTextStream out(stdout);
                        out << "zustellung an=";
                        for (QWidget *w = ziel; w; w = w->parentWidget())
                            out << w->metaObject()->className()
                                << "(" << (w->objectName().isEmpty()
                                               ? QStringLiteral("-") : w->objectName())
                                << ") < ";
                        out << "\n";
                        if (ziel) {
                            out << "   geometrie=" << ziel->geometry().x() << ","
                                << ziel->geometry().y() << " "
                                << ziel->width() << "x" << ziel->height()
                                << "  mausdurchlaessig="
                                << ziel->testAttribute(Qt::WA_TransparentForMouseEvents)
                                << "\n";
                        }
                    }
                    if (ziel) {
                        const auto echt = [&](QEvent::Type t, const QPoint &global) {
                            QMouseEvent me(t, QPointF(ziel->mapFromGlobal(global)),
                                           QPointF(global), Qt::LeftButton,
                                           t == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                           : Qt::LeftButton,
                                           Qt::NoModifier);
                            QApplication::sendEvent(ziel, &me);
                            QApplication::processEvents();
                        };
                        echt(QEvent::MouseButtonPress, g0);
                        echt(QEvent::MouseMove, g0 + weg / 2);
                        echt(QEvent::MouseMove, g0 + weg);
                        echt(QEvent::MouseButtonRelease, g0 + weg);
                        QEventLoop s2;
                        QTimer::singleShot(800, &s2, &QEventLoop::quit);
                        s2.exec();
                        break;
                    }
                }
                send(QEvent::MouseButtonPress, grab, g0);
                send(QEvent::MouseMove, grab + weg / 2, g0 + weg / 2);
                send(QEvent::MouseMove, grab + weg, g0 + weg);
                send(QEvent::MouseButtonRelease, grab + weg, g0 + weg);
                QEventLoop settle;
                QTimer::singleShot(800, &settle, &QEventLoop::quit);
                settle.exec();
                break;
            }

            for (int r = 4; r < args.size(); ++r) {
                if (!args.at(r).startsWith(QLatin1String("move="))) continue;
                const QStringList d = args.at(r).mid(5).split(u',');
                if (d.size() != 2) break;
                auto *ed = dv->findChild<QTextEdit *>(QStringLiteral("InlineEditor"));
                QWidget *frame = ed ? ed->parentWidget() : nullptr;
                if (!frame) break;
                const QPoint grab(frame->width() * 3 / 8, 3);
                const QPoint weg(d.at(0).toInt(), d.at(1).toInt());
                const QPoint g0 = frame->mapToGlobal(grab);
                const auto send = [&](QEvent::Type t, const QPoint &lokal,
                                      const QPoint &global) {
                    QMouseEvent me(t, QPointF(lokal), QPointF(global),
                                   Qt::LeftButton,
                                   t == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                   : Qt::LeftButton,
                                   Qt::NoModifier);
                    QApplication::sendEvent(frame, &me);
                    QApplication::processEvents();
                };
                send(QEvent::MouseButtonPress, grab, g0);
                send(QEvent::MouseMove, grab + weg / 2, g0 + weg / 2);
                send(QEvent::MouseMove, grab + weg, g0 + weg);
                send(QEvent::MouseButtonRelease, grab + weg, g0 + weg);
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

            for (int r = 4; r < args.size(); ++r) {
                const QString o = args.at(r);
                if (o.startsWith(QLatin1String("color="))) {
                    dv->setEditorTextColor(QColor(o.mid(6)));
                } else if (o.startsWith(QLatin1String("size="))) {
                    dv->setEditorFontSize(o.mid(5).toInt());
                } else if (o.startsWith(QLatin1String("font="))) {
                    const QStringList teile = o.mid(5).split(u',');
                    dv->setEditorFontFamily(teile.at(0));
                    dv->setEditorBold(teile.contains(QLatin1String("bold")));
                    dv->setEditorItalic(teile.contains(QLatin1String("italic")));
                    dv->setEditorUnderline(teile.contains(QLatin1String("underline")));
                } else {
                    continue;
                }
                QApplication::processEvents();
            }

            const auto zeigeBounds = [&](const char *wann) {
                const QRectF b = dv->editBounds();
                const QRectF f = dv->editFrameRect();
                QTextStream(stdout) << "bounds " << wann << "="
                    << QStringLiteral("%1,%2,%3,%4").arg(b.x(), 0, 'f', 2)
                           .arg(b.y(), 0, 'f', 2).arg(b.width(), 0, 'f', 2)
                           .arg(b.height(), 0, 'f', 2)
                    << "  schrift=" << QString::number(dv->editFontSizePt(), 'f', 2)
                    << "  rahmen=" << QStringLiteral("%1,%2,%3,%4").arg(f.x(), 0, 'f', 2)
                           .arg(f.y(), 0, 'f', 2).arg(f.width(), 0, 'f', 2)
                           .arg(f.height(), 0, 'f', 2)
                    << "\n";
            };
            if (args.contains(QStringLiteral("bounds"))) zeigeBounds("danach");

            if (args.contains(QStringLiteral("selectall"))) {
                if (auto *ed = dv->findChild<QTextEdit *>(
                        QStringLiteral("InlineEditor"))) {
                    ed->selectAll();
                    QApplication::processEvents();
                }
            }

            if (args.contains(QStringLiteral("nocaret"))
                    || args.contains(QStringLiteral("caret"))) {
                if (auto *ed = dv->findChild<InlineEditor *>())
                    ed->setCaretVisible(args.contains(QStringLiteral("caret")));
                QApplication::processEvents();
            }

            QPoint boxCenter(-1, -1);
            if (auto *ed = dv->findChild<QTextEdit *>(QStringLiteral("InlineEditor"))) {
                if (QWidget *fr = ed->parentWidget(); fr && fr->isVisible())
                    boxCenter = dv->viewport()->mapFromGlobal(
                        fr->mapToGlobal(fr->rect().center()));
            }

            if (args.contains(QStringLiteral("escape"))) {
                QTextStream(stdout) << "seite vorher=" << dv->currentPage() << "\n";
                if (auto *ed = dv->findChild<QTextEdit *>(
                        QStringLiteral("InlineEditor"))) {
                    QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QApplication::sendEvent(ed, &key);
                }
                QEventLoop zu;
                QTimer::singleShot(800, &zu, &QEventLoop::quit);
                zu.exec();
                QTextStream(stdout) << "seite nachher=" << dv->currentPage() << "\n";
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
                    QPoint again = boxCenter;
                    if (args.at(r2).mid(5) != QLatin1String("box")) {
                        const QStringList xy2 = args.at(r2).mid(5).split(u',');
                        if (xy2.size() != 2) break;
                        again = QPoint(xy2.at(0).toInt(), xy2.at(1).toInt());
                    }
                    if (again.x() < 0) break;
                    QWidget *vp2 = dv->viewport();
                    for (int k = 0; k < 2; ++k) {
                        for (const QEvent::Type type : { QEvent::MouseButtonPress,
                                                         QEvent::MouseButtonRelease }) {
                            QMouseEvent me(type, QPointF(again),
                                           vp2->mapToGlobal(QPointF(again)),
                                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                            QApplication::sendEvent(vp2, &me);
                        }
                        QEventLoop settle;
                        QTimer::singleShot(600, &settle, &QEventLoop::quit);
                        settle.exec();
                    }
                    QEventLoop opened;
                    QTimer::singleShot(1200, &opened, &QEventLoop::quit);
                    opened.exec();
                    for (int r3 = 4; r3 < args.size(); ++r3) {
                        if (!args.at(r3).startsWith(QLatin1String("then-type="))) continue;
                        if (auto *ed2 = dv->findChild<QTextEdit *>(
                                QStringLiteral("InlineEditor"))) {
                            ed2->selectAll();
                            ed2->insertPlainText(args.at(r3).mid(10));
                            QApplication::processEvents();
                        }
                        break;
                    }
                    if (!args.contains(QStringLiteral("then-open"))) {
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
                    }
                    QTextStream(stdout) << "undo2=" << dv->undoStack()->count() << "\n";
                    break;
                }
            }

            for (int r4 = 4; r4 < args.size(); ++r4) {
                if (!args.at(r4).startsWith(QLatin1String("save="))) continue;
                QTextStream(stdout) << "gespeichert="
                                    << (dv->saveToFile(args.at(r4).mid(5)) ? "ja" : "nein")
                                    << "\n";
                break;
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
                        if (args.contains(QStringLiteral("bounds")))
                            zeigeBounds(qPrintable(QString::number(pct)));
                    }
                break;
            }
            break;
        }

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

                if (QPushButton *fallback = box->defaultButton()) fallback->click();
                else if (!buttons.isEmpty())                      buttons.first()->click();
            });
            poll->start(100);
            break;
        }

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

                DocumentView *dv = win->findChild<DocumentView *>();
                if (!dv) continue;
                QMimeData mime;
                mime.setUrls({ QUrl::fromLocalFile(args.at(a).mid(5)) });
                const QPointF at(dv->viewport()->width() / 2.0,
                                 dv->viewport()->height() / 2.0);
                QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &mime,
                                      Qt::LeftButton, Qt::NoModifier);

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

        if (args.contains(QLatin1String("tools"))) {
            win->leftSidebar()->openCustomizePopup();
            QEventLoop cardSettle;
            QTimer::singleShot(600, &cardSettle, &QEventLoop::quit);
            cardSettle.exec();
        }

        const bool ok = win->grab().save(args.at(2));
        return ok ? 0 : 3;
    }

    App app;
    app.startup();

    if (args.size() > 1 && QFileInfo::exists(args.at(1)))
        app.mainWindow()->openPath(args.at(1));

    return qapp.exec();
}
