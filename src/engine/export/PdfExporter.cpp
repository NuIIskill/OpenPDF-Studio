#include "engine/export/PdfExporter.hpp"
#include "app/PdfPwStore.hpp"

#include "app/SafeWrite.hpp"
#include "engine/document/PdfConformance.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QImage>

#include <cstring>
#include <set>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFAcroFormDocumentHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFWriter.hh>
#  include <qpdf/Constants.h>
#endif

bool pdfExportAvailable()
{
#ifdef HAVE_QPDF
    return true;
#else
    return false;
#endif
}

#ifdef HAVE_QPDF
namespace {

bool isWidget(QPDFObjectHandle annot)
{
    if (!annot.isDictionary()) return false;
    QPDFObjectHandle sub = annot.getKey("/Subtype");
    return sub.isName() && sub.getName() == "/Widget";
}

void filterAnnotations(QPDFPageObjectHelper &page, bool keepComments, bool keepForms)
{
    if (keepComments && keepForms) return;

    QPDFObjectHandle dict = page.getObjectHandle();
    QPDFObjectHandle annots = dict.getKey("/Annots");
    if (!annots.isArray()) return;

    QPDFObjectHandle kept = QPDFObjectHandle::newArray();
    for (int i = 0; i < annots.getArrayNItems(); ++i) {
        QPDFObjectHandle annot = annots.getArrayItem(i);
        const bool widget = isWidget(annot);
        if (widget ? keepForms : keepComments) kept.appendItem(annot);
    }
    if (kept.getArrayNItems() == 0) dict.removeKey("/Annots");
    else                            dict.replaceKey("/Annots", kept);
}

void stripEmbeddedFonts(QPDF &pdf)
{
    for (QPDFObjectHandle obj : pdf.getAllObjects()) {
        if (!obj.isDictionary()) continue;
        QPDFObjectHandle type = obj.getKey("/Type");
        if (!type.isName() || type.getName() != "/FontDescriptor") continue;
        obj.removeKey("/FontFile");
        obj.removeKey("/FontFile2");
        obj.removeKey("/FontFile3");
    }
}

bool recompressImage(QPDF &pdf, QPDFObjectHandle image, int quality)
{
    if (!image.isStream()) return false;
    QPDFObjectHandle dict = image.getDict();

    QPDFObjectHandle subtype = dict.getKey("/Subtype");
    if (!subtype.isName() || subtype.getName() != "/Image") return false;

    if (dict.hasKey("/Mask") || dict.hasKey("/Decode")) return false;
    QPDFObjectHandle maskFlag = dict.getKey("/ImageMask");
    if (maskFlag.isBool() && maskFlag.getBoolValue()) return false;

    QPDFObjectHandle bpcObj = dict.getKey("/BitsPerComponent");
    if (!bpcObj.isInteger() || bpcObj.getIntValue() != 8) return false;

    QPDFObjectHandle csObj = dict.getKey("/ColorSpace");
    if (!csObj.isName()) return false;
    const std::string cs = csObj.getName();
    const bool gray = cs == "/DeviceGray";
    const bool rgb  = cs == "/DeviceRGB";
    if (!gray && !rgb) return false;

    QPDFObjectHandle wObj = dict.getKey("/Width");
    QPDFObjectHandle hObj = dict.getKey("/Height");
    if (!wObj.isInteger() || !hObj.isInteger()) return false;
    const int w = static_cast<int>(wObj.getIntValue());
    const int h = static_cast<int>(hObj.getIntValue());
    if (w <= 0 || h <= 0 || qint64(w) * h > 64LL * 1024 * 1024) return false;

    std::shared_ptr<Buffer> raw;
    try {
        raw = image.getStreamData(qpdf_dl_all);
    } catch (const std::exception &) {
        return false;
    }
    if (!raw) return false;

    const int channels = gray ? 1 : 3;
    const qint64 expected = qint64(w) * h * channels;
    if (qint64(raw->getSize()) < expected) return false;

    QImage img(w, h, gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
    const unsigned char *src = raw->getBuffer();
    for (int y = 0; y < h; ++y)
        std::memcpy(img.scanLine(y), src + qint64(y) * w * channels,
                    size_t(w) * channels);

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!img.save(&buffer, "JPEG", qBound(10, quality, 100))) return false;
    buffer.close();

    if (jpeg.isEmpty() || jpeg.size() >= int(image.getRawStreamData()->getSize()))
        return false;

    dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName(gray ? "/DeviceGray"
                                                                 : "/DeviceRGB"));
    image.replaceStreamData(std::string(jpeg.constData(), size_t(jpeg.size())),
                            QPDFObjectHandle::newName("/DCTDecode"),
                            QPDFObjectHandle::newNull());
    Q_UNUSED(pdf);
    return true;
}

void recompressImages(QPDF &pdf, int quality)
{

    std::set<QPDFObjGen> masks;
    for (QPDFObjectHandle obj : pdf.getAllObjects()) {
        if (!obj.isStream() && !obj.isDictionary()) continue;
        QPDFObjectHandle dict = obj.isStream() ? obj.getDict() : obj;
        for (const char *key : { "/SMask", "/Mask" }) {
            QPDFObjectHandle ref = dict.getKey(key);
            if (ref.isStream() || ref.isDictionary()) masks.insert(ref.getObjGen());
        }
    }

    for (QPDFObjectHandle obj : pdf.getAllObjects()) {
        if (masks.count(obj.getObjGen())) continue;
        try {
            recompressImage(pdf, obj, quality);
        } catch (const std::exception &ex) {
            qWarning() << "[PDF] image recompression skipped:" << ex.what();
        }
    }
}

void copyInfo(QPDF &in, QPDF &out)
{
    QPDFObjectHandle source = in.getTrailer().getKey("/Info");
    if (!source.isDictionary()) return;
    QPDFObjectHandle info = QPDFObjectHandle::newDictionary();
    for (const std::string &key : source.getKeys())
        if (source.getKey(key).isString())
            info.replaceKey(key, QPDFObjectHandle::newString(source.getKey(key).getStringValue()));
    out.getTrailer().replaceKey("/Info", out.makeIndirectObject(info));
}

// The written file is converted once more; anything still to do means it is not PDF/A.
bool verifyPdfA(const QString &staging, PdfA::Report &report)
{
    PdfA::Report again;
    QStringList broken;
    try {
        QPDF written;
        written.processFile(staging.toLocal8Bit().constData());
        PdfA::convert(written, again);
        broken = PdfConformance::problems(staging, {});
    } catch (const std::exception &ex) {
        broken << QString::fromUtf8(ex.what());
    }
    if (again.clean() && broken.isEmpty()) return true;
    qWarning() << "[PDF] PDF/A check of the written file failed:" << again.blockers << again.changes
               << again.repairs << broken;
    report.blockers << QCoreApplication::translate("PdfA", "The written file did not pass the PDF/A check.");
    report.blockers << again.blockers << again.changes << broken;
    return false;
}

}
#endif

bool exportPdf(const QString &sourcePath, const WriteGrant &outPathGrant,
               const PdfExportOptions &options, PdfA::Report *pdfA)
{
    const QString outPath = outPathGrant.path();
#ifndef HAVE_QPDF
    Q_UNUSED(sourcePath) Q_UNUSED(outPath) Q_UNUSED(options) Q_UNUSED(pdfA)
    return false;
#else
    if (sourcePath.isEmpty() || outPath.isEmpty()) return false;
    try {
        QPDF in;
        const std::string pw = PdfPwStore::forQpdf(sourcePath);
        in.processFile(sourcePath.toLocal8Bit().constData(),
                       pw.empty() ? nullptr : pw.c_str());

        QPDF out;
        out.emptyPDF();
        QPDFPageDocumentHelper inPages(in);
        QPDFPageDocumentHelper outPages(out);
        QPDFAcroFormDocumentHelper inForms(in);
        QPDFAcroFormDocumentHelper outForms(out);

        std::vector<QPDFPageObjectHelper> source = inPages.getAllPages();
        const int total = static_cast<int>(source.size());

        QList<int> wanted = options.pages;
        if (wanted.isEmpty())
            for (int i = 0; i < total; ++i) wanted.append(i);

        for (int index : wanted) {
            if (index < 0 || index >= total) continue;
            const auto idx = static_cast<std::size_t>(index);
            outPages.addPage(source[idx], false);

            auto added = outPages.getAllPages();
            if (added.empty()) continue;
            QPDFPageObjectHelper page = added.back();

            outForms.fixCopiedAnnotations(page.getObjectHandle(),
                                          source[idx].getObjectHandle(), inForms);
            filterAnnotations(page, options.includeComments, options.keepForms);
        }
        if (outPages.getAllPages().empty()) return false;

        if (!options.keepForms)
            out.getRoot().removeKey("/AcroForm");
        if (!options.embedFonts && !options.pdfA)
            stripEmbeddedFonts(out);
        if (options.compressImages)
            recompressImages(out, options.imageQuality);

        PdfA::Report unused;
        PdfA::Report &report = pdfA ? *pdfA : unused;
        if (options.pdfA) {
            copyInfo(in, out);
            PdfA::convert(out, report);
            if (!report.blockers.isEmpty()) return false;
        }

        const QString staging = SafeWrite::stagingPath(outPath);
        if (staging.isEmpty()) return false;
        {
            QPDFWriter writer(out, staging.toLocal8Bit().constData());
            writer.setCompressStreams(true);
            writer.setObjectStreamMode(qpdf_o_generate);
            if (options.pdfA) {
                writer.forcePDFVersion("1.7");
                writer.setNewlineBeforeEndstream(true);
            } else if (!options.userPassword.isEmpty()) {
                const std::string pass = options.userPassword.toStdString();

                writer.setR6EncryptionParameters(
                    pass.c_str(), pass.c_str(),
                      true,   true,   true,
                      true,   true,
                      true, qpdf_r3p_full,   true);
            }
            writer.write();
        }
        if (options.pdfA && !verifyPdfA(staging, report)) {
            SafeWrite::discard(staging);
            return false;
        }
        return SafeWrite::commit(staging, outPath);

    } catch (const std::exception &ex) {
        qWarning() << "[PDF] export failed:" << ex.what();
        return false;
    }
#endif
}
