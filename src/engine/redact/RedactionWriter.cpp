#include "engine/redact/RedactionWriter.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM) && defined(HAVE_QPDF)

#include "app/PdfPwStore.hpp"
#include "app/SafeWrite.hpp"
#include "engine/document/PdfiumLock.hpp"
#include "engine/document/PdfiumWriter.hpp"
#include "engine/redact/PdfiumRedaction.hpp"

#include "fpdfview.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QDebug>
#include <QSet>
#include <QFile>

#include <algorithm>

namespace {

// A page that could not be cut in place becomes its image and the bars, nothing else.
void replaceByImage(QPDF &pdf, QPDFObjectHandle page, const PdfiumRedaction::Raster &raster)
{
    QPDFObjectHandle image = QPDFObjectHandle::newStream(
        &pdf, std::string(raster.rgb.constData(), size_t(raster.rgb.size())));
    QPDFObjectHandle dict = image.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(raster.size.width()));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(raster.size.height()));
    dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    dict.replaceKey("/Filter", QPDFObjectHandle::newName("/FlateDecode"));

    const QTransform &m = raster.placement;
    QByteArray content = QStringLiteral("q %1 %2 %3 %4 %5 %6 cm /Page Do Q\n0 g\n")
        .arg(m.m11(), 0, 'f', 4).arg(m.m12(), 0, 'f', 4).arg(m.m21(), 0, 'f', 4)
        .arg(m.m22(), 0, 'f', 4).arg(m.dx(), 0, 'f', 4).arg(m.dy(), 0, 'f', 4).toLatin1();
    for (const QRectF &bar : raster.bars)
        content += QStringLiteral("%1 %2 %3 %4 re f\n").arg(bar.left(), 0, 'f', 3)
                       .arg(bar.top(), 0, 'f', 3).arg(bar.width(), 0, 'f', 3)
                       .arg(bar.height(), 0, 'f', 3).toLatin1();

    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/Page", image);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents", QPDFObjectHandle::newStream(&pdf, content.toStdString()));
    page.removeKey("/Annots");
    page.removeKey("/Group");
}

// Only the pages go into the copy: outlines, forms, names, metadata and
// structure could all still hold what was redacted.
int rebuild(const QString &input, const QString &output, const QString &password,
            const QHash<int, PdfiumRedaction::Raster> &rasters)
{
    try {
        QPDF in;
        const QByteArray pw = password.toUtf8();
        in.processFile(input.toLocal8Bit().constData(), pw.isEmpty() ? nullptr : pw.constData());
        in.pushInheritedAttributesToPage();
        QPDFPageDocumentHelper source(in);
        source.removeUnreferencedResources();

        QPDF out;
        out.emptyPDF();
        QPDFPageDocumentHelper target(out);
        const std::vector<QPDFPageObjectHelper> pages = source.getAllPages();
        for (size_t i = 0; i < pages.size(); ++i) {
            QPDFPageObjectHelper page = pages.at(i);
            QPDFObjectHandle dict = page.getObjectHandle();
            if (rasters.contains(int(i))) replaceByImage(in, dict, rasters.value(int(i)));
            for (const char *key : { "/Thumb", "/PieceInfo", "/Metadata", "/AA" })
                dict.removeKey(key);
            target.addPage(page, false);
        }
        // Layers keep their visibility; without them hidden content would show.
        QPDFObjectHandle layers = in.getRoot().getKey("/OCProperties");
        if (layers.isDictionary())
            out.getRoot().replaceKey("/OCProperties", out.copyForeignObject(layers));

        QPDFWriter writer(out, output.toLocal8Bit().constData());
        writer.setCompressStreams(true);
        writer.setObjectStreamMode(qpdf_o_generate);
        // An encrypted document stays encrypted with its password, as saving keeps it.
        if (!pw.isEmpty())
            writer.setR6EncryptionParameters(pw.constData(), pw.constData(), true, true, true,
                                             true, true, true, qpdf_r3p_full, true);
        writer.write();
        return int(pages.size());
    } catch (const std::exception &ex) {
        qWarning() << "[Redaction] the copy could not be rebuilt:" << ex.what();
        return -1;
    }
}

// What still shows under the areas, by page.
QHash<int, QStringList> check(const QString &path, const QString &password, int pages,
                              const RedactionWriter::Areas &areas)
{
    PdfiumLock lock;
    const QByteArray file = path.toUtf8();
    const QByteArray pw = password.toUtf8();
    FPDF_DOCUMENT doc = FPDF_LoadDocument(file.constData(), pw.isEmpty() ? nullptr : pw.constData());
    if (!doc) return { { -1, { QStringLiteral("unreadable") } } };
    QHash<int, QStringList> found;
    if (FPDF_GetPageCount(doc) != pages) found[-1] << QStringLiteral("page count");
    for (auto it = areas.cbegin(); it != areas.cend() && !found.contains(-1); ++it) {
        FPDF_PAGE page = FPDF_LoadPage(doc, it.key());
        if (!page) { found[-1] << QStringLiteral("missing page"); break; }
        const QStringList leaks = PdfiumRedaction::leaks(doc, page, it.value());
        if (!leaks.isEmpty()) found.insert(it.key(), leaks);
        FPDF_ClosePage(page);
    }
    FPDF_CloseDocument(doc);
    return found;
}

// One attempt at the copy; pages named in asImage become images outright.
RedactionWriter::Result attempt(const QString &source, const WriteGrant &target,
                                const QString &staging, const RedactionWriter::Areas &areas,
                                const QString &password, const QSet<int> &asImage,
                                QHash<int, QStringList> *found)
{
    const WriteGrant changed = target.beside(QStringLiteral(".cut"));

    PdfiumWriter::PagePass pass;
    pass.pages = areas.keys();
    std::sort(pass.pages.begin(), pass.pages.end());
    QHash<int, PdfiumRedaction::Raster> rasters;
    pass.change = [&](FPDF_DOCUMENT doc, FPDF_PAGE page, int index, bool &inPlace) {
        PdfiumRedaction::Raster raster;
        if (!PdfiumRedaction::apply(doc, page, index, areas.value(index), asImage.contains(index),
                                    inPlace, &raster))
            return false;
        if (!raster.rgb.isEmpty()) rasters.insert(index, raster);
        return true;
    };
    const bool rewritten = PdfiumWriter::rewrite(source, changed, pass);
    const int pages = rewritten ? rebuild(changed.path(), staging, password, rasters) : -1;
    QFile::remove(changed.path());
    if (pages <= 0) return RedactionWriter::Result::Failed;
    *found = check(staging, password, pages, areas);
    return found->isEmpty() ? RedactionWriter::Result::Written : RedactionWriter::Result::Leaked;
}

}

bool RedactionWriter::available()
{
    return true;
}

RedactionWriter::Result RedactionWriter::write(const QString &source, const WriteGrant &target,
                                               const Areas &areas, const QString &password)
{
    if (source.isEmpty() || target.path().isEmpty() || areas.isEmpty()) return Result::Failed;
    const QString staging = SafeWrite::stagingPath(target.path());
    if (staging.isEmpty()) return Result::Failed;
    const bool known = PdfPwStore::has(source);
    if (!password.isEmpty() && !known) PdfPwStore::set(source, password);

    // A page that still shows something under its areas is made an image and checked again.
    QSet<int> asImage;
    QHash<int, QStringList> found;
    Result result = attempt(source, target, staging, areas, password, asImage, &found);
    if (result == Result::Leaked && !found.contains(-1)) {
        qWarning() << "[Redaction] cut in place, these pages still show content under the areas:" << found;
        for (auto it = found.cbegin(); it != found.cend(); ++it) asImage.insert(it.key());
        result = attempt(source, target, staging, areas, password, asImage, &found);
    }
    if (!password.isEmpty() && !known) PdfPwStore::forget(source);
    if (result != Result::Written) {
        if (result == Result::Leaked)
            qWarning() << "[Redaction] the copy still shows content under the areas:" << found;
        SafeWrite::discard(staging);
        return result;
    }
    return SafeWrite::commit(staging, target.path()) ? Result::Written : Result::Failed;
}

#else

bool RedactionWriter::available()
{
    return false;
}

RedactionWriter::Result RedactionWriter::write(const QString &, const WriteGrant &, const Areas &,
                                               const QString &)
{
    return Result::Failed;
}

#endif
