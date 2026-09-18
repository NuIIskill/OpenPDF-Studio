#include "engine/import/ImageImport.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>

namespace {

// A page cannot be larger than 200 inches in either direction.
constexpr double MaxPageSidePt = 14400.0;
constexpr double FallbackDpi   = 96.0;
constexpr qint64 MaxPixels     = 200LL * 1000 * 1000;

QString tr(const char *text) { return QCoreApplication::translate("DocumentImport", text); }

/// Physical size in points, from the image's own resolution where it has one.
QSizeF pageSizePt(const QSize &pixels, int dotsPerMeterX, int dotsPerMeterY)
{
    auto dpi = [](int dotsPerMeter) {
        const double value = dotsPerMeter * 0.0254;
        return (value >= 20.0 && value <= 2400.0) ? value : FallbackDpi;
    };

    QSizeF size(pixels.width()  / dpi(dotsPerMeterX) * 72.0,
                pixels.height() / dpi(dotsPerMeterY) * 72.0);

    const double longest = qMax(size.width(), size.height());
    if (longest > MaxPageSidePt)
        size *= MaxPageSidePt / longest;
    return size.expandedTo(QSizeF(1.0, 1.0));
}

/// Deflate for a PDF stream. qCompress hands back a zlib stream behind a four
/// byte length, and a zlib stream is exactly what /FlateDecode wants.
QByteArray flate(const QByteArray &data)
{
    const QByteArray compressed = qCompress(data, 9);
    return compressed.size() > 4 ? compressed.mid(4) : QByteArray();
}

/// Rows without the padding QImage keeps between them.
QByteArray packedRgb(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_RGB888);
    const int width  = image.width();
    const int height = image.height();

    QByteArray out;
    out.reserve(static_cast<qsizetype>(width) * height * 3);
    for (int y = 0; y < height; ++y)
        out.append(reinterpret_cast<const char *>(image.constScanLine(y)), width * 3);
    return out;
}

struct Jpeg {
    QByteArray data;
    int components    { 0 };
    int dotsPerMeterX { 0 };
    int dotsPerMeterY { 0 };
};

/// The bytes of a baseline JPEG can go into the PDF untouched. Progressive
/// files and anything but grey or colour are decoded instead, because no viewer
/// has to support those behind /DCTDecode.
Jpeg jpegPassthrough(const QString &path)
{
    Jpeg jpeg;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray data = file.readAll();
    if (!data.startsWith("\xFF\xD8")) return {};

    for (qsizetype at = 2; at + 4 <= data.size();) {
        if (static_cast<quint8>(data[at]) != 0xFF) return {};
        const quint8 marker = static_cast<quint8>(data[at + 1]);
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            at += 2;
            continue;
        }
        const int length = (static_cast<quint8>(data[at + 2]) << 8)
                         |  static_cast<quint8>(data[at + 3]);
        if (length < 2 || at + 2 + length > data.size()) return {};

        // The JFIF header carries the resolution, which is the only way to get
        // it without decoding the whole thing.
        if (marker == 0xE0 && length >= 16 && data.mid(at + 4, 5) == "JFIF\0") {
            const int  units = static_cast<quint8>(data[at + 11]);
            const auto value = [&](int offset) {
                return (static_cast<quint8>(data[at + offset]) << 8)
                     |  static_cast<quint8>(data[at + offset + 1]);
            };
            const double perMeter = units == 1 ? 1.0 / 0.0254 : units == 2 ? 100.0 : 0.0;
            jpeg.dotsPerMeterX = qRound(value(12) * perMeter);
            jpeg.dotsPerMeterY = qRound(value(14) * perMeter);
        }

        // SOF0 baseline and SOF1 extended sequential are safe, everything else
        // in the SOF range (progressive, arithmetic, lossless) is not.
        if (marker == 0xC0 || marker == 0xC1) {
            if (at + 9 >= data.size()) return {};
            jpeg.components = static_cast<quint8>(data[at + 9]);
            if (jpeg.components != 1 && jpeg.components != 3) return {};
            jpeg.data = data;
            return jpeg;
        }
        if (marker >= 0xC2 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8
                && marker != 0xCC)
            return {};
        if (marker == 0xDA) return {};
        at += 2 + length;
    }
    return {};
}

void appendObject(QByteArray &pdf, QList<qint64> &offsets, const QByteArray &body)
{
    offsets.append(pdf.size());
    pdf += QByteArray::number(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
}

QByteArray streamObject(const QByteArray &dictionary, const QByteArray &stream)
{
    return "<<" + dictionary + "/Length " + QByteArray::number(stream.size())
         + ">>\nstream\n" + stream + "\nendstream";
}

bool writeSinglePagePdf(const QString &outPath, const QSizeF &pagePt,
                        const QByteArray &image, const QByteArray &filter,
                        const QSize &pixels, int components)
{
    auto number = [](double value) {
        return QByteArray::number(value, 'f', 2);
    };
    const QByteArray width  = QByteArray::number(pixels.width());
    const QByteArray height = QByteArray::number(pixels.height());

    QByteArray pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    QList<qint64> offsets;

    appendObject(pdf, offsets, "<</Type/Catalog/Pages 2 0 R>>");
    appendObject(pdf, offsets, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
    appendObject(pdf, offsets,
        "<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + number(pagePt.width())
        + " " + number(pagePt.height()) + "]"
        "/Resources<</XObject<</Im0 4 0 R>>>>/Contents 5 0 R>>");
    appendObject(pdf, offsets, streamObject(
        "/Type/XObject/Subtype/Image/Width " + width + "/Height " + height
        + "/ColorSpace" + (components == 1 ? "/DeviceGray" : "/DeviceRGB")
        + "/BitsPerComponent 8/Filter" + filter, image));
    appendObject(pdf, offsets, streamObject({},
        "q " + number(pagePt.width()) + " 0 0 " + number(pagePt.height())
        + " 0 0 cm /Im0 Do Q"));
    appendObject(pdf, offsets, "<</Producer(OpenPDF Studio)>>");

    const qint64 startxref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(offsets.size() + 1)
         + "\n0000000000 65535 f \n";
    for (qint64 offset : offsets)
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<</Size " + QByteArray::number(offsets.size() + 1)
         + "/Root 1 0 R/Info 6 0 R>>\nstartxref\n"
         + QByteArray::number(startxref) + "\n%%EOF\n";

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(pdf) == pdf.size() && file.flush();
}

}

QStringList ImageImport::suffixes()
{
    static const QStringList list = {
        QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"),  QStringLiteral("gif"), QStringLiteral("tiff"),
        QStringLiteral("tif"),  QStringLiteral("webp")
    };
    return list;
}

bool ImageImport::isSupported(const QString &path)
{
    return suffixes().contains(QFileInfo(path).suffix().toLower());
}

QString ImageImport::nameFilter()
{
    QStringList patterns;
    for (const QString &suffix : suffixes())
        patterns += QStringLiteral("*.") + suffix;
    return tr("Images") + QStringLiteral(" (") + patterns.join(QLatin1Char(' ')) + QLatin1Char(')');
}

bool ImageImport::toPdf(const QString &imagePath, const QString &outPdf, QString *error)
{
    QImageReader reader(imagePath);
    reader.setAutoTransform(true);

    const QSize probe = reader.size();
    if (probe.isValid()
            && static_cast<qint64>(probe.width()) * probe.height() > MaxPixels) {
        if (error) *error = tr("The image is too large.");
        return false;
    }

    // An untouched baseline JPEG goes into the PDF as it is: no second round of
    // lossy encoding, and the smallest file. Anything the reader would have to
    // rotate must be decoded first.
    if (probe.isValid()
            && reader.transformation() == QImageIOHandler::TransformationNone) {
        const Jpeg jpeg = jpegPassthrough(imagePath);
        if (!jpeg.data.isEmpty()) {
            const QSizeF page = pageSizePt(probe, jpeg.dotsPerMeterX, jpeg.dotsPerMeterY);
            if (writeSinglePagePdf(outPdf, page, jpeg.data, "/DCTDecode",
                                   probe, jpeg.components))
                return true;
        }
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        if (error) *error = reader.errorString();
        return false;
    }

    // Transparency is composited onto white, the same white the page would show
    // through anyway, so the PDF needs no soft mask.
    QImage opaque = image;
    if (image.hasAlphaChannel()) {
        opaque = QImage(image.size(), QImage::Format_RGB888);
        opaque.fill(Qt::white);
        QPainter painter(&opaque);
        painter.drawImage(0, 0, image);
        painter.end();
        opaque.setDotsPerMeterX(image.dotsPerMeterX());
        opaque.setDotsPerMeterY(image.dotsPerMeterY());
    }

    const QByteArray stream = flate(packedRgb(opaque));
    if (stream.isEmpty()) {
        if (error) *error = tr("The PDF could not be written.");
        return false;
    }

    const QSizeF page = pageSizePt(opaque.size(), opaque.dotsPerMeterX(),
                                   opaque.dotsPerMeterY());
    if (!writeSinglePagePdf(outPdf, page, stream, "/FlateDecode", opaque.size(), 3)) {
        if (error) *error = tr("The PDF could not be written.");
        return false;
    }
    return true;
}
