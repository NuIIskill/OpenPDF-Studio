#pragma once

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentInfo.hpp"
#include "engine/document/PdfBookmark.hpp"
#include "engine/edit/EditSession.hpp"
#include "engine/edit/TextBlock.hpp"
#include "engine/edit/TextLayout.hpp"

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class ContentProvider;

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

    virtual DocumentInfo::Permissions permissions() const { return {}; }
    virtual bool isOwnerPassword(const QString &file, const QString &password) const
    { Q_UNUSED(file) Q_UNUSED(password) return false; }

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

    /// An image object of a page, keyed by its place among the page's objects.
    struct PageImage {
        QList<int> key;
        QRectF     bounds;
    };

    virtual QList<PageImage> pageImages(int page) const { Q_UNUSED(page) return {}; }
    virtual QImage pageImage(int page, const QList<int> &key) const
    { Q_UNUSED(page) Q_UNUSED(key) return {}; }

    virtual QSizeF pageSizePts(int page) const = 0;

    virtual QSize pixelSize(int page, qreal scale) const = 0;

    virtual QImage renderPage(int page, qreal scale) const = 0;

    virtual QImage renderPage(int page, qreal scale,
                              const EditSession *session) const = 0;

    virtual QImage renderPageInBackground(int page, qreal scale, const EditSession *session,
                                          const std::function<bool()> &cancelled,
                                          const QRect &area = {}, quint64 state = 0) const
    {
        Q_UNUSED(cancelled) Q_UNUSED(state)
        const QImage image = renderPage(page, scale, session);
        return area.isEmpty() ? image : image.copy(area);
    }

    /// A render of the part of a page that differs from the unedited page.
    struct AreaRender {
        QImage image;
        QRect  pixels;
        QRect  changed;
    };

    virtual AreaRender renderChanges(int page, qreal scale, const EditSession *session,
                                     const QRect &include, bool wholePage) const = 0;

    virtual QImage renderEditText(int page, qreal scale, const QList<EditSession::Edit> &edits,
                                  const QRect &pixels) const;

    virtual std::unique_ptr<ContentProvider> makeContentProvider() const = 0;

    virtual bool saveWithEdits(const QString &outputPath, const EditSession &session,
                               const std::function<bool()> &cancelled = {}) const = 0;

    virtual TextBlock textAt(int page, const QPointF &pdfPt,
                             const QList<QRectF> &exclude = {}) const = 0;

    virtual TextBlock blockInRect(int page, const QRectF &rect,
                                  const QList<QRectF> &exclude = {}) const = 0;

    struct Selection {
        QList<QRectF> rects;
        QString       text;
        QList<TextLayout::Source> sources;
        QList<TextLayout::OriginalLine> lines;
    };

    struct TextMatch {
        int           page { -1 };
        QList<QRectF> rects;
    };

    virtual QList<TextMatch> findText(const QString &text) const = 0;
    virtual QList<TextMatch> findTextOnPage(int page, const QString &text) const = 0;

    virtual Selection selectPage(int page,
                                 const std::optional<QPointF> &from,
                                 const std::optional<QPointF> &to) const = 0;

    virtual bool hasSelectableText(int page) const = 0;
    virtual bool hiddenText(int page, const QRectF &area) const;

    virtual QString embeddedFontFamily(int page, const QPointF &pdfPt) const;

    virtual bool exactFontAt(int page, const QPointF &pdfPt) const;

    virtual double textWidthPt(int page, const QPointF &pdfPt,
                               const QString &text, double sizePt) const;

    virtual double standardTextWidthPt(const QString &family, bool bold,
                                       bool italic, const QString &text,
                                       double sizePt) const;

    virtual bool canEmbedFont(const QString &family, bool bold,
                              bool italic) const;

    virtual TextLayout::Metrics editMetrics(const EditSession::Edit &edit) const;
    virtual QList<TextLayout::OriginalLine> editLines(const EditSession::Edit &edit,
                                                      QList<QList<TextLayout::Source>> *sources,
                                                      QList<QList<TextLayout::Mark>> *marks = nullptr) const;

    virtual QList<TextLayout::OriginalLine> originalLines(int page, const QRectF &area,
                                                          const QList<QRectF> &exclude) const;

    virtual QList<QRectF> glyphRects(int page, const QRectF &area,
                                     const QList<QRectF> &exclude = {}) const = 0;

protected:
    QString m_path;
};

#endif
