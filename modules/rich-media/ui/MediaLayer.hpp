// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaAsset.hpp"
#include "rich-media/engine/MediaSession.hpp"
#include "rich-media/engine/MediaSpec.hpp"
#include "ui/view/PageOverlay.hpp"

#include <QHash>
#include <QList>
#include <QStringList>
#include <QObject>
#include <QPoint>
#include <QColor>
#include <QImage>
#include <QPointer>
#include <QRectF>

QT_BEGIN_NAMESPACE
class QRubberBand;
QT_END_NAMESPACE

class MediaFrame;
class PageCanvas;
class RichMediaPanel;
class MediaPlayerFrame;

/// The media layer over the pages of one document view, built through
/// PageOverlays. Keeps three things apart: media found in the document, media
/// inserted but not yet saved, and the area currently being dragged out.
///
/// Positions are held in PDF points and the widgets derived from them, so a
/// zoom is just a relayout(), as in ImageAnnotationLayer.
class MediaLayer : public QObject, public PageOverlay
{
    Q_OBJECT

public:
    explicit MediaLayer(PageCanvas *canvas, QObject *parent = nullptr);
    ~MediaLayer() override;

    // ── PageOverlay ──────────────────────────────────────────────────────────
    void setDocument(const QString &path) override;
    void setActiveTool(const QString &toolId) override;
    void relayout() override;
    bool writeTo(const QString &stagingPath) override;
    bool acceptsDroppedFile(const QString &path) const override;
    bool handleDroppedFile(const QString &path, int page,
                           QString *newDocument) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Placed {
        MediaAsset  asset;       // found media only
        MediaSpec   spec;        // pending inserts only
        bool        pending { false };
        /// Marked for removal. Stays in the list: the frame has to cover the
        /// spot until the save.
        bool        removed { false };
        int         page { -1 };
        QRectF      bounds;      // PDF points
        MediaFrame *frame { nullptr };
    };

    void rebuildFrames();
    void clearFrames();
    void addFrame(Placed placed);
    /// Puts a frame where its PDF points say it belongs.
    void positionFrame(const Placed &placed) const;

    void beginPlacement(int page, const QRectF &pdfBounds);
    void cancelPlacement();
    void commitInsert(const MediaSpec &spec);
    /// Checks the source against what plays everywhere and offers to convert
    /// it. Empty return means cancelled.
    QString ensurePlayableSource(const QString &source);

    void showMenuFor(MediaFrame *frame, const QPoint &globalPos);
    void removeFrame(MediaFrame *frame);
    void saveMediaAs(const Placed &placed);
    /// Colour a removed spot is covered with, sampled from the rendered page
    /// so the cover is not a white patch on coloured paper.
    QColor pageColorAround(int page, const QRectF &pdfBounds) const;
    /// The rendered page under a medium: what the player covers, and therefore
    /// the right still image until the first video frame arrives.
    QImage pageExcerpt(int page, const QRectF &pdfBounds) const;

    /// Asks once per document before anything plays. False = the user declined.
    bool confirmPlayback(const Placed &placed);
    /// The message shown when the built-in player gives up: what the file is,
    /// and an offer to open it outside that the user has to click.
    void reportPlaybackFailure(const QString &filePath, const QString &message);
    /// The copyable block under the failure dialog: version, error, file and
    /// which media backends are actually present.
    QString playbackDiagnostics(const QString &filePath,
                                const QString &message) const;
    /// Re-encodes to 1080p beside the extracted file and plays that copy.
    void playSmallerCopy(const QString &filePath);
    void playPlaced(const Placed &placed);
    void playFile(const QString &filePath, const Placed &placed);
    void closePlayer();

    /// Between canvas pixels and page points.
    QRectF canvasToPdf(int page, const QRect &canvasRect) const;
    QRect  pdfToCanvas(int page, const QRectF &pdfBounds) const;

    void bindPanel();
    void unbindPanel();

    PageCanvas *m_canvas { nullptr };
    QString     m_documentPath;

    QList<Placed>  m_placed;
    /// Converted files of this session, removed on teardown.
    QStringList    m_tempFiles;
    /// Original file to the smaller copy made for it, so a medium that needed
    /// one is played from it from then on.
    QHash<QString, QString> m_smallerCopies;
    MediaSession   m_session;

    bool m_toolActive { false };
    /// Playback allowed for this document. Session-wide, reset on every
    /// document change.
    bool m_playbackTrusted { false };
    /// A failure is reported once. Players can raise the same trouble twice.
    bool m_reportingFailure { false };

    // Drag-to-frame (canvas coordinates)
    QRubberBand *m_band      { nullptr };
    bool         m_dragging  { false };
    QPoint       m_dragStart;
    int          m_dragPage  { -1 };

    // The area currently being set up
    MediaFrame *m_placement     { nullptr };
    int         m_placementPage { -1 };
    QRectF      m_placementBounds;

    QPointer<RichMediaPanel> m_panel;

    // QPointer and not a raw one: the player is a child of the canvas, and the
    // view tears its widgets down before its QObject children, this layer
    // among them.
    QPointer<MediaPlayerFrame> m_player;
};
