#pragma once

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBookmark.hpp"
#include "engine/edit/TextBlock.hpp"

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class ContentProvider;
class EditSession;

/// Defines document operations for one open PDF.
class PdfBackend
{
public:

    using PasswordAsker =
        std::function<std::optional<QString>(const QString &file, bool retry)>;

    virtual ~PdfBackend() = default;

    static std::unique_ptr<PdfBackend> create();

    virtual QString name() const = 0;

    virtual bool open(const QString &path, const PasswordAsker &ask) = 0;

    virtual void close() = 0;

    QString path() const { return m_path; }
    bool    isOpen() const { return !m_path.isEmpty(); }

    virtual int pageCount() const = 0;

    virtual QList<PdfBookmark> bookmarks() const { return {}; }

    struct Link {
        QRectF  bounds;
        QString url;
        QList<QRectF> textRects;
        bool styledByOpenPdf { false };
    };

    virtual QList<Link> pageLinks(int page) const { Q_UNUSED(page) return {}; }

    struct Note {
        QString id;
        QString title;
        QString text;
        QRectF  bounds;
        bool    pinned { false };
    };

    virtual QList<Note> pageNotes(int page) const { Q_UNUSED(page) return {}; }

    virtual QSizeF pageSizePts(int page) const = 0;

    virtual QSize pixelSize(int page, qreal scale) const = 0;

    virtual QImage renderPage(int page, qreal scale) const = 0;

    virtual QImage renderPage(int page, qreal scale,
                              const EditSession *session) const;

    virtual std::unique_ptr<ContentProvider> makeContentProvider() const = 0;

    virtual bool saveWithEdits(const QString &outputPath,
                               const EditSession &session) const = 0;

    virtual TextBlock textAt(int page, const QPointF &pdfPt,
                             const QList<QRectF> &exclude = {}) const = 0;

    virtual TextBlock blockInRect(int page, const QRectF &rect,
                                  const QList<QRectF> &exclude = {}) const = 0;

    struct Selection {
        QList<QRectF> rects;
        QString       text;
    };

    struct TextMatch {
        int           page { -1 };
        QList<QRectF> rects;
    };

    virtual QList<TextMatch> findText(const QString &text) const = 0;

    virtual Selection selectPage(int page,
                                 const std::optional<QPointF> &from,
                                 const std::optional<QPointF> &to) const = 0;

    virtual bool hasSelectableText(int page) const = 0;

    virtual QString embeddedFontFamily(int page, const QPointF &pdfPt) const;

    virtual double textWidthPt(int page, const QPointF &pdfPt,
                               const QString &text, double sizePt) const;

    virtual double standardTextWidthPt(const QString &family, bool bold,
                                       bool italic, const QString &text,
                                       double sizePt) const;

    virtual bool canEmbedFont(const QString &family, bool bold,
                              bool italic) const;

    virtual QList<QRectF> glyphRects(int page, const QRectF &area,
                                     const QList<QRectF> &exclude = {}) const = 0;

protected:
    QString m_path;
};

#endif
