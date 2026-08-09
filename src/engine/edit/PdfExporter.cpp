#include "PdfExporter.hpp"
#include "app/PdfPwStore.hpp"

#include "app/SafeWrite.hpp"

#include <QBuffer>
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

// Drops annotations the user chose not to carry over. Comments and form fields
// are separate switches, so the two are filtered independently on every page.
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

// Removes embedded font programs. The glyphs then render with a substitute
// font, which is exactly what "do not embed fonts" means — and it is usually
// the single biggest saving available on a text-heavy document.
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

// Re-encodes an image XObject as JPEG at the requested quality. Deliberately
// conservative: anything with a mask, a custom decode array, an unusual bit
// depth or an indexed/CMYK colour space is left alone, because rewriting those
// correctly needs the full colour pipeline and getting it wrong corrupts the
// page. The result is only kept when it is actually smaller.
bool recompressImage(QPDF &pdf, QPDFObjectHandle image, int quality)
{
    if (!image.isStream()) return false;
    QPDFObjectHandle dict = image.getDict();

    QPDFObjectHandle subtype = dict.getKey("/Subtype");
    if (!subtype.isName() || subtype.getName() != "/Image") return false;
    // An /SMask on the image itself is fine — it lives in its own stream and
    // keeps pointing at it. Skipping those meant scanned pages, by far the
    // commonest thing anyone compresses, were passed through untouched.
    // A stencil /Mask or a custom /Decode still needs the full colour
    // pipeline to rewrite safely, so those are left alone.
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
        return false;                       // unsupported filter — leave as is
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

    // Only worth it if it beats what the file already carries.
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
    // Soft masks carry alpha, not colour. Running them through a lossy encoder
    // would fringe every edge they cut, so they are collected first and then
    // left exactly as they are.
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

} // namespace
#endif // HAVE_QPDF

bool exportPdf(const QString &sourcePath, const QString &outPath,
               const PdfExportOptions &options)
{
#ifndef HAVE_QPDF
    Q_UNUSED(sourcePath) Q_UNUSED(outPath) Q_UNUSED(options)
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
            // Copies of a page otherwise share one annotation set, and form
            // fields drop out for want of a page reference.
            outForms.fixCopiedAnnotations(page.getObjectHandle(),
                                          source[idx].getObjectHandle(), inForms);
            filterAnnotations(page, options.includeComments, options.keepForms);
        }
        if (outPages.getAllPages().empty()) return false;

        if (!options.keepForms)
            out.getRoot().removeKey("/AcroForm");
        if (!options.embedFonts)
            stripEmbeddedFonts(out);
        if (options.compressImages)
            recompressImages(out, options.imageQuality);

        // Staged: "export" onto the document currently open would truncate the
        // very file the pages are still being copied from.
        const QString staging = SafeWrite::stagingPath(outPath);
        if (staging.isEmpty()) return false;
        {
            QPDFWriter writer(out, staging.toLocal8Bit().constData());
            writer.setCompressStreams(true);
            writer.setObjectStreamMode(qpdf_o_generate);
            if (!options.userPassword.isEmpty()) {
                const std::string pass = options.userPassword.toStdString();
                // AES-256. The user password also serves as the owner password,
                // so the document opens with the one password the dialog asked
                // for; permissions stay fully granted once it is entered.
                writer.setR6EncryptionParameters(
                    pass.c_str(), pass.c_str(),
                    /*accessibility*/ true, /*extract*/ true, /*assemble*/ true,
                    /*annotate_and_form*/ true, /*form_filling*/ true,
                    /*modify_other*/ true, qpdf_r3p_full, /*encrypt_metadata*/ true);
            }
            writer.write();
        }
        return SafeWrite::commit(staging, outPath);

    } catch (const std::exception &ex) {
        qWarning() << "[PDF] export failed:" << ex.what();
        return false;
    }
#endif
}
