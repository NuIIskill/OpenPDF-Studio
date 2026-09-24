#include "cli/Commands.hpp"
#include "app/PdfPwStore.hpp"
#include "engine/export/DocxExporter.hpp"
#include "engine/export/PdfExporter.hpp"
#include "engine/import/DocumentImport.hpp"
#include "ui/DocumentView.hpp"

#include <QDebug>
#include <QFileInfo>

namespace Cli {

int exportPdfCommand(const QStringList &args)
{
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
        else if (o.startsWith(QLatin1String("pages=")))
            appendPages(o.mid(6), opt.pages);
    }
    return exportPdf(args.at(2), args.at(3), opt) ? 0 : 3;
}

int exportDocxCommand(const QStringList &args)
{
    QList<int> pages;
    DocxExportOptions docxOpt;
    for (int a = 4; a < args.size(); ++a) {
        const QString o = args.at(a);
        if      (o.startsWith(QLatin1String("srcpw="))) PdfPwStore::set(args.at(2), o.mid(6));
        else if (o == QLatin1String("nocompress"))      docxOpt.compressImages = false;
        else if (o.startsWith(QLatin1String("q=")))     docxOpt.imageQuality = o.mid(2).toInt();
        else if (o.startsWith(QLatin1String("pages="))) appendPages(o.mid(6), pages);
    }
    DocumentView view;
    if (!view.openFile(args.at(2))) return 2;
    const bool ok = DocxExporter::exportToDocx(
        args.at(3), view.allPageContent(pages),
        QFileInfo(args.at(2)).completeBaseName(), docxOpt);
    return ok ? 0 : 3;
}

int exportImagesCommand(const QStringList &args)
{
    QList<int> pages;
    int quality = 85;
    for (int a = 4; a < args.size(); ++a) {
        const QString o = args.at(a);
        if      (o.startsWith(QLatin1String("srcpw="))) PdfPwStore::set(args.at(2), o.mid(6));
        else if (o.startsWith(QLatin1String("q=")))     quality = o.mid(2).toInt();
        else if (o.startsWith(QLatin1String("pages="))) appendPages(o.mid(6), pages);
    }
    DocumentView view;
    if (!view.openFile(args.at(2))) return 2;
    return view.exportPagesToImages(args.at(3), quality, pages) ? 0 : 3;
}

int importPdfCommand(const QStringList &args)
{
    QString dump;
    for (int a = 4; a < args.size(); ++a)
        if (args.at(a).startsWith(QLatin1String("dump=")))
            dump = args.at(a).mid(5);

    QString error;
    if (!DocumentImport::convertToPdf(args.at(2), args.at(3), &error, dump)) {
        qWarning().noquote() << "[import]" << error;
        return 3;
    }
    return 0;
}

}
