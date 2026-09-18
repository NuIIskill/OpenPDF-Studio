#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QDebug>
#include <QPalette>
#include <QStyleHints>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

namespace Theme {

bool   DarkMode    = false;
QColor Primary      { "#2563EB" };
QColor IconNormal   { "#374151" };
QColor IconMuted    { "#6B7280" };
QColor IconChecked  { "#2563EB" };
QColor IconDisabled { "#D1D5DB" };

QString loadStyleSheet()
{
    const QString path = DarkMode ? QStringLiteral(":/theme/Style.dark.qss")
                                  : QStringLiteral(":/theme/Style.qss");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Theme: could not open" << path;
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

    QByteArray data = f.readAll();
    data.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QByteArray buf = data + '\0';

    NSVGimage *image = nsvgParse(buf.data(), "px", 96.0f);
    if (!image) {
        qWarning() << "Theme: nsvgParse failed for:" << name;
        return {};
    }

    const int physSize = qRound(size * dpr);

    const float viewBox = image->width > 0.0f ? image->width : 24.0f;
    const float scale = static_cast<float>(physSize) / viewBox;

    NSVGrasterizer *rast = nsvgCreateRasterizer();
    QByteArray pixels(physSize * physSize * 4, '\0');

    nsvgRasterize(rast, image, 0.0f, 0.0f, scale,
                  reinterpret_cast<unsigned char *>(pixels.data()),
                  physSize, physSize, physSize * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    QImage img(reinterpret_cast<const uchar *>(pixels.constData()),
               physSize, physSize,
               physSize * 4,
               QImage::Format_RGBA8888);
    img.setDevicePixelRatio(dpr);

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

void apply(const QString &mode)
{
    const bool isDark = (mode == QStringLiteral("dark")) ||
        (mode != QStringLiteral("light") &&
         QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);

    DarkMode = isDark;
    if (isDark) {
        IconNormal   = QColor(QStringLiteral("#B8B8B8"));
        IconMuted    = QColor(QStringLiteral("#787878"));
        IconChecked  = QColor(QStringLiteral("#EEEEEE"));
        IconDisabled = QColor(QStringLiteral("#484848"));
    } else {
        IconNormal   = QColor(QStringLiteral("#374151"));
        IconMuted    = QColor(QStringLiteral("#6B7280"));
        IconChecked  = QColor(QStringLiteral("#2563EB"));
        IconDisabled = QColor(QStringLiteral("#D1D5DB"));
    }

    QPalette p;
    if (isDark) {

        p.setColor(QPalette::Window,          QColor(0x35, 0x35, 0x35));
        p.setColor(QPalette::WindowText,      QColor(0xD8, 0xD8, 0xD8));
        p.setColor(QPalette::Base,            QColor(0x2B, 0x2B, 0x2B));
        p.setColor(QPalette::AlternateBase,   QColor(0x3E, 0x3E, 0x3E));
        p.setColor(QPalette::Text,            QColor(0xD8, 0xD8, 0xD8));
        p.setColor(QPalette::BrightText,      QColor(0xF0, 0xF0, 0xF0));
        p.setColor(QPalette::Button,          QColor(0x35, 0x35, 0x35));
        p.setColor(QPalette::ButtonText,      QColor(0xD8, 0xD8, 0xD8));
        p.setColor(QPalette::Highlight,       QColor(0x25, 0x63, 0xEB));
        p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
        p.setColor(QPalette::Mid,             QColor(0x48, 0x48, 0x48));
        p.setColor(QPalette::Dark,            QColor(0x55, 0x55, 0x55));
        p.setColor(QPalette::Shadow,          QColor(0x1A, 0x1A, 0x1A));
        p.setColor(QPalette::ToolTipBase,     QColor(0x3E, 0x3E, 0x3E));
        p.setColor(QPalette::ToolTipText,     QColor(0xD8, 0xD8, 0xD8));
        p.setColor(QPalette::PlaceholderText, QColor(0x77, 0x77, 0x77));
    } else {
        p.setColor(QPalette::Window,          QColor(0xF3, 0xF4, 0xF6));
        p.setColor(QPalette::WindowText,      QColor(0x11, 0x18, 0x27));
        p.setColor(QPalette::Base,            Qt::white);
        p.setColor(QPalette::AlternateBase,   QColor(0xF9, 0xFA, 0xFB));
        p.setColor(QPalette::Text,            QColor(0x11, 0x18, 0x27));
        p.setColor(QPalette::BrightText,      Qt::white);
        p.setColor(QPalette::Button,          Qt::white);
        p.setColor(QPalette::ButtonText,      QColor(0x11, 0x18, 0x27));
        p.setColor(QPalette::Highlight,       QColor(0x25, 0x63, 0xEB));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Mid,             QColor(0xE5, 0xE7, 0xEB));
        p.setColor(QPalette::Dark,            QColor(0xD1, 0xD5, 0xDB));
        p.setColor(QPalette::Shadow,          QColor(0x9C, 0xA3, 0xAF));
        p.setColor(QPalette::ToolTipBase,     Qt::white);
        p.setColor(QPalette::ToolTipText,     QColor(0x11, 0x18, 0x27));
        p.setColor(QPalette::PlaceholderText, QColor(0x9C, 0xA3, 0xAF));
    }

    const QColor dimText  = isDark ? QColor(0x6E, 0x6E, 0x6E) : QColor(0x9C, 0xA3, 0xAF);
    const QColor dimFill  = isDark ? QColor(0x30, 0x30, 0x30) : QColor(0xF3, 0xF4, 0xF6);
    for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text,
                                      QPalette::ButtonText, QPalette::HighlightedText })
        p.setColor(QPalette::Disabled, role, dimText);
    p.setColor(QPalette::Disabled, QPalette::Base,      dimFill);
    p.setColor(QPalette::Disabled, QPalette::Button,    dimFill);
    p.setColor(QPalette::Disabled, QPalette::Highlight, isDark ? QColor(0x3A, 0x3A, 0x3A)
                                                              : QColor(0xD1, 0xD5, 0xDB));

    QApplication::setPalette(p);
    qApp->setStyleSheet(loadStyleSheet());
}

}
