#include "ui/export/Printing.hpp"

#include "ui/DocumentView.hpp"

#if defined(HAVE_QT_PRINT) && defined(HAVE_PDF_RENDERING)
#include "ui/export/ExportCopy.hpp"
#include "ui/export/PrintDialog.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QPrinter>
#endif

void Printing::run(QWidget *parent, DocumentView *view)
{
#if defined(HAVE_QT_PRINT) && defined(HAVE_PDF_RENDERING)
    if (!view || view->contentFile().isEmpty() || view->pageCount() <= 0) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    ExportCopy copy(view);
    const bool opened = copy.open();
    QApplication::restoreOverrideCursor();
    if (!opened) {
        QMessageBox::warning(parent, tr("Print"), tr("The document could not be printed."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(view->currentFile()).completeBaseName());

    PrintDialog dlg(&printer, view, copy.sources(), parent);
    if (dlg.exec() != QDialog::Accepted) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = PrintSheets(copy.sources(), dlg.job()).print(&printer);
    QApplication::restoreOverrideCursor();

    if (!ok)
        QMessageBox::warning(parent, tr("Print"),
                             tr("The document could not be printed."));
#else
    Q_UNUSED(parent)
    Q_UNUSED(view)
#endif
}
