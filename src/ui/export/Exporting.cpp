#include "ui/export/Exporting.hpp"

#include "ui/DocumentView.hpp"
#include "ui/SaveIndicator.hpp"
#include "ui/export/ExportCopy.hpp"
#include "app/SafeWrite.hpp"
#include "engine/export/DocxExporter.hpp"
#include "engine/export/PdfExporter.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <functional>

namespace {

constexpr int MinBusyMs = 1000;

/// Holds back closing the window while a long export runs and remembers that it was asked for.
class CloseGuard : public QObject
{
public:
    explicit CloseGuard(QWidget *window) : m_window(window) { m_window->installEventFilter(this); }
    ~CloseGuard() override { m_window->removeEventFilter(this); }

    bool requested() const { return m_requested.load(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_window || event->type() != QEvent::Close) return false;
        m_requested.store(true);
        event->ignore();
        return true;
    }

private:
    QWidget          *m_window;
    std::atomic<bool> m_requested { false };
};

bool copyFile(const QString &source, const QString &target)
{
    const QString staging = SafeWrite::stagingPath(target);
    if (staging.isEmpty()) return false;
    QFile::remove(staging);
    if (!QFile::copy(source, staging)) {
        SafeWrite::discard(staging);
        return false;
    }
    return SafeWrite::commit(staging, target);
}

void runInBackground(const std::function<void()> &work)
{
    QEventLoop loop;
    QThread *thread = QThread::create(work);
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    thread->wait();
    delete thread;
}

bool writePdf(const QString &source, int pageCount, const ExportRequest &request,
              QString *failure)
{
    const QString shownName = QFileInfo(request.path).fileName();
    PdfExportOptions opt;
    opt.pages           = request.pages;
    opt.includeComments = request.includeComments;
    opt.keepForms       = request.keepForms;
    opt.embedFonts      = request.embedFonts;
    opt.compressImages  = request.compressImages;
    opt.imageQuality    = request.imageQuality;

    const bool plainRequest = request.pages.size() == pageCount
                           && request.includeComments && request.keepForms
                           && request.embedFonts;
    bool ok = false;
    if (pdfExportAvailable())
        runInBackground([&] { ok = exportPdf(source, request.path, opt); });
    if (!ok && plainRequest) ok = copyFile(source, request.path);
    *failure = ok ? QString{}
                  : pdfExportAvailable()
                      ? Exporting::tr("Could not write \"%1\".").arg(shownName)

                      : Exporting::tr("Could not write \"%1\".\n\n"
                                      "Selecting pages for a PDF needs qpdf, which this "
                                      "build does not include. "
                                      "Exporting as Word or PNG is unaffected.")
                            .arg(shownName);
    return ok;
}

}

void Exporting::run(QWidget *parent, DocumentView *view, const ExportRequest &request,
                    SaveIndicator *indicator)
{
    if (!view || request.path.isEmpty()) return;

    const QString shownName = QFileInfo(request.path).fileName();
    bool ok = false;
    QString failure;

    QElapsedTimer busy;
    busy.start();
    view->setBusy(true);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    QWidget *window = parent->window();
    CloseGuard guard(window);
    const auto closeWindow = [&] {
        view->setBusy(false);
        QApplication::restoreOverrideCursor();
        indicator->hide();
        QTimer::singleShot(0, window, &QWidget::close);
    };
    const auto reportPage = [&](int done, int total) {
        const QString text = tr("Exporting page %1 of %2…").arg(done + 1).arg(total);
        QMetaObject::invokeMethod(indicator, [indicator, text] {
            indicator->showProgress(text);
        }, Qt::QueuedConnection);
        return !guard.requested();
    };

    const bool word  = request.format == QLatin1String("word");
    const bool jpeg  = request.format == QLatin1String("jpeg");
    const bool image = jpeg || request.format == QLatin1String("image");

    ExportCopy copy(view);
    // Writing the copy hands the spinner back to the view's own state.
    view->setBusy(true);
    bool readable = !copy.path().isEmpty();
#ifdef HAVE_PDF_RENDERING
    if (readable && (word || image)) readable = copy.open();
    const DocumentExporter exporter(copy.sources());
#endif

    if (!readable) {
        failure = tr("Could not write \"%1\".").arg(shownName);

    } else if (word) {
#ifdef HAVE_PDF_RENDERING
        DocxExportOptions docxOpt;
        docxOpt.compressImages = request.compressImages;
        docxOpt.imageQuality   = request.imageQuality;

        QList<DocxPage> content;
        runInBackground([&] {
            content = exporter.allPageContent(request.pages, docxOpt, reportPage);
        });
        if (guard.requested()) {
            closeWindow();
            return;
        }

        indicator->showProgress(tr("Writing \"%1\"…").arg(shownName));
        const QString title = QFileInfo(view->currentFile()).completeBaseName();
        runInBackground([&] { ok = DocxExporter::exportToDocx(request.path, content, title); });
#endif
        failure = tr("Could not write to \"%1\".").arg(request.path);

    } else if (image) {
#ifdef HAVE_PDF_RENDERING
        runInBackground([&] {
            ok = exporter.exportPagesToImages(request.path, request.imageQuality, request.pages,
                                              reportPage);
        });
#endif
        const QString folder = QFileInfo(request.path).absolutePath();
        failure = jpeg ? tr("Could not export JPEG images to \"%1\".").arg(folder)
                       : tr("Could not export PNG images to \"%1\".").arg(folder);

    } else {
        ok = writePdf(copy.path(), view->pageCount(), request, &failure);
    }

    if (busy.elapsed() < MinBusyMs) {
        QEventLoop wait;
        QTimer::singleShot(int(MinBusyMs - busy.elapsed()), &wait, &QEventLoop::quit);
        wait.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (guard.requested()) {
        closeWindow();
        return;
    }
    view->setBusy(false);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        indicator->hide();
        QMessageBox::warning(parent, tr("Export failed"), failure);
        return;
    }

    const int pages = request.pages.size();
    const bool many = image && pages > 1;
    indicator->showDone(!many ? tr("Exported \"%1\"").arg(shownName)
                        : jpeg ? tr("%1 pages exported as JPEG images").arg(pages)
                               : tr("%1 pages exported as PNG images").arg(pages));

    if (request.openAfterExport) {
        const QString target = many ? QFileInfo(request.path).absolutePath() : request.path;
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    }
}
