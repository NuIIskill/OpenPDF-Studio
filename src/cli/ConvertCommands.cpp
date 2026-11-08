#include "cli/CliWrites.hpp"
#include "cli/Commands.hpp"
#include "app/PdfPwStore.hpp"
#include "engine/export/DocumentExporter.hpp"
#include "engine/export/DocxExporter.hpp"
#include "engine/export/MarkupExporter.hpp"
#include "engine/export/OdtExporter.hpp"
#include "engine/export/PdfExporter.hpp"
#include "engine/import/DocumentImport.hpp"
#include "ui/DocumentView.hpp"
#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#endif

#include <QDebug>
#include <QFileInfo>

#include <cstdio>

namespace Cli {

namespace {

// The view asks for a missing or wrong password in a dialog nobody can answer
// here, so a document that does not open with what srcpw= gave is refused first.
bool openWithoutPrompt(DocumentView &view, const QString &path)
{
#ifdef HAVE_PDF_RENDERING
    if (DocumentSource probe; !probe.open(path, nullptr)) {
        qWarning().noquote() << "[export]" << path
                             << "cannot be opened; an encrypted file needs srcpw=";
        return false;
    }
#endif
    return view.openFile(path);
}

}

int exportPdfCommand(const QStringList &args)
{
    PdfExportOptions opt;
    for (int a = 4; a < args.size(); ++a) {
        const QString o = args.at(a);
        if      (o == QLatin1String("nocomments")) opt.includeComments = false;
        else if (o == QLatin1String("noforms"))    opt.keepForms       = false;
        else if (o == QLatin1String("nofonts"))    opt.embedFonts      = false;
        else if (o == QLatin1String("nocompress")) opt.compressImages  = false;
        else if (o == QLatin1String("pdfa"))       opt.pdfA            = true;
        else if (o.startsWith(QLatin1String("q=")))
            opt.imageQuality = o.mid(2).toInt();
        else if (o.startsWith(QLatin1String("pw=")))
            opt.userPassword = o.mid(3);
        else if (o.startsWith(QLatin1String("srcpw=")))
            PdfPwStore::set(args.at(2), o.mid(6));
        else if (o.startsWith(QLatin1String("pages=")))
            appendPages(o.mid(6), opt.pages);
    }
    PdfA::Report report;
    const bool ok = exportPdf(args.at(2), CliWrites::to(args.at(3)), opt, &report);
    for (const QString &line : std::as_const(report.changes))
        std::fprintf(stderr, "changed: %s\n", qPrintable(line));
    for (const QString &line : std::as_const(report.blockers))
        std::fprintf(stderr, "blocked: %s\n", qPrintable(line));
    return ok ? 0 : 3;
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
    if (!openWithoutPrompt(view, args.at(2))) return 2;
    const QList<DocxPage> content = view.allPageContent(pages, docxOpt);
    const QString mode  = args.at(1);
    const QString title = QFileInfo(args.at(2)).completeBaseName();
    const bool ok = mode == QLatin1String("--export-odt")  ? OdtExporter::exportToOdt(args.at(3), content)
                  : mode == QLatin1String("--export-md")   ? MarkupExporter::exportToMarkdown(args.at(3), content)
                  : mode == QLatin1String("--export-html") ? MarkupExporter::exportToHtml(args.at(3), content, title)
                  : DocxExporter::exportToDocx(args.at(3), content, title);
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
    if (!openWithoutPrompt(view, args.at(2))) return 2;
    return view.exportPagesToImages(args.at(3), quality, pages) ? 0 : 3;
}

int exportTextCommand(const QStringList &args)
{
    QList<int> pages;
    for (int a = 4; a < args.size(); ++a) {
        const QString o = args.at(a);
        if      (o.startsWith(QLatin1String("srcpw="))) PdfPwStore::set(args.at(2), o.mid(6));
        else if (o.startsWith(QLatin1String("pages="))) appendPages(o.mid(6), pages);
    }
#ifdef HAVE_PDF_RENDERING
    DocumentSource source;
    if (!source.open(args.at(2), nullptr)) {
        qWarning().noquote() << "[export]" << args.at(2)
                             << "cannot be opened; an encrypted file needs srcpw=";
        return 2;
    }
    DocumentExporter::Sources src;
    src.backend   = source.backend();
    src.pageCount = source.pageCount();
    return DocumentExporter(src).exportPagesToText(args.at(3), pages) ? 0 : 3;
#else
    return 2;
#endif
}

int importPdfCommand(const QStringList &args)
{
    QString dump;
    for (int a = 4; a < args.size(); ++a)
        if (args.at(a).startsWith(QLatin1String("dump=")))
            dump = args.at(a).mid(5);

    QString error;
    if (!DocumentImport::convertToPdf(args.at(2), CliWrites::to(args.at(3)), &error, dump)) {
        qWarning().noquote() << "[import]" << error;
        return 3;
    }
    return 0;
}

}
