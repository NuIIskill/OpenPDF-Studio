#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

/// One logical region detected on a PDF page.
struct ContentItem {
    enum class Type {
        Text,
        Paragraph,
        TableCell,
        FormField,
        Image,
        Media,
    };

    Type    type       { Type::Text };
    QRectF  bounds;
    QString text;
    double  fontSizePt { 0.0 };

    bool    fontSizeExact { false };
    QString fontFamily;
    QString rawFontName;
    bool    bold       { false };
    bool    italic     { false };
    QColor  textColor;
    QColor  bgColor;
    QString fieldName;

    QPointF textOrigin;

    double  lineSpacingPt { 0.0 };

    bool isFormField() const { return type == Type::FormField; }
    bool isTextual()   const {
        return type == Type::Text || type == Type::Paragraph
            || type == Type::TableCell || type == Type::FormField;
    }
    bool isValid()     const { return !bounds.isNull() && !bounds.isEmpty(); }
};

constexpr unsigned contentTypeBit(ContentItem::Type t)
{
    return 1u << static_cast<unsigned>(t);
}
constexpr unsigned kTextualContentTypes =
      contentTypeBit(ContentItem::Type::Text)
    | contentTypeBit(ContentItem::Type::Paragraph)
    | contentTypeBit(ContentItem::Type::TableCell)
    | contentTypeBit(ContentItem::Type::FormField);
constexpr unsigned kAllContentTypes = 0x3Fu;

ContentItem contentItemAt(const QList<ContentItem> &items, const QPointF &pdfPt,
                          unsigned typeMask = kTextualContentTypes,
                          double maxDistance = 40.0);

/// Stores one backend-neutral positioned text run.
struct ContentCluster {
    QRectF  bounds;
    QString text;
    double  fontSizePt { 0.0 };
    bool    fontSizeExact { false };
    QString rawFontName;
    QColor  textColor;
    bool    exactWidth { false };
    QPointF origin;
};

QList<ContentItem> classifyContentClusters(QList<ContentCluster> clusters,
                                           bool mergeVertical = true);

struct ResolvedFont {
    QString family;
    bool    bold   { false };
    bool    italic { false };
};

ResolvedFont resolvePdfFont(const QString &rawBaseFont);

#ifdef HAVE_QPDF

#include <string>
#include <qpdf/QPDFObjectHandle.hh>

QList<ContentItem> qpdfBuildPageItems(const std::string &cs, double pageH,
                                      QPDFObjectHandle pageObj);

#endif
