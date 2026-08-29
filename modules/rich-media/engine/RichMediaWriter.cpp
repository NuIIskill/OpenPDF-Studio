// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/RichMediaWriter.hpp"

#include "rich-media/engine/MediaSession.hpp"
#include "rich-media/engine/PosterFrame.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFEFStreamObjectHelper.hh>
#  include <qpdf/QPDFFileSpecObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFWriter.hh>
#  include <qpdf/QUtil.hh>

#  include <algorithm>
#  include <string>
#  include <vector>
#endif

bool RichMediaWriter::available()
{
#ifdef HAVE_QPDF
    return true;
#else
    return false;
#endif
}

#ifdef HAVE_QPDF

namespace {

struct PageBox { double left { 0 }, top { 0 }; bool valid { false }; };

PageBox pageBox(QPDFPageObjectHelper &page)
{
    PageBox box;
    QPDFObjectHandle rect = page.getCropBox(false, true);
    if (!rect.isArray() || rect.getArrayNItems() != 4) return box;
    double v[4];
    for (int i = 0; i < 4; ++i) {
        QPDFObjectHandle n = rect.getArrayItem(i);
        if (!n.isNumber()) return box;
        v[i] = n.getNumericValue();
    }
    box.left  = std::min(v[0], v[2]);
    box.top   = std::max(v[1], v[3]);
    box.valid = true;
    return box;
}

QPDFObjectHandle toPdfRect(const QRectF &bounds, const PageBox &box)
{
    const double x0 = box.left + bounds.left();
    const double x1 = box.left + bounds.right();
    const double y0 = box.top  - bounds.bottom();
    const double y1 = box.top  - bounds.top();
    return QPDFObjectHandle::newArray({
        QPDFObjectHandle::newReal(x0, 4), QPDFObjectHandle::newReal(y0, 4),
        QPDFObjectHandle::newReal(x1, 4), QPDFObjectHandle::newReal(y1, 4),
    });
}

QPDFObjectHandle imageXObject(QPDF &pdf, const QImage &image)
{
    QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull()) return QPDFObjectHandle::newNull();

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!rgb.save(&buffer, "JPEG", 88)) return QPDFObjectHandle::newNull();
    buffer.close();

    QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type",             QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype",          QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width",            QPDFObjectHandle::newInteger(rgb.width()));
    dict.replaceKey("/Height",           QPDFObjectHandle::newInteger(rgb.height()));
    dict.replaceKey("/ColorSpace",       QPDFObjectHandle::newName("/DeviceRGB"));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));

    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&pdf);
    stream.replaceDict(dict);
    stream.replaceStreamData(std::string(jpeg.constData(), static_cast<size_t>(jpeg.size())),
                             QPDFObjectHandle::newName("/DCTDecode"),
                             QPDFObjectHandle::newNull());
    return pdf.makeIndirectObject(stream);
}

QPDFObjectHandle appearanceStream(QPDF &pdf, const QImage &poster,
                                  double width, double height)
{
    QPDFObjectHandle image = imageXObject(pdf, poster);
    if (image.isNull()) return QPDFObjectHandle::newNull();

    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/Im0", image);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);

    QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type",      QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype",   QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/FormType",  QPDFObjectHandle::newInteger(1));
    dict.replaceKey("/BBox", QPDFObjectHandle::newArray({
        QPDFObjectHandle::newInteger(0), QPDFObjectHandle::newInteger(0),
        QPDFObjectHandle::newReal(width, 4), QPDFObjectHandle::newReal(height, 4) }));
    resources.replaceKey("/ProcSet", QPDFObjectHandle::newArray({
        QPDFObjectHandle::newName("/PDF"), QPDFObjectHandle::newName("/ImageC") }));
    dict.replaceKey("/Resources", resources);
    dict.replaceKey("/Matrix", QPDFObjectHandle::newArray({
        QPDFObjectHandle::newInteger(1), QPDFObjectHandle::newInteger(0),
        QPDFObjectHandle::newInteger(0), QPDFObjectHandle::newInteger(1),
        QPDFObjectHandle::newInteger(0), QPDFObjectHandle::newInteger(0) }));

    double drawW = width, drawH = height;
    if (poster.width() > 0 && poster.height() > 0) {
        const double imageAspect = double(poster.width()) / double(poster.height());
        const double boxAspect   = width / height;
        if (imageAspect > boxAspect) drawW = height * imageAspect;
        else                         drawH = width / imageAspect;
    }
    const double offsetX = (width  - drawW) / 2.0;
    const double offsetY = (height - drawH) / 2.0;

    const std::string content =
        "q " + QUtil::double_to_string(drawW, 4) + " 0 0 "
             + QUtil::double_to_string(drawH, 4) + " "
             + QUtil::double_to_string(offsetX, 4) + " "
             + QUtil::double_to_string(offsetY, 4) + " cm /Im0 Do Q";

    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&pdf);
    stream.replaceDict(dict);
    stream.replaceStreamData(content, QPDFObjectHandle::newNull(),
                             QPDFObjectHandle::newNull());
    return pdf.makeIndirectObject(stream);
}

const char *configSubtype(MediaSpec::Type type)
{
    switch (type) {
    case MediaSpec::Type::Audio: return "/Sound";
    case MediaSpec::Type::Video:
    case MediaSpec::Type::WebEmbed:
    case MediaSpec::Type::Button: break;
    }
    return "/Video";
}

std::string urlEncoded(const QString &text)
{
    QByteArray out;
    for (const char c : text.toUtf8()) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool plain = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
                        || (u >= '0' && u <= '9') || u == '.' || u == '-'
                        || u == '_' || u == '~';
        if (plain) out.append(static_cast<char>(u));
        else       out.append(QByteArray("%") + QByteArray::number(u, 16).toUpper().rightJustified(2, '0'));
    }
    return std::string(out.constData(), static_cast<size_t>(out.size()));
}

QPDFObjectHandle instanceParams(QPDF &pdf, const MediaSpec &spec)
{
    const std::string flashVars =
        "source=" + urlEncoded(spec.displayName)
        + "&volume=" + (spec.muted ? std::string("0.00") : std::string("1.00"))
        + (spec.loop ? std::string("&loop=true") : std::string());

    QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
    params.replaceKey("/Binding",   QPDFObjectHandle::newName("/Background"));
    params.replaceKey("/FlashVars", QPDFObjectHandle::newString(flashVars));
    return pdf.makeIndirectObject(params);
}

QPDFObjectHandle richMediaSettings(QPDF &pdf, const MediaSpec &spec,
                                   QPDFObjectHandle configuration)
{

    QPDFObjectHandle presentation = QPDFObjectHandle::newDictionary();
    presentation.replaceKey("/Type",  QPDFObjectHandle::newName("/RichMediaPresentation"));
    presentation.replaceKey("/Style",
        QPDFObjectHandle::newName(spec.floating ? "/Windowed" : "/Embedded"));
    presentation.replaceKey("/Toolbar",        QPDFObjectHandle::newBool(spec.showControls));
    presentation.replaceKey("/NavigationPane", QPDFObjectHandle::newBool(false));
    presentation.replaceKey("/PassContextClick", QPDFObjectHandle::newBool(false));
    presentation.replaceKey("/Transparent",    QPDFObjectHandle::newBool(false));

    if (spec.floating) {
        QPDFObjectHandle width = QPDFObjectHandle::newDictionary();
        width.replaceKey("/Default", QPDFObjectHandle::newInteger(480));
        width.replaceKey("/Min",     QPDFObjectHandle::newInteger(120));
        width.replaceKey("/Max",     QPDFObjectHandle::newInteger(1920));
        QPDFObjectHandle height = QPDFObjectHandle::newDictionary();
        height.replaceKey("/Default", QPDFObjectHandle::newInteger(270));
        height.replaceKey("/Min",     QPDFObjectHandle::newInteger(80));
        height.replaceKey("/Max",     QPDFObjectHandle::newInteger(1080));
        QPDFObjectHandle window = QPDFObjectHandle::newDictionary();
        window.replaceKey("/Type",   QPDFObjectHandle::newName("/RichMediaWindow"));
        window.replaceKey("/Width",  width);
        window.replaceKey("/Height", height);
        presentation.replaceKey("/Window", window);
    }

    QPDFObjectHandle activation = QPDFObjectHandle::newDictionary();
    activation.replaceKey("/Type", QPDFObjectHandle::newName("/RichMediaActivation"));

    activation.replaceKey("/Condition",
        QPDFObjectHandle::newName(spec.activateOnPageOpen ? "/PO" : "/XA"));
    activation.replaceKey("/Configuration", configuration);
    activation.replaceKey("/Presentation",  presentation);

    QPDFObjectHandle deactivation = QPDFObjectHandle::newDictionary();
    deactivation.replaceKey("/Type", QPDFObjectHandle::newName("/RichMediaDeactivation"));
    deactivation.replaceKey("/Condition", QPDFObjectHandle::newName("/XD"));

    QPDFObjectHandle settings = QPDFObjectHandle::newDictionary();
    settings.replaceKey("/Activation",   activation);
    settings.replaceKey("/Deactivation", deactivation);
    return settings;
}

QPDFObjectHandle richMediaContent(QPDF &pdf, const MediaSpec &spec,
                                  QPDFObjectHandle filespec,
                                  QPDFObjectHandle *configuration)
{
    QPDFObjectHandle instance = QPDFObjectHandle::newDictionary();
    instance.replaceKey("/Asset",  filespec);
    instance.replaceKey("/Params", instanceParams(pdf, spec));

    QPDFObjectHandle instances = pdf.makeIndirectObject(
        QPDFObjectHandle::newArray({ pdf.makeIndirectObject(instance) }));

    QPDFObjectHandle config = QPDFObjectHandle::newDictionary();
    config.replaceKey("/Subtype",   QPDFObjectHandle::newName(configSubtype(spec.type)));
    config.replaceKey("/Instances", instances);
    *configuration = pdf.makeIndirectObject(config);

    QPDFObjectHandle assets = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
    assets.replaceKey("/Names", QPDFObjectHandle::newArray({
        QPDFObjectHandle::newUnicodeString(spec.displayName.toStdString()),
        filespec }));

    QPDFObjectHandle configurations = pdf.makeIndirectObject(
        QPDFObjectHandle::newArray({ *configuration }));

    QPDFObjectHandle content = QPDFObjectHandle::newDictionary();
    content.replaceKey("/Assets",         assets);
    content.replaceKey("/Configurations", configurations);
    return pdf.makeIndirectObject(content);
}

QImage posterFor(const MediaSpec &spec, const QSize &pixelSize)
{
    if (!spec.poster.isNull()) return spec.poster;
    if (spec.type == MediaSpec::Type::Video) {
        const QImage grabbed = PosterFrame::grab(spec.source, pixelSize.width());
        if (!grabbed.isNull()) return grabbed;
    }
    return PosterFrame::placeholder(pixelSize);
}

int nextAnnotNumber(std::vector<QPDFPageObjectHelper> &pages)
{
    int highest = 0;
    for (QPDFPageObjectHelper &page : pages) {
        QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
        if (!annots.isArray()) continue;
        for (int i = 0; i < annots.getArrayNItems(); ++i) {
            QPDFObjectHandle item = annots.getArrayItem(i);
            if (!item.isDictionary()) continue;
            QPDFObjectHandle name = item.getKey("/NM");
            if (!name.isString()) continue;
            const QString text = QString::fromStdString(name.getUTF8Value());
            if (!text.startsWith(QLatin1String("RM"))) continue;
            bool ok = false;
            const int number = text.mid(2).toInt(&ok);
            if (ok && number > highest) highest = number;
        }
    }
    return highest + 1;
}

QPDFPageObjectHelper addBlankPage(QPDF &pdf, std::vector<QPDFPageObjectHelper> &pages,
                                  int after, const QSizeF &sizePt)
{
    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type",     QPDFObjectHandle::newName("/Page"));
    page.replaceKey("/MediaBox", QPDFObjectHandle::newArray({
        QPDFObjectHandle::newInteger(0), QPDFObjectHandle::newInteger(0),
        QPDFObjectHandle::newReal(sizePt.width(),  4),
        QPDFObjectHandle::newReal(sizePt.height(), 4) }));
    page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());

    page.replaceKey("/Contents", pdf.makeIndirectObject(
        QPDFObjectHandle::newStream(&pdf, std::string())));

    QPDFPageObjectHelper helper(pdf.makeIndirectObject(page));
    QPDFPageDocumentHelper document(pdf);
    if (after >= 0 && after < static_cast<int>(pages.size()))
        document.addPageAt(helper,  false, pages[static_cast<size_t>(after)]);
    else
        document.addPage(helper,  false);

    pages = document.getAllPages();
    return helper;
}

bool insertOne(QPDF &pdf, std::vector<QPDFPageObjectHelper> &pages,
               const MediaSpec &spec)
{
    if (spec.page < 0 || spec.page >= static_cast<int>(pages.size())) {
        qWarning() << "[rich-media] no such page:" << spec.page;
        return false;
    }
    if (!QFileInfo::exists(spec.source)) {
        qWarning() << "[rich-media] source missing:" << spec.source;
        return false;
    }

    MediaSpec placement = spec;
    if (spec.ownPage) {
        addBlankPage(pdf, pages, spec.page, spec.pageSizePt);
        placement.page   = spec.page + 1;
        placement.bounds = QRectF(QPointF(0, 0), spec.pageSizePt);
    }

    QPDFPageObjectHelper &page = pages[static_cast<size_t>(placement.page)];
    const PageBox box = pageBox(page);
    if (!box.valid) return false;

    QPDFEFStreamObjectHelper ef = QPDFEFStreamObjectHelper::createEFStream(
        pdf, QUtil::file_provider(spec.source.toLocal8Bit().constData()));
    if (!spec.mimeType.isEmpty()) ef.setSubtype(spec.mimeType.toStdString());
    ef.setModDate(QUtil::qpdf_time_to_pdf_time(
        QUtil::get_current_qpdf_time()));

    QPDFFileSpecObjectHelper filespec = QPDFFileSpecObjectHelper::createFileSpec(
        pdf, spec.displayName.toStdString(), ef);

    QPDFObjectHandle efDict = filespec.getObjectHandle().getKey("/EF");
    if (efDict.isDictionary()) efDict.removeKey("/UF");

    const double width  = placement.bounds.width();
    const double height = placement.bounds.height();

    const QSize posterPixels(qBound(160, qRound(width  * 2.0), 1600),
                             qBound(90,  qRound(height * 2.0), 1600));
    QPDFObjectHandle appearance =
        appearanceStream(pdf, posterFor(spec, posterPixels), width, height);

    QPDFObjectHandle configuration = QPDFObjectHandle::newNull();
    QPDFObjectHandle content =
        richMediaContent(pdf, spec, filespec.getObjectHandle(), &configuration);

    QPDFObjectHandle annot = QPDFObjectHandle::newDictionary();
    annot.replaceKey("/Type",    QPDFObjectHandle::newName("/Annot"));
    annot.replaceKey("/Subtype", QPDFObjectHandle::newName("/RichMedia"));
    annot.replaceKey("/Rect",    toPdfRect(placement.bounds, box));

    annot.replaceKey("/F",       QPDFObjectHandle::newInteger(68));

    annot.replaceKey("/NM", QPDFObjectHandle::newString(
        ("RM" + std::to_string(nextAnnotNumber(pages))).c_str()));
    annot.replaceKey("/P",       page.getObjectHandle());

    QPDFObjectHandle border = QPDFObjectHandle::newDictionary();
    border.replaceKey("/Type", QPDFObjectHandle::newName("/Border"));
    border.replaceKey("/S",    QPDFObjectHandle::newName("/S"));
    border.replaceKey("/W",    QPDFObjectHandle::newInteger(0));
    annot.replaceKey("/BS",     border);
    annot.replaceKey("/Border", QPDFObjectHandle::newArray({
        QPDFObjectHandle::newInteger(0), QPDFObjectHandle::newInteger(0),
        QPDFObjectHandle::newInteger(0) }));
    if (!appearance.isNull()) {
        QPDFObjectHandle ap = QPDFObjectHandle::newDictionary();
        ap.replaceKey("/N", appearance);
        annot.replaceKey("/AP", ap);
    }
    annot.replaceKey("/RichMediaContent",  content);
    annot.replaceKey("/RichMediaSettings", richMediaSettings(pdf, spec, configuration));

    QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
    if (!annots.isArray()) {
        annots = QPDFObjectHandle::newArray();
        page.getObjectHandle().replaceKey("/Annots", annots);
    }
    annots.appendItem(pdf.makeIndirectObject(annot));
    return true;
}

void removeOne(std::vector<QPDFPageObjectHelper> &pages, const MediaAsset &asset)
{
    if (asset.page < 0 || asset.page >= static_cast<int>(pages.size())) return;
    QPDFObjectHandle annots =
        pages[static_cast<size_t>(asset.page)].getObjectHandle().getKey("/Annots");
    if (!annots.isArray()) return;

    for (int i = annots.getArrayNItems() - 1; i >= 0; --i) {
        QPDFObjectHandle item = annots.getArrayItem(i);
        if (!item.isIndirect()) continue;
        const QPDFObjGen og = item.getObjGen();
        if (og.getObj() == asset.annotObject && og.getGen() == asset.annotGeneration)
            annots.eraseItem(i);
    }

}

}

bool RichMediaWriter::apply(const QString &pdfPath, const MediaSession &session,
                            const QString &password)
{
    if (session.isEmpty()) return true;
    if (!QFileInfo::exists(pdfPath)) return false;

    const QString outPath = pdfPath + QStringLiteral(".media");
    QFile::remove(outPath);

    try {
        QPDF pdf;
        const std::string pw = password.toStdString();
        pdf.processFile(pdfPath.toLocal8Bit().constData(),
                        pw.empty() ? nullptr : pw.c_str());

        QPDFPageDocumentHelper helper(pdf);
        std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();

        for (const MediaAsset &asset : session.removals())
            removeOne(pages, asset);

        for (const MediaSpec &spec : session.inserts())
            if (!insertOne(pdf, pages, spec)) {
                QFile::remove(outPath);
                return false;
            }

        QPDFWriter writer(pdf, outPath.toLocal8Bit().constData());

        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setObjectStreamMode(qpdf_o_preserve);
        writer.write();
    } catch (const std::exception &e) {
        qWarning() << "[rich-media] write failed:" << e.what();
        QFile::remove(outPath);
        return false;
    }

    if (!QFile::remove(pdfPath) || !QFile::rename(outPath, pdfPath)) {
        qWarning() << "[rich-media] could not move" << outPath << "into place of"
                   << pdfPath;
        QFile::remove(outPath);
        return false;
    }
    return true;
}

#else

bool RichMediaWriter::apply(const QString &, const MediaSession &, const QString &)
{
    return false;
}

#endif
