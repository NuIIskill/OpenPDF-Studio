#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfBackend.hpp"

#include "fpdf_text.h"
#include "fpdfview.h"

#include <mutex>
#include <vector>

struct PdfiumChar;
struct PdfiumLine;

/// Implements PDF document operations with PDFium.
class PdfiumBackend : public PdfBackend
{
public:
    PdfiumBackend();
    ~PdfiumBackend() override;

    QString name() const override { return QStringLiteral("PDFium"); }

    bool open(const QString &path, const PasswordAsker &ask) override;
    void close() override;

    int    pageCount() const override;
    QList<PdfBookmark> bookmarks() const override;
    QList<Link> pageLinks(int page) const override;
    QList<Note> pageNotes(int page) const override;
    QSizeF pageSizePts(int page) const override;
    QSize  pixelSize(int page, qreal scale) const override;
    QImage renderPage(int page, qreal scale) const override;
    QImage renderPage(int page, qreal scale,
                      const EditSession *session) const override;
    QImage renderPageInBackground(int page, qreal scale, const EditSession *session,
                                  const std::function<bool()> &cancelled,
                                  const QRect &area = {}, quint64 state = 0) const override;

    AreaRender renderChanges(int page, qreal scale, const EditSession *session,
                             const QRect &include, bool wholePage) const override;

    std::unique_ptr<ContentProvider> makeContentProvider() const override;

    bool saveWithEdits(const QString &outputPath, const EditSession &session,
                       const std::function<bool()> &cancelled = {}) const override;

    TextBlock textAt(int page, const QPointF &pdfPt,
                     const QList<QRectF> &exclude = {}) const override;
    TextBlock blockInRect(int page, const QRectF &rect,
                          const QList<QRectF> &exclude = {}) const override;
    QList<QRectF> glyphRects(int page, const QRectF &area,
                             const QList<QRectF> &exclude = {}) const override;
    QString embeddedFontFamily(int page, const QPointF &pdfPt) const override;
    double  textWidthPt(int page, const QPointF &pdfPt,
                        const QString &text, double sizePt) const override;
    double  standardTextWidthPt(const QString &family, bool bold, bool italic,
                                const QString &text, double sizePt) const override;
    bool    canEmbedFont(const QString &family, bool bold,
                         bool italic) const override;
    TextLayout::Metrics editMetrics(const EditSession::Edit &edit) const override;
    QList<TextLayout::OriginalLine> originalLines(int page, const QRectF &area,
                                                  const QList<QRectF> &exclude) const override;
    bool    hasSelectableText(int page) const override;

    QList<TextMatch> findText(const QString &text) const override;

    QList<TextMatch> findTextOnPage(int page, const QString &text) const override;
    Selection selectPage(int page, const std::optional<QPointF> &from,
                         const std::optional<QPointF> &to) const override;

private:
    enum class LineSplit {
        Baseline,
        Blocks
    };

    QImage renderPageInternal(int page, qreal scale, const EditSession *session,
                              const std::function<bool()> *cancelled,
                              const QRect &area = {}, quint64 state = 0) const;

    std::vector<PdfiumLine> linesOfPage(int page, const QList<QRectF> &exclude,
                                        const std::optional<QPointF> &from,
                                        const std::optional<QPointF> &to,
                                        LineSplit split) const;

    static std::vector<PdfiumLine> buildLines(const std::vector<PdfiumChar> &chars,
                                              int first, int last, LineSplit split,
                                              const QList<QRectF> &textObjects);

    FPDF_PAGE backgroundPage(int page, quint64 state = 0,
                             const EditSession *session = nullptr) const;
    bool      primeImages(int page, FPDF_PAGE pg, const std::function<bool()> &cancelled) const;
    void      dropBackgroundPages() const;
    FPDF_PAGE     readPage(int page) const;
    FPDF_TEXTPAGE readText(int page) const;
    void          dropReadPages() const;

    FPDF_DOCUMENT       m_doc { nullptr };
    std::vector<QSizeF> m_pageSizes;
    struct BackgroundPage { int index; quint64 state; FPDF_PAGE page; bool primed; };
    mutable std::vector<BackgroundPage> m_backgroundPages;
    struct ReadPage { int index; FPDF_PAGE page; FPDF_TEXTPAGE text; };
    mutable std::vector<ReadPage> m_readPages;
    mutable std::mutex m_metricsMutex;
    mutable QList<std::pair<EditSession::Edit, TextLayout::Metrics>> m_metricsCache;
};

#endif
