#include "ui/export/Exporting.hpp"

#include "ui/DocumentView.hpp"
#include "engine/export/DocxExporter.hpp"
#include "engine/export/PdfExporter.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QUrl>

void Exporting::run(QWidget *parent, DocumentView *view, const ExportRequest &request)
{
    if (!view || request.path.isEmpty()) return;

    const QString shownName = QFileInfo(request.path).fileName();
    bool ok = false;
    QString failure;

    if (request.format == QLatin1String("word")) {
        const QList<DocxPage> content = view->allPageContent(request.pages);
        const QString title = QFileInfo(view->currentFile()).completeBaseName();
        DocxExportOptions docxOpt;
        docxOpt.compressImages = request.compressImages;
        docxOpt.imageQuality   = request.imageQuality;
        ok = DocxExporter::exportToDocx(request.path, content, title, docxOpt);
        failure = tr("Could not write to \"%1\".").arg(request.path);

    } else if (request.format == QLatin1String("image")) {
        ok = view->exportPagesToImages(request.path, request.imageQuality, request.pages);
        failure = tr("Could not export PNG images to \"%1\".")
                      .arg(QFileInfo(request.path).absolutePath());

    } else {

        const QString source = view->contentFile();
        PdfExportOptions opt;
        opt.pages           = request.pages;
        opt.includeComments = request.includeComments;
        opt.keepForms       = request.keepForms;
        opt.embedFonts      = request.embedFonts;
        opt.compressImages  = request.compressImages;
        opt.imageQuality    = request.imageQuality;
        opt.userPassword    = request.password;

        const bool plainRequest = request.pages.size() == view->pageCount()
                               && request.includeComments && request.keepForms
                               && request.embedFonts && request.password.isEmpty();
        ok = pdfExportAvailable() && exportPdf(source, request.path, opt);
        if (!ok && plainRequest) ok = view->saveToFile(request.path);
        failure = ok ? QString{}
                     : pdfExportAvailable()
                         ? tr("Could not write \"%1\".").arg(shownName)

                         : tr("Could not write \"%1\".\n\n"
                              "Selecting pages or setting a password for a PDF "
                              "needs qpdf, which this build does not include. "
                              "Exporting as Word or PNG is unaffected.")
                               .arg(shownName);
    }

    if (!ok) {
        QMessageBox::warning(parent, tr("Export failed"), failure);
        return;
    }

    const int pages = request.pages.size();
    QMessageBox::information(parent, tr("Export successful"),
        request.format == QLatin1String("image") && pages > 1
            ? tr("%1 pages exported as PNG images.").arg(pages)
            : tr("Document exported to \"%1\".").arg(shownName));

    if (request.openAfterExport) {

        const bool many = request.format == QLatin1String("image") && pages > 1;
        const QString target = many ? QFileInfo(request.path).absolutePath() : request.path;
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    }
}
