#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfBackend.hpp"

#include "fpdf_text.h"
#include "fpdfview.h"

#include <memory>
#include <mutex>
#include <vector>

class PdfiumForms;
class PdfiumScanText;

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
    DocumentInfo::Permissions permissions() const override;
    bool isOwnerPassword(const QString &file, const QString &password) const override;
    QList<Link> pageLinks(int page) const override;
    QList<Note> pageNotes(int page) const override;
    bool hasForms() const override;
    QList<FormWidget> pageForms(int page) const override;
    Selection sentenceAt(int page, const QPointF &pdfPt) const override;
    QList<PageImage> pageImages(int page) const override;
    QImage pageImage(int page, const QList<int> &key) const override;
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
    QImage renderEditText(int page, qreal scale, const QList<EditSession::Edit> &edits,
                          const QRect &pixels) const override;

    std::unique_ptr<ContentProvider> makeContentProvider() const override;

    bool saveWithEdits(const WriteGrant &output, const EditSession &session,
                       const std::function<bool()> &cancelled = {}) const override;

    TextBlock textAt(int page, const QPointF &pdfPt,
                     const QList<QRectF> &exclude = {}) const override;
    TextBlock blockInRect(int page, const QRectF &rect,
                          const QList<QRectF> &exclude = {}) const override;
    QList<QRectF> glyphRects(int page, const QRectF &area,
                             const QList<QRectF> &exclude = {}) const override;
    QString embeddedFontFamily(int page, const QPointF &pdfPt) const override;
    bool    exactFontAt(int page, const QPointF &pdfPt) const override;
    double  textWidthPt(int page, const QPointF &pdfPt,
                        const QString &text, double sizePt) const override;
    double  standardTextWidthPt(const QString &family, bool bold, bool italic,
                                const QString &text, double sizePt) const override;
    bool    canEmbedFont(const QString &family, bool bold,
                         bool italic) const override;
    TextLayout::Metrics editMetrics(const EditSession::Edit &edit) const override;
    QList<TextLayout::OriginalLine> editLines(const EditSession::Edit &edit,
                                              QList<QList<TextLayout::Source>> *sources,
                                              QList<QList<TextLayout::Mark>> *marks = nullptr) const override;
    QList<TextLayout::OriginalLine> originalLines(int page, const QRectF &area,
                                                  const QList<QRectF> &exclude,
                                                  const QTransform &frame = {}) const override;
    bool    hasSelectableText(int page) const override;
    bool    hiddenText(int page, const QRectF &area) const override;

    QList<TextMatch> findText(const QString &text) const override;

    QList<TextMatch> findTextOnPage(int page, const QString &text) const override;
    void setScanText(int page, const QList<ScanLine> &lines) override;
    bool hasScanText(int page) const override;
    QList<TextMatch> findScanText(int page, const QString &text) const override;
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
    bool      syncForms(const QList<EditSession::FormValue> &values,
                        const QList<FormSpec> &design) const;
    QImage    renderFormPage(int page, const QSize &px, const QRect &target,
                             const EditSession &session) const;
    void      dropFormDoc() const;
    void      drawForms(FPDF_BITMAP bmp, FPDF_PAGE pg, const QRect &target, const QSize &px) const;
    void      closeShownPage(FPDF_PAGE pg) const;
    FPDF_PAGE     readPage(int page) const;
    FPDF_TEXTPAGE readText(int page) const;
    void          dropReadPages() const;
    bool          readScan(int page, FPDF_PAGE &pg, FPDF_TEXTPAGE &text) const;

    FPDF_DOCUMENT       m_doc { nullptr };
    std::vector<QSizeF> m_pageSizes;
    struct BackgroundPage { int index; quint64 state; FPDF_PAGE page; bool primed; };
    mutable std::vector<BackgroundPage> m_backgroundPages;
    struct ReadPage { int index; FPDF_PAGE page; FPDF_TEXTPAGE text; };
    mutable std::vector<ReadPage> m_readPages;
    mutable std::mutex m_metricsMutex;
    mutable QList<std::pair<EditSession::Edit, TextLayout::Metrics>> m_metricsCache;
    std::unique_ptr<PdfiumScanText> m_scanText;
    // PDFium draws form fields only through a form environment.
    std::unique_ptr<PdfiumForms>             m_docForms;
    // The form is filled in a second load of the file, so the shown document stays as read.
    mutable FPDF_DOCUMENT                    m_formDoc { nullptr };
    mutable std::unique_ptr<PdfiumForms>     m_forms;
    mutable QList<EditSession::FormValue>    m_formApplied;
    mutable QList<FormSpec>                  m_formDesigned;
    mutable QByteArray                       m_formBytes;
};

#endif
