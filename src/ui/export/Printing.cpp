#include "ui/export/Printing.hpp"

#include "ui/DocumentView.hpp"

#ifdef HAVE_QT_PRINT
#include <QApplication>
#include <QFileInfo>
#include <QMarginsF>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageRanges>
#include <QPrintDialog>
#include <QPrinter>

namespace {

QList<int> pagesToPrint(QPrinter &printer, int pageCount, int currentPage)
{
    QList<int> pages;
    switch (printer.printRange()) {
    case QPrinter::CurrentPage:
        pages.append(currentPage);
        break;
    case QPrinter::PageRange: {

        const QPageRanges ranges = printer.pageRanges();
        if (!ranges.isEmpty()) {
            for (int p = ranges.firstPage(); p <= ranges.lastPage(); ++p)
                if (ranges.contains(p)) pages.append(p - 1);
        } else {
            for (int p = printer.fromPage(); p <= printer.toPage(); ++p)
                pages.append(p - 1);
        }
        break;
    }
    default:
        break;
    }

    const int copies = printer.supportsMultipleCopies()
        ? 1 : qMax(1, printer.copyCount());
    if (copies > 1) {
        if (pages.isEmpty())
            for (int p = 0; p < pageCount; ++p) pages.append(p);
        const QList<int> once = pages;
        pages.clear();
        if (printer.collateCopies()) {
            for (int c = 0; c < copies; ++c) pages.append(once);
        } else {
            for (int page : once)
                for (int c = 0; c < copies; ++c) pages.append(page);
        }
    }
    return pages;
}

}
#endif

void Printing::run(QWidget *parent, DocumentView *view)
{
#ifdef HAVE_QT_PRINT
    if (!view || view->contentFile().isEmpty()) return;
    const int pageCount = view->pageCount();
    if (pageCount <= 0) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(view->currentFile()).completeBaseName());

    printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    printer.setFromTo(1, pageCount);

    QPrintDialog dlg(&printer, parent);
    dlg.setOption(QAbstractPrintDialog::PrintPageRange);
    dlg.setOption(QAbstractPrintDialog::PrintCurrentPage);
    dlg.setWindowTitle(tr("Print"));
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<int> pages = pagesToPrint(printer, pageCount, view->currentPage());

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = view->printDocument(&printer, pages);
    QApplication::restoreOverrideCursor();

    if (!ok)
        QMessageBox::warning(parent, tr("Print"),
                             tr("The document could not be printed."));
#else
    Q_UNUSED(parent)
    Q_UNUSED(view)
#endif
}
