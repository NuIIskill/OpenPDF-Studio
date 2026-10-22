#include "ui/export/Exporting.hpp"

#include "ui/DocumentView.hpp"
#include "ui/SaveIndicator.hpp"
#include "engine/export/DocxExporter.hpp"
#include "engine/export/PdfExporter.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
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

bool writePdf(DocumentView *view, const ExportRequest &request, QString *failure)
{
    const QString shownName = QFileInfo(request.path).fileName();
    const QString source = view->contentFile();
    PdfExportOptions opt;
    opt.pages           = request.pages;
    opt.includeComments = request.includeComments;
    opt.keepForms       = request.keepForms;
    opt.embedFonts      = request.embedFonts;
    opt.compressImages  = request.compressImages;
    opt.imageQuality    = request.imageQuality;

    const bool plainRequest = request.pages.size() == view->pageCount()
                           && request.includeComments && request.keepForms
                           && request.embedFonts;
    bool ok = pdfExportAvailable() && exportPdf(source, request.path, opt);
    if (!ok && plainRequest) ok = view->saveToFile(request.path);
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

    if (request.format == QLatin1String("word")) {
        DocxExportOptions docxOpt;
        docxOpt.compressImages = request.compressImages;
        docxOpt.imageQuality   = request.imageQuality;

        QList<DocxPage> content;
        runInBackground([&] {
            content = view->allPageContent(request.pages, docxOpt, reportPage);
        });
        if (guard.requested()) {
            closeWindow();
            return;
        }

        indicator->showProgress(tr("Writing \"%1\"…").arg(shownName));
        const QString title = QFileInfo(view->currentFile()).completeBaseName();
        runInBackground([&] { ok = DocxExporter::exportToDocx(request.path, content, title); });
        failure = tr("Could not write to \"%1\".").arg(request.path);

    } else if (request.format == QLatin1String("image")) {
        runInBackground([&] {
            ok = view->exportPagesToImages(request.path, request.imageQuality, request.pages,
                                           reportPage);
        });
        failure = tr("Could not export PNG images to \"%1\".")
                      .arg(QFileInfo(request.path).absolutePath());

    } else {
        ok = writePdf(view, request, &failure);
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
    indicator->showDone(request.format == QLatin1String("image") && pages > 1
                            ? tr("%1 pages exported as PNG images").arg(pages)
                            : tr("Exported \"%1\"").arg(shownName));

    if (request.openAfterExport) {

        const bool many = request.format == QLatin1String("image") && pages > 1;
        const QString target = many ? QFileInfo(request.path).absolutePath() : request.path;
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    }
}
