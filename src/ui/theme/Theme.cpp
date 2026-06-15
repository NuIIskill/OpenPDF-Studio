#include "Theme.hpp"

#include <QFile>
#include <QImage>
#include <QDebug>

// NanoSVG — single-header SVG parser + rasterizer (no Qt::Svg needed)
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

namespace Theme {

QString loadStyleSheet()
{
    QFile file(QStringLiteral(":/theme/Style.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Theme: could not open :/theme/Style.qss";
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QPixmap renderSvg(const QString &name, const QColor &color, int size, qreal dpr)
{
    QFile f(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Theme: icon not found:" << name;
        return {};
    }

    // Patch currentColor → actual hex color (Lucide uses stroke="currentColor")
    QByteArray data = f.readAll();
    data.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    // nsvgParse modifies the buffer in-place — pass a writable copy
    QByteArray buf = data + '\0';

    NSVGimage *image = nsvgParse(buf.data(), "px", 96.0f);
    if (!image) {
        qWarning() << "Theme: nsvgParse failed for:" << name;
        return {};
    }
    qDebug() << "Theme SVG" << name << "size:" << image->width << "x" << image->height
             << "shapes:" << (image->shapes != nullptr)
             << "hasStroke:" << (image->shapes ? image->shapes->stroke.type : -1);

    const int physSize = qRound(size * dpr);
    // Lucide icons have a 24×24 viewBox — scale to physSize
    const float scale = static_cast<float>(physSize) / 24.0f;

    NSVGrasterizer *rast = nsvgCreateRasterizer();
    QByteArray pixels(physSize * physSize * 4, '\0');

    nsvgRasterize(rast, image, 0.0f, 0.0f, scale,
                  reinterpret_cast<unsigned char *>(pixels.data()),
                  physSize, physSize, physSize * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    // Debug: check if any pixel was written
    const auto *px = reinterpret_cast<const unsigned char *>(pixels.constData());
    bool hasContent = false;
    for (int i = 0; i < physSize * physSize * 4; i += 4)
        if (px[i+3] > 0) { hasContent = true; break; }
    qDebug() << "Theme rasterize" << name << physSize << "px — hasContent:" << hasContent;

    // NanoSVG outputs RGBA — matches Qt's Format_RGBA8888
    QImage img(reinterpret_cast<const uchar *>(pixels.constData()),
               physSize, physSize,
               physSize * 4,
               QImage::Format_RGBA8888);
    img.setDevicePixelRatio(dpr);

    // QImage shares the buffer — copy before pixels goes out of scope
    return QPixmap::fromImage(img.copy());
}

QIcon makeIcon(const QString &name,
               const QColor &normal,
               const QColor &checked,
               const QColor &disabled,
               int size)
{
    QIcon icon;
    for (const qreal dpr : { 1.0, 2.0 }) {
        icon.addPixmap(renderSvg(name, normal,   size, dpr), QIcon::Normal,   QIcon::Off);
        icon.addPixmap(renderSvg(name, checked,  size, dpr), QIcon::Normal,   QIcon::On);
        icon.addPixmap(renderSvg(name, disabled, size, dpr), QIcon::Disabled, QIcon::Off);
    }
    return icon;
}

} // namespace Theme
