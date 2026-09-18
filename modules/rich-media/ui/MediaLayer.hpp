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
#include <QImage>
#include <QPointer>
#include <QRectF>

QT_BEGIN_NAMESPACE
class QLabel;
class QRubberBand;
QT_END_NAMESPACE

class MediaFrame;
class PageCanvas;
class RichMediaPanel;
class MediaPlayerFrame;

/// The media layer over the pages of one document view, built through PageOverlays.
class MediaLayer : public QObject, public PageOverlay
{
    Q_OBJECT

public:
    explicit MediaLayer(PageCanvas *canvas, QObject *parent = nullptr);
    ~MediaLayer() override;

    void setDocument(const QString &path) override;
    void setActiveTool(const QString &toolId) override;
    void relayout() override;
    bool writeTo(const QString &stagingPath) override;
    bool acceptsDroppedFile(const QString &path) const override;
    bool handleDroppedFile(const QString &path, int page,
                           const QPoint &canvasPosition,
                           QString *newDocument) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Placed {
        MediaAsset  asset;
        MediaSpec   spec;
        bool        pending { false };

        bool        removed { false };
        int         page { -1 };
        QRectF      bounds;
        MediaFrame *frame { nullptr };
        QLabel     *backgroundPatch { nullptr };
    };

    void rebuildFrames();
    void clearFrames();
    void addFrame(Placed placed);

    void positionFrame(const Placed &placed) const;
    void ensureBackgroundPatch(Placed &placed);
    QImage backgroundWithoutMedia(const MediaAsset &asset) const;

    void beginPlacement(int page, const QRectF &pdfBounds);
    void cancelPlacement();
    void selectFrame(MediaFrame *frame);
    void clearSelection(bool resetPanel = true);
    void applyPanel(const MediaSpec &spec);
    void commitInsert(const MediaSpec &spec);

    QString ensurePlayableSource(const QString &source);

    void showMenuFor(MediaFrame *frame, const QPoint &globalPos);
    void removeFrame(MediaFrame *frame);
    void saveMediaAs(const Placed &placed);

    QImage pageExcerpt(int page, const QRectF &pdfBounds) const;

    bool confirmPlayback(const Placed &placed);

    void reportPlaybackFailure(const QString &filePath, const QString &message);

    QString playbackDiagnostics(const QString &filePath,
                                const QString &message) const;

    void playSmallerCopy(const QString &filePath);
    void playPlaced(const Placed &placed);
    void playFile(const QString &filePath, const Placed &placed);
    void closePlayer();

    QRectF canvasToPdf(int page, const QRect &canvasRect) const;
    QRect  pdfToCanvas(int page, const QRectF &pdfBounds) const;

    void bindPanel();
    void unbindPanel();

    PageCanvas *m_canvas { nullptr };
    QString     m_documentPath;

    QList<Placed>  m_placed;

    QStringList    m_tempFiles;

    QHash<QString, QString> m_smallerCopies;
    MediaSession   m_session;

    bool m_toolActive { false };

    bool m_playbackTrusted { false };

    bool m_reportingFailure { false };

    QRubberBand *m_band      { nullptr };
    bool         m_dragging  { false };
    QPoint       m_dragStart;
    int          m_dragPage  { -1 };

    MediaFrame *m_placement     { nullptr };
    MediaFrame *m_selected      { nullptr };
    int         m_placementPage { -1 };
    QRectF      m_placementBounds;

    QPointer<RichMediaPanel> m_panel;

    QPointer<MediaPlayerFrame> m_player;
};
