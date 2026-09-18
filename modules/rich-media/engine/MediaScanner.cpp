// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaScanner.hpp"

#ifdef HAVE_QPDF
#  include "app/PdfPwStore.hpp"

#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFNameTreeObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>

#  include <QDebug>
#  include <QFileInfo>

#  include <algorithm>
#  include <string>
#  include <vector>
#endif

bool MediaScanner::available()
{
#ifdef HAVE_QPDF
    return true;
#else
    return false;
#endif
}

#ifdef HAVE_QPDF

namespace {

QString nameOf(QPDFObjectHandle oh, const char *key)
{
    QPDFObjectHandle v = oh.getKey(key);
    return v.isName() ? QString::fromStdString(v.getName()) : QString();
}

QString stringOf(QPDFObjectHandle oh, const char *key)
{
    QPDFObjectHandle v = oh.getKey(key);
    return v.isString() ? QString::fromStdString(v.getUTF8Value()) : QString();
}

QPDFObjectHandle embeddedStream(QPDFObjectHandle filespec)
{
    if (!filespec.isDictionary()) return QPDFObjectHandle::newNull();
    QPDFObjectHandle ef = filespec.getKey("/EF");
    if (!ef.isDictionary()) return QPDFObjectHandle::newNull();
    for (const char *key : { "/F", "/UF", "/DOS", "/Mac", "/Unix" }) {
        QPDFObjectHandle stream = ef.getKey(key);
        if (stream.isStream()) return stream;
    }
    return QPDFObjectHandle::newNull();
}

QString filespecName(QPDFObjectHandle filespec)
{
    if (!filespec.isDictionary()) return QString();
    const QString uf = stringOf(filespec, "/UF");
    return uf.isEmpty() ? stringOf(filespec, "/F") : uf;
}

QString mimeFromStream(QPDFObjectHandle stream)
{
    if (!stream.isStream()) return QString();
    QString sub = nameOf(stream.getDict(), "/Subtype");
    if (sub.startsWith(QLatin1Char('/'))) sub.remove(0, 1);
    return sub;
}

QString mimeFromName(const QString &fileName)
{
    static const struct { const char *ext, *mime; } kTypes[] = {
        { "mp4",  "video/mp4" },        { "m4v",  "video/mp4" },
        { "mov",  "video/quicktime" },  { "webm", "video/webm" },
        { "mkv",  "video/x-matroska" }, { "avi",  "video/x-msvideo" },
        { "flv",  "video/x-flv" },      { "f4v",  "video/mp4" },
        { "mp3",  "audio/mpeg" },       { "m4a",  "audio/mp4" },
        { "wav",  "audio/wav" },        { "ogg",  "audio/ogg" },
    };
    const QString ext = QFileInfo(fileName).suffix().toLower();
    for (const auto &t : kTypes)
        if (ext == QLatin1String(t.ext)) return QLatin1String(t.mime);
    return QString();
}

qint64 declaredSize(QPDFObjectHandle stream)
{
    if (!stream.isStream()) return 0;
    QPDFObjectHandle params = stream.getDict().getKey("/Params");
    if (!params.isDictionary()) return 0;
    QPDFObjectHandle size = params.getKey("/Size");
    return size.isInteger() ? static_cast<qint64>(size.getIntValue()) : 0;
}

QPDFObjectHandle richMediaAsset(QPDFObjectHandle annot, QPDF &pdf)
{
    QPDFObjectHandle content = annot.getKey("/RichMediaContent");
    if (!content.isDictionary()) return QPDFObjectHandle::newNull();

    QPDFObjectHandle configs = content.getKey("/Configurations");
    if (configs.isArray()) {
        for (int i = 0; i < configs.getArrayNItems(); ++i) {
            QPDFObjectHandle cfg = configs.getArrayItem(i);
            if (!cfg.isDictionary()) continue;
            QPDFObjectHandle instances = cfg.getKey("/Instances");
            if (!instances.isArray()) continue;
            for (int j = 0; j < instances.getArrayNItems(); ++j) {
                QPDFObjectHandle inst = instances.getArrayItem(j);
                if (!inst.isDictionary()) continue;
                QPDFObjectHandle asset = inst.getKey("/Asset");
                if (!embeddedStream(asset).isNull()) return asset;
            }
        }
    }

    QPDFObjectHandle assets = content.getKey("/Assets");
    if (!assets.isDictionary()) return QPDFObjectHandle::newNull();
    try {
        QPDFNameTreeObjectHelper tree(assets, pdf,  false);
        for (auto it = tree.begin(); it != tree.end(); ++it)
            if (!embeddedStream(it->second).isNull()) return it->second;
    } catch (const std::exception &) {

    }
    return QPDFObjectHandle::newNull();
}

void readRichMediaOptions(QPDFObjectHandle annot, MediaAsset *asset)
{
    QPDFObjectHandle settings = annot.getKey("/RichMediaSettings");
    if (!settings.isDictionary()) return;
    QPDFObjectHandle activation = settings.getKey("/Activation");
    if (!activation.isDictionary()) return;

    asset->activateOnPageOpen = nameOf(activation, "/Condition") == QLatin1String("/PO");
    QPDFObjectHandle presentation = activation.getKey("/Presentation");
    if (presentation.isDictionary()) {
        asset->floating = nameOf(presentation, "/Style") == QLatin1String("/Windowed");
        QPDFObjectHandle toolbar = presentation.getKey("/Toolbar");
        if (toolbar.isBool()) asset->showControls = toolbar.getBoolValue();
    }

    QPDFObjectHandle content = annot.getKey("/RichMediaContent");
    if (!content.isDictionary()) return;
    QPDFObjectHandle configs = content.getKey("/Configurations");
    if (!configs.isArray() || configs.getArrayNItems() == 0) return;
    QPDFObjectHandle config = configs.getArrayItem(0);
    if (!config.isDictionary()) return;
    QPDFObjectHandle instances = config.getKey("/Instances");
    if (!instances.isArray() || instances.getArrayNItems() == 0) return;
    QPDFObjectHandle instance = instances.getArrayItem(0);
    if (!instance.isDictionary()) return;
    QPDFObjectHandle params = instance.getKey("/Params");
    if (!params.isDictionary()) return;
    const QString flashVars = stringOf(params, "/FlashVars");
    for (const QString &part : flashVars.split(QLatin1Char('&'))) {
        if (part == QLatin1String("loop=true")) asset->loop = true;
        if (part.startsWith(QLatin1String("volume=")))
            asset->muted = part.mid(7).toDouble() <= 0.0;
    }
}

void screenClip(QPDFObjectHandle annot, QPDFObjectHandle *filespec,
                QPDFObjectHandle *stream, QString *mime)
{
    *filespec = QPDFObjectHandle::newNull();
    *stream   = QPDFObjectHandle::newNull();

    QPDFObjectHandle action = annot.getKey("/A");
    if (!action.isDictionary()) return;
    QPDFObjectHandle rendition = action.getKey("/R");
    if (!rendition.isDictionary()) return;
    QPDFObjectHandle clip = rendition.getKey("/C");
    if (!clip.isDictionary()) return;

    *mime = stringOf(clip, "/CT");
    QPDFObjectHandle data = clip.getKey("/D");
    if (data.isStream())          *stream   = data;
    else if (data.isDictionary()) *filespec = data;
}

struct PageBox { double left { 0 }, top { 0 }; bool valid { false }; };

bool readRect(QPDFObjectHandle rect, double out[4])
{
    if (!rect.isArray() || rect.getArrayNItems() != 4) return false;
    for (int i = 0; i < 4; ++i) {
        QPDFObjectHandle n = rect.getArrayItem(i);
        if (!n.isNumber()) return false;
        out[i] = n.getNumericValue();
    }
    return true;
}

PageBox pageBox(QPDFPageObjectHelper &page)
{
    PageBox box;
    double v[4];
    if (!readRect(page.getCropBox(false, true), v)) return box;
    box.left  = std::min(v[0], v[2]);
    box.top   = std::max(v[1], v[3]);
    box.valid = true;
    return box;
}

QRectF toTopLeft(QPDFObjectHandle rect, const PageBox &box)
{
    double v[4];
    if (!box.valid || !readRect(rect, v)) return {};
    const double x0 = std::min(v[0], v[2]);
    const double x1 = std::max(v[0], v[2]);
    const double y0 = std::min(v[1], v[3]);
    const double y1 = std::max(v[1], v[3]);
    return QRectF(x0 - box.left, box.top - y1, x1 - x0, y1 - y0);
}

}

QList<MediaAsset> MediaScanner::scan(const QString &pdfPath)
{
    QList<MediaAsset> found;
    if (pdfPath.isEmpty() || !QFileInfo::exists(pdfPath)) return found;

    try {
        QPDF pdf;
        const std::string pw = PdfPwStore::forQpdf(pdfPath);
        pdf.processFile(pdfPath.toLocal8Bit().constData(),
                        pw.empty() ? nullptr : pw.c_str());

        QPDFPageDocumentHelper docPages(pdf);
        std::vector<QPDFPageObjectHelper> pages = docPages.getAllPages();

        for (size_t index = 0; index < pages.size(); ++index) {
            QPDFPageObjectHelper &page = pages[index];
            const PageBox box = pageBox(page);
            if (!box.valid) continue;

            QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
            if (!annots.isArray()) continue;

            for (int a = 0; a < annots.getArrayNItems(); ++a) {
                QPDFObjectHandle annot = annots.getArrayItem(a);
                if (!annot.isDictionary()) continue;
                const QString subtype = nameOf(annot, "/Subtype");

                MediaAsset asset;
                QPDFObjectHandle filespec = QPDFObjectHandle::newNull();
                QPDFObjectHandle stream   = QPDFObjectHandle::newNull();

                if (subtype == QLatin1String("/RichMedia")) {
                    asset.kind = MediaAsset::Kind::RichMedia;
                    filespec   = richMediaAsset(annot, pdf);
                    stream     = embeddedStream(filespec);
                    readRichMediaOptions(annot, &asset);
                } else if (subtype == QLatin1String("/Screen")) {
                    asset.kind = MediaAsset::Kind::Screen;
                    screenClip(annot, &filespec, &stream, &asset.mimeType);
                    if (stream.isNull()) stream = embeddedStream(filespec);
                } else if (subtype == QLatin1String("/Movie")) {
                    asset.kind = MediaAsset::Kind::Movie;
                    QPDFObjectHandle movie = annot.getKey("/Movie");
                    if (movie.isDictionary()) {
                        filespec = movie.getKey("/F");
                        stream   = embeddedStream(filespec);

                        if (filespec.isString())
                            asset.name = QString::fromStdString(filespec.getUTF8Value());
                    }
                } else {
                    continue;
                }

                asset.page   = static_cast<int>(index);
                asset.bounds = toTopLeft(annot.getKey("/Rect"), box);
                if (!asset.isValid()) continue;

                if (asset.name.isEmpty()) asset.name = filespecName(filespec);
                if (asset.name.isEmpty())
                    asset.name = QStringLiteral("media-%1").arg(index + 1);

                if (stream.isStream()) {
                    const QPDFObjGen og = stream.getObjGen();
                    asset.streamObject     = og.getObj();
                    asset.streamGeneration = og.getGen();
                    asset.size             = declaredSize(stream);
                    if (asset.mimeType.isEmpty()) asset.mimeType = mimeFromStream(stream);
                }
                if (asset.mimeType.isEmpty()) asset.mimeType = mimeFromName(asset.name);

                if (annot.isIndirect()) {
                    const QPDFObjGen og = annot.getObjGen();
                    asset.annotObject     = og.getObj();
                    asset.annotGeneration = og.getGen();
                }

                found.append(asset);
            }
        }
    } catch (const std::exception &e) {
        qWarning() << "[rich-media] scan of" << pdfPath << "failed:" << e.what();
        return {};
    }

    return found;
}

#else

QList<MediaAsset> MediaScanner::scan(const QString &)
{
    return {};
}

#endif
