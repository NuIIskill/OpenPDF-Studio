// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/ui/MediaLayer.hpp"

#include "rich-media/engine/ExternalPlayer.hpp"
#include "rich-media/engine/MediaDrop.hpp"
#include "rich-media/engine/MediaExtractor.hpp"
#include "rich-media/engine/MediaConvert.hpp"
#include "rich-media/engine/MediaFormat.hpp"
#include "rich-media/engine/MediaScanner.hpp"
#include "rich-media/engine/PosterFrame.hpp"
#include "rich-media/engine/RichMediaWriter.hpp"
#include "rich-media/ui/MediaFrame.hpp"
#include "rich-media/ui/RichMediaPanel.hpp"

#include "app/AppSettings.hpp"
#include "app/PdfPwStore.hpp"
#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#endif
#include "ui/view/PageCanvas.hpp"

#include "rich-media/ui/MediaPlayerFrame.hpp"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QPushButton>
#include <QPixmap>
#include <algorithm>
#include <QDebug>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QDir>
#include <QProgressDialog>
#include <QScopeGuard>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QRubberBand>
#include <QVariant>
#include <QWidget>

#include <optional>

namespace {

const QLatin1String kToolId { "video" };

constexpr int kMinDrag = 12;

}

RichMediaPanel *richMediaPanel();

MediaLayer::MediaLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{
    if (QWidget *widget = m_canvas ? m_canvas->canvasWidget() : nullptr) {

        widget->installEventFilter(this);
        m_band = new QRubberBand(QRubberBand::Rectangle, widget);
    }
}

MediaLayer::~MediaLayer()
{

    if (m_player) {
        m_player->stop();
        delete m_player.data();
    }
    closePlayer();
    MediaExtractor::clearCache(m_documentPath);
    for (const QString &temp : std::as_const(m_tempFiles))
        QFile::remove(temp);
}

void MediaLayer::setDocument(const QString &path)
{
    closePlayer();
    cancelPlacement();
    clearSelection();
    clearFrames();
    m_session.clear();

    if (!m_documentPath.isEmpty() && m_documentPath != path)
        MediaExtractor::clearCache(m_documentPath);

    m_documentPath = path;

    m_playbackTrusted = false;
    if (path.isEmpty()) {
        if (m_panel) m_panel->resetForInsert();
        return;
    }

    const QList<MediaAsset> assets = MediaScanner::scan(path);
    for (const MediaAsset &asset : assets) {
        Placed placed;
        placed.asset  = asset;
        placed.page   = asset.page;
        placed.bounds = asset.bounds;
        addFrame(std::move(placed));
    }
    if (!assets.isEmpty())
        qInfo() << "[rich-media] found" << assets.size() << "media in" << path;
}

void MediaLayer::setActiveTool(const QString &toolId)
{
    const bool active = (toolId == kToolId);
    if (active == m_toolActive) return;
    m_toolActive = active;

    for (const Placed &placed : m_placed)
        if (placed.frame) placed.frame->setInteractive(active && !placed.removed);

    if (active) {
        bindPanel();
    } else {
        cancelPlacement();
        clearSelection();
        unbindPanel();
    }
}

void MediaLayer::showMenuFor(MediaFrame *frame, const QPoint &globalPos)
{
    int index = -1;
    for (int i = 0; i < m_placed.size(); ++i)
        if (m_placed.at(i).frame == frame) { index = i; break; }
    if (index < 0) return;
    const Placed placed = m_placed.at(index);
    if (placed.removed) return;

    QMenu menu;
    QAction *edit = m_toolActive ? menu.addAction(tr("Edit properties")) : nullptr;
    if (edit) menu.addSeparator();
    QAction *play = menu.addAction(tr("Play"));
    QAction *save = menu.addAction(tr("Save media as..."));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove"));
    remove->setShortcut(QKeySequence::Delete);

    const bool playable = placed.pending || placed.asset.isEmbedded();
    play->setEnabled(playable);
    save->setEnabled(playable);

    QAction *chosen = menu.exec(globalPos);
    if (edit && chosen == edit) selectFrame(frame);
    else if (chosen == play)   playPlaced(placed);
    else if (chosen == save)   saveMediaAs(placed);
    else if (chosen == remove) removeFrame(frame);
}

void MediaLayer::removeFrame(MediaFrame *frame)
{
    for (int i = 0; i < m_placed.size(); ++i) {
        Placed &placed = m_placed[i];
        if (placed.frame != frame || placed.removed) continue;

        if (placed.asset.annotObject > 0) {
            QImage cleanBackground;
            if (placed.backgroundPatch) {
                const QPixmap patch = placed.backgroundPatch->pixmap();
                if (!patch.isNull()) cleanBackground = patch.toImage();
            }
            if (cleanBackground.isNull())
                cleanBackground = backgroundWithoutMedia(placed.asset);

            if (placed.pending)
                m_session.dropInsert(placed.spec);
            m_session.addRemoval(placed.asset);

            if (m_selected == frame) {
                frame->setSelected(false);
                m_selected = nullptr;
                if (m_panel) m_panel->resetForInsert();
            }
            delete placed.backgroundPatch;
            placed.backgroundPatch = nullptr;
            placed.pending = false;
            placed.removed = true;
            placed.page = placed.asset.page;
            placed.bounds = placed.asset.bounds;
            frame->setInteractive(false);
            frame->setPoster(cleanBackground);
            frame->setMode(MediaFrame::Mode::Removed);
            positionFrame(placed);
            return;
        }

        if (placed.pending) {
            m_session.dropInsert(placed.spec);
            if (m_selected == frame) clearSelection();
            frame->deleteLater();
            m_placed.removeAt(i);
            return;
        }
    }
}

QImage MediaLayer::pageExcerpt(int page, const QRectF &pdfBounds) const
{
    if (!m_canvas) return {};
    const QLabel *label = m_canvas->pageLabel(page);
    if (!label) return {};
    const QPixmap pixmap = label->pixmap();
    if (pixmap.isNull()) return {};

    const qreal scale = m_canvas->screenScale();
    const QRect inPixels(qRound(pdfBounds.left()   * scale),
                         qRound(pdfBounds.top()    * scale),
                         qRound(pdfBounds.width()  * scale),
                         qRound(pdfBounds.height() * scale));
    const QRect clipped = inPixels.intersected(pixmap.rect());
    if (clipped.isEmpty()) return {};
    return pixmap.copy(clipped).toImage();
}

QImage MediaLayer::backgroundWithoutMedia(const MediaAsset &asset) const
{
#ifdef HAVE_PDF_RENDERING
    if (!m_canvas || m_documentPath.isEmpty() || asset.annotObject <= 0) return {};

    QTemporaryFile cleanFile(QDir::tempPath()
        + QStringLiteral("/openpdf-media-background-XXXXXX.pdf"));
    if (!cleanFile.open()) return {};
    const QString cleanPath = cleanFile.fileName();
    cleanFile.close();
    QFile::remove(cleanPath);
    if (!QFile::copy(m_documentPath, cleanPath)) return {};

    MediaSession removal;
    removal.addRemoval(asset);
    const QString password = PdfPwStore::get(m_documentPath);
    if (!RichMediaWriter::apply(cleanPath, removal, password)) return {};

    std::unique_ptr<PdfBackend> backend = PdfBackend::create();
    if (!backend || !backend->open(cleanPath,
            [password](const QString &, bool) -> std::optional<QString> {
                return password;
            }))
        return {};

    const qreal scale = m_canvas->screenScale();
    const QImage page = backend->renderPage(asset.page, scale);
    if (page.isNull()) return {};
    const QRect pixels(qRound(asset.bounds.left() * scale),
                       qRound(asset.bounds.top() * scale),
                       qRound(asset.bounds.width() * scale),
                       qRound(asset.bounds.height() * scale));
    const QRect clipped = pixels.intersected(page.rect());
    return clipped.isEmpty() ? QImage() : page.copy(clipped);
#else
    Q_UNUSED(asset)
    return {};
#endif
}

void MediaLayer::ensureBackgroundPatch(Placed &placed)
{
    if (placed.pending || placed.asset.annotObject <= 0 || placed.backgroundPatch)
        return;
    const QImage cleanBackground = backgroundWithoutMedia(placed.asset);
    if (cleanBackground.isNull()) return;

    QWidget *canvasWidget = m_canvas ? m_canvas->canvasWidget() : nullptr;
    if (!canvasWidget) return;
    auto *patch = new QLabel(canvasWidget);
    patch->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    patch->setScaledContents(true);
    patch->setPixmap(QPixmap::fromImage(cleanBackground));
    placed.backgroundPatch = patch;
    positionFrame(placed);
}

void MediaLayer::saveMediaAs(const Placed &placed)
{
    QString source = placed.pending ? placed.spec.source : QString();
    if (source.isEmpty() && placed.asset.isEmbedded())
        source = MediaExtractor::extract(m_documentPath, placed.asset);
    if (source.isEmpty()) return;

    const QString suggested = placed.pending ? placed.spec.displayName
                                             : placed.asset.name;
    const QString target = QFileDialog::getSaveFileName(
        QApplication::activeWindow(), tr("Save media as"), suggested);
    if (target.isEmpty()) return;

    QFile::remove(target);
    if (!QFile::copy(source, target))
        QMessageBox::warning(QApplication::activeWindow(), tr("Rich Media"),
                             tr("Could not write \"%1\".").arg(target));
}

void MediaLayer::clearFrames()
{
    m_selected = nullptr;
    for (Placed &placed : m_placed) {
        delete placed.backgroundPatch;
        delete placed.frame;
    }
    m_placed.clear();
}

void MediaLayer::addFrame(Placed placed)
{
    QWidget *canvasWidget = m_canvas ? m_canvas->canvasWidget() : nullptr;
    if (!canvasWidget) return;

    auto *frame = new MediaFrame(MediaFrame::Mode::Existing, canvasWidget);
    frame->setCaption(placed.pending ? placed.spec.displayName : placed.asset.name);
    if (placed.pending && !placed.spec.poster.isNull())
        frame->setPoster(placed.spec.poster);
    else if (placed.pending)
        frame->setPoster(PosterFrame::placeholder(QSize(320, 180)));

    placed.frame = frame;
    m_placed.append(placed);

    frame->setInteractive(m_toolActive);

    connect(frame, &MediaFrame::activated, this, [this, frame]() {
        for (const Placed &placed : m_placed)
            if (placed.frame == frame) { playPlaced(placed); return; }
    });
    connect(frame, &MediaFrame::selected, this,
            [this, frame]() { selectFrame(frame); });
    connect(frame, &MediaFrame::geometryEdited, this,
            [this, frame](const QRect &geometry) {
        for (Placed &placed : m_placed) {
            if (placed.frame != frame || placed.removed) continue;
            placed.bounds = canvasToPdf(placed.page, geometry);
            if (m_panel && m_selected == frame)
                m_panel->setPlacement(placed.page, placed.bounds);
            return;
        }
    });
    connect(frame, &MediaFrame::geometryEditFinished, this,
            [this, frame](const QRect &geometry) {
        for (Placed &placed : m_placed) {
            if (placed.frame != frame || placed.removed) continue;
            placed.bounds = canvasToPdf(placed.page, geometry);
            if (m_panel && m_selected == frame) {
                m_panel->setPlacement(placed.page, placed.bounds);
                const MediaSpec media = m_panel->spec();
                if (media.isValid()) {
                    applyPanel(media);
                    return;
                }
            }
            placed.bounds = placed.pending ? placed.spec.bounds : placed.asset.bounds;
            positionFrame(placed);
            return;
        }
    });
    connect(frame, &MediaFrame::contextMenuRequested, this,
            [this, frame](const QPoint &globalPos) { showMenuFor(frame, globalPos); });
    connect(frame, &MediaFrame::deleteRequested, this,
            [this, frame]() { removeFrame(frame); });

    positionFrame(m_placed.last());
    frame->show();
}

void MediaLayer::positionFrame(const Placed &placed) const
{
    if (!placed.frame || !m_canvas) return;
    if (placed.backgroundPatch) {
        const QRect patchRect = pdfToCanvas(placed.asset.page, placed.asset.bounds);
        if (patchRect.isNull()) {
            placed.backgroundPatch->hide();
        } else {
            placed.backgroundPatch->setGeometry(patchRect);
            placed.backgroundPatch->show();
            placed.backgroundPatch->raise();
        }
    }
    const QRect canvasRect = pdfToCanvas(placed.page, placed.bounds);
    if (canvasRect.isNull()) {
        placed.frame->hide();
        return;
    }
    placed.frame->setGeometry(canvasRect);
    if (QLabel *label = m_canvas->pageLabel(placed.page))
        placed.frame->setPageRect(label->geometry());
    placed.frame->show();
    placed.frame->raise();
}

void MediaLayer::rebuildFrames()
{
    for (const Placed &placed : m_placed)
        positionFrame(placed);
}

void MediaLayer::relayout()
{
    rebuildFrames();

    if (m_placement && m_placementPage >= 0) {
        const QRect canvasRect = pdfToCanvas(m_placementPage, m_placementBounds);
        if (!canvasRect.isNull()) {
            m_placement->setGeometry(canvasRect);
            if (QLabel *label = m_canvas->pageLabel(m_placementPage))
                m_placement->setPageRect(label->geometry());
        }
    }

    if (m_player) {
        for (const Placed &placed : m_placed) {
            if (placed.frame != m_player->property("mediaFrame").value<QObject *>())
                continue;
            const QRect canvasRect = pdfToCanvas(placed.page, placed.bounds);
            if (!canvasRect.isNull()) m_player->setGeometry(canvasRect);
        }
    }
}

QRect MediaLayer::pdfToCanvas(int page, const QRectF &pdfBounds) const
{
    if (!m_canvas || page < 0 || page >= m_canvas->pageLabelCount()) return {};
    const QLabel *label = m_canvas->pageLabel(page);
    if (!label) return {};
    const qreal scale = m_canvas->screenScale();
    return QRect(label->pos().x() + qRound(pdfBounds.left() * scale),
                 label->pos().y() + qRound(pdfBounds.top()  * scale),
                 qMax(1, qRound(pdfBounds.width()  * scale)),
                 qMax(1, qRound(pdfBounds.height() * scale)));
}

QRectF MediaLayer::canvasToPdf(int page, const QRect &canvasRect) const
{
    if (!m_canvas) return {};
    const QLabel *label = m_canvas->pageLabel(page);
    if (!label) return {};
    const qreal scale = m_canvas->screenScale();
    if (scale <= 0.0) return {};
    return QRectF((canvasRect.x() - label->pos().x()) / scale,
                  (canvasRect.y() - label->pos().y()) / scale,
                  canvasRect.width()  / scale,
                  canvasRect.height() / scale);
}

bool MediaLayer::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *canvasWidget = m_canvas ? m_canvas->canvasWidget() : nullptr;
    if (!m_toolActive || watched != canvasWidget) return false;

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        const QPoint pos = mouse->pos();
        auto [page, label] = m_canvas->pageAtCanvasPos(pos);
        if (page < 0 || !label) return false;
        if (m_selected) clearSelection();
        m_dragging  = true;
        m_dragStart = pos;
        m_dragPage  = page;
        m_band->setGeometry(QRect(pos, QSize()));
        m_band->show();
        return true;
    }
    case QEvent::MouseMove: {
        if (!m_dragging) return false;
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_band->setGeometry(QRect(m_dragStart, mouse->pos()).normalized());
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (!m_dragging) return false;
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_dragging = false;
        m_band->hide();
        const QRect dragged = QRect(m_dragStart, mouse->pos()).normalized();
        if (dragged.width() < kMinDrag || dragged.height() < kMinDrag) return true;

        QRect clamped = dragged;
        if (const QLabel *label = m_canvas->pageLabel(m_dragPage))
            clamped = dragged.intersected(label->geometry());
        if (clamped.width() < kMinDrag || clamped.height() < kMinDrag) return true;

        beginPlacement(m_dragPage, canvasToPdf(m_dragPage, clamped));
        return true;
    }
    default:
        break;
    }
    return false;
}

void MediaLayer::beginPlacement(int page, const QRectF &pdfBounds)
{
    cancelPlacement();
    QWidget *canvasWidget = m_canvas ? m_canvas->canvasWidget() : nullptr;
    if (!canvasWidget || pdfBounds.isEmpty()) return;

    m_placementPage   = page;
    m_placementBounds = pdfBounds;

    m_placement = new MediaFrame(MediaFrame::Mode::Placement, canvasWidget);
    if (const QLabel *label = m_canvas->pageLabel(page))
        m_placement->setPageRect(label->geometry());
    m_placement->setGeometry(pdfToCanvas(page, pdfBounds));
    m_placement->show();
    m_placement->raise();
    m_placement->setFocus(Qt::OtherFocusReason);

    connect(m_placement, &MediaFrame::geometryEdited, this, [this](const QRect &geometry) {
        m_placementBounds = canvasToPdf(m_placementPage, geometry);
        if (m_panel) m_panel->setPlacement(m_placementPage, m_placementBounds);
    });
    connect(m_placement, &MediaFrame::deleteRequested, this, [this]() { cancelPlacement(); });

    bindPanel();
    if (m_panel) m_panel->setPlacement(page, pdfBounds);
}

void MediaLayer::cancelPlacement()
{
    if (m_placement) {
        m_placement->deleteLater();
        m_placement = nullptr;
    }
    m_placementPage = -1;
    m_placementBounds = QRectF();
    if (m_panel) m_panel->clearPlacement();
}

void MediaLayer::clearSelection(bool resetPanel)
{
    if (m_selected) m_selected->setSelected(false);
    for (Placed &placed : m_placed) {
        if (placed.frame != m_selected || placed.pending || placed.removed) continue;
        placed.frame->setPoster(QImage());
        delete placed.backgroundPatch;
        placed.backgroundPatch = nullptr;
        placed.page = placed.asset.page;
        placed.bounds = placed.asset.bounds;
        positionFrame(placed);
        break;
    }
    m_selected = nullptr;
    if (resetPanel && m_panel) m_panel->resetForInsert();
}

void MediaLayer::selectFrame(MediaFrame *frame)
{
    if (!frame) return;
    cancelPlacement();
    for (const Placed &placed : std::as_const(m_placed))
        if (placed.frame && placed.frame != frame) placed.frame->setSelected(false);

    for (Placed &placed : m_placed) {
        if (placed.frame != frame || placed.removed) continue;
        m_selected = frame;
        frame->setSelected(true);

        MediaSpec media;
        if (placed.pending) {
            media = placed.spec;
        } else {
            ensureBackgroundPatch(placed);
            media.type = placed.asset.mimeType.startsWith(QLatin1String("audio/"))
                ? MediaSpec::Type::Audio : MediaSpec::Type::Video;
            media.source = placed.asset.isEmbedded()
                ? MediaExtractor::extract(m_documentPath, placed.asset) : QString();
            media.displayName = placed.asset.name;
            media.mimeType = placed.asset.mimeType;
            if (media.type == MediaSpec::Type::Video && !media.source.isEmpty())
                media.poster = PosterFrame::grab(media.source, 1280);
            if (media.poster.isNull())
                media.poster = pageExcerpt(placed.page, placed.bounds);
            media.activateOnPageOpen = placed.asset.activateOnPageOpen;
            media.muted = placed.asset.muted;
            media.loop = placed.asset.loop;
            media.showControls = placed.asset.showControls;
            media.floating = placed.asset.floating;
            media.page = placed.page;
            media.bounds = placed.bounds;
        }
        if (!media.poster.isNull()) frame->setPoster(media.poster);
        bindPanel();
        if (m_panel) m_panel->editMedia(media);
        return;
    }
}

void MediaLayer::bindPanel()
{
    RichMediaPanel *panel = richMediaPanel();
    if (!panel || m_panel == panel) return;

    unbindPanel();
    m_panel = panel;
    connect(panel, &RichMediaPanel::applyRequested,
            this, &MediaLayer::applyPanel, Qt::UniqueConnection);
    connect(panel, &RichMediaPanel::previewChanged, this, [this](const QImage &poster) {
        if (m_placement) m_placement->setPoster(poster);
    }, Qt::UniqueConnection);

    if (m_selected) {
        selectFrame(m_selected);
    } else if (m_placementPage >= 0) {
        panel->setPlacement(m_placementPage, m_placementBounds);
    } else {
        panel->resetForInsert();
    }
}

void MediaLayer::unbindPanel()
{
    if (m_panel) disconnect(m_panel, nullptr, this, nullptr);
    m_panel = nullptr;
}

void MediaLayer::applyPanel(const MediaSpec &requested)
{
    if (!m_selected) {
        commitInsert(requested);
        return;
    }

    for (Placed &placed : m_placed) {
        if (placed.frame != m_selected || placed.removed) continue;

        MediaSpec media = requested;
        const QString previousSource = placed.pending ? placed.spec.source
            : (placed.asset.isEmbedded()
                ? MediaExtractor::extract(m_documentPath, placed.asset) : QString());
        const bool sourceChanged = media.source != previousSource;
        if (media.type == MediaSpec::Type::Video && sourceChanged) {
            const QString playable = ensurePlayableSource(media.source);
            if (playable.isEmpty()) return;
            if (playable != media.source) {
                media.displayName = MediaDrop::displayNameFor(media.source, playable);
                media.source = playable;
                media.mimeType = QStringLiteral("video/mp4");
            }
        }
        if (media.poster.isNull() && media.type == MediaSpec::Type::Video)
            media.poster = PosterFrame::grab(media.source, 960);
        if (media.poster.isNull())
            media.poster = PosterFrame::placeholder(QSize(960, 540));

        if (placed.pending) {
            m_session.dropInsert(placed.spec);
        } else {
            m_session.addRemoval(placed.asset);
        }
        m_session.addInsert(media);

        placed.pending = true;
        placed.spec = media;
        placed.page = media.page;
        placed.bounds = media.bounds;
        placed.frame->setCaption(media.displayName);
        placed.frame->setPoster(media.poster);
        placed.frame->setSelected(true);
        positionFrame(placed);
        if (m_panel) m_panel->editMedia(media);
        return;
    }
}

QString MediaLayer::ensurePlayableSource(const QString &source)
{
    const MediaFormat::Info info = MediaFormat::inspect(source);
    if (!info.readable || info.playsEverywhere()) return source;

    QWidget *parent = QApplication::activeWindow();
    const QString what = info.videoCodec.isEmpty()
        ? tr("%1, an unsupported format").arg(info.container.toUpper())
        : tr("%1 in %2").arg(info.videoCodec.toUpper(), info.container.toUpper());

    if (!MediaConvert::available()) {
        const auto answer = QMessageBox::warning(
            parent, tr("Rich Media"),
            tr("This file is %1. Most PDF viewers play only H.264 video in an "
               "MP4 container, so for many people the video will be a still "
               "image.\n\nInstall ffmpeg to convert it, or embed it as it is.")
                .arg(what),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        return answer == QMessageBox::Ok ? source : QString();
    }

    QMessageBox question(parent);
    question.setIcon(QMessageBox::Question);
    question.setWindowTitle(tr("Rich Media"));
    question.setText(tr("This file is %1.").arg(what));
    question.setInformativeText(
        tr("H.264 in an MP4 container is what plays everywhere: in PDF "
           "viewers, in browsers and on phones. Convert it now so the video "
           "works for everyone who opens the document?"));
    QAbstractButton *convert = question.addButton(tr("Convert"), QMessageBox::AcceptRole);
    QAbstractButton *asIs    = question.addButton(tr("Embed as is"), QMessageBox::DestructiveRole);
    question.addButton(QMessageBox::Cancel);
    question.setDefaultButton(qobject_cast<QPushButton *>(convert));
    question.exec();

    if (question.clickedButton() == asIs)  return source;
    if (question.clickedButton() != convert) return QString();

    const QString target = QDir(QDir::tempPath()).filePath(
        QStringLiteral("openpdf-media-%1-%2.mp4")
            .arg(QCoreApplication::applicationPid())
            .arg(QFileInfo(source).completeBaseName()));

    QProgressDialog progress(tr("Converting %1...").arg(QFileInfo(source).fileName()),
                             tr("Cancel"), 0, 100, parent);
    progress.setWindowTitle(tr("Rich Media"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    const bool ok = MediaConvert::run(source, target, [&progress](int percent) {
        progress.setValue(percent);
        QApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.close();

    if (!ok) {
        if (!progress.wasCanceled())
            QMessageBox::warning(parent, tr("Rich Media"),
                                 tr("The file could not be converted."));
        return QString();
    }

    m_tempFiles << target;
    return target;
}

void MediaLayer::commitInsert(const MediaSpec &requested)
{
    MediaSpec spec = requested;
    spec.page   = m_placementPage;
    spec.bounds = m_placementBounds;
    if (!spec.isValid()) return;

    if (spec.type == MediaSpec::Type::Video) {
        const QString playable = ensurePlayableSource(spec.source);
        if (playable.isEmpty()) return;
        if (playable != spec.source) {
            spec.displayName = MediaDrop::displayNameFor(spec.source, playable);
            spec.source      = playable;
            spec.mimeType    = QStringLiteral("video/mp4");
        }
    }

    if (spec.poster.isNull() && spec.type == MediaSpec::Type::Video)
        spec.poster = PosterFrame::grab(spec.source, 960);
    if (spec.poster.isNull())
        spec.poster = PosterFrame::placeholder(QSize(960, 540));

    m_session.addInsert(spec);

    Placed placed;
    placed.pending = true;
    placed.spec    = spec;
    placed.page    = spec.page;
    placed.bounds  = spec.bounds;
    addFrame(std::move(placed));

    cancelPlacement();
}

QString MediaLayer::playbackDiagnostics(const QString &filePath,
                                        const QString &message) const
{
    const MediaFormat::Info info = MediaFormat::inspect(filePath);
    QStringList lines;
    lines << QStringLiteral("OpenPDF Studio %1").arg(QLatin1String(APP_VERSION))
          << QStringLiteral("Qt %1 on %2").arg(QLatin1String(qVersion()),
                                               QSysInfo::prettyProductName())
          << QStringLiteral("error: %1").arg(message)
          << QStringLiteral("file: %1 / %2, %3x%4, %5 s")
                 .arg(info.container, info.videoCodec)
                 .arg(info.size.width()).arg(info.size.height())
                 .arg(info.durationSec, 0, 'f', 1);
    if (m_player) lines << m_player->engineReport();
    lines << QStringLiteral("ffmpeg: %1")
                 .arg(MediaConvert::available() ? MediaConvert::toolPath()
                                                : QStringLiteral("<not found>"));
    return lines.join(QLatin1Char('\n'));
}

void MediaLayer::reportPlaybackFailure(const QString &filePath,
                                       const QString &message)
{

    if (m_reportingFailure) return;
    m_reportingFailure = true;
    const auto done = qScopeGuard([this] { m_reportingFailure = false; });

    const MediaFormat::Info info = MediaFormat::inspect(filePath);
    const bool large = info.size.height() > 1080;

    QStringList sentences;
    if (info.readable && !info.videoCodec.isEmpty()) {
        sentences << (info.size.isValid()
            ? tr("%1 in %2, %3 by %4 pixels.")
                  .arg(info.videoCodec.toUpper(), info.container.toUpper())
                  .arg(info.size.width()).arg(info.size.height())
            : tr("%1 in %2.").arg(info.videoCodec.toUpper(),
                                  info.container.toUpper()));
    }
    if (large)
        sentences << tr("Video this large is more than some systems will "
                        "decode. A smaller copy usually plays, and the "
                        "document keeps the original.");
    const QString facts = sentences.join(QLatin1Char(' '));

    QMessageBox box(QApplication::activeWindow());
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Rich Media"));
    box.setText(tr("This video could not be played here: %1").arg(message));
    box.setInformativeText(facts);

    box.setDetailedText(playbackDiagnostics(filePath, message));

    QAbstractButton *smaller = nullptr;
    if (large && MediaConvert::available())
        smaller = box.addButton(tr("Play a smaller copy"), QMessageBox::AcceptRole);
    QAbstractButton *outside =
        box.addButton(tr("Open in system player"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.setDefaultButton(QMessageBox::Close);
    box.exec();

    if (box.clickedButton() == outside) {
        if (!ExternalPlayer::play(filePath, QString()))
            QMessageBox::warning(QApplication::activeWindow(), tr("Rich Media"),
                                 tr("No media player was found on this system."));
        return;
    }
    if (smaller && box.clickedButton() == smaller) playSmallerCopy(filePath);
}

void MediaLayer::playSmallerCopy(const QString &filePath)
{
    QWidget *parent = QApplication::activeWindow();
    const QString target = QDir(QFileInfo(filePath).absolutePath())
        .filePath(QFileInfo(filePath).completeBaseName() + QStringLiteral("-1080p.mp4"));

    if (!QFileInfo::exists(target)) {
        QProgressDialog progress(tr("Preparing a smaller copy..."), tr("Cancel"),
                                 0, 100, parent);
        progress.setWindowTitle(tr("Rich Media"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);

        const bool ok = MediaConvert::run(filePath, target,
                                          [&progress](int percent) {
            progress.setValue(percent);
            QApplication::processEvents();
            return !progress.wasCanceled();
        },  1080);
        progress.close();

        if (!ok) {
            if (!progress.wasCanceled())
                QMessageBox::warning(parent, tr("Rich Media"),
                                     tr("The smaller copy could not be made."));
            return;
        }
        m_tempFiles << target;
    }

    m_smallerCopies.insert(filePath, target);

    for (const Placed &placed : m_placed) {
        if (placed.pending || !placed.asset.isEmbedded()) continue;
        playFile(filePath, placed);
        return;
    }
}

bool MediaLayer::confirmPlayback(const Placed &placed)
{

    if (placed.pending || m_playbackTrusted) return true;

    QMessageBox question(QApplication::activeWindow());
    question.setIcon(QMessageBox::Question);
    question.setWindowTitle(tr("Rich Media"));
    question.setText(tr("Play \"%1\" from this document?").arg(placed.asset.name));
    question.setInformativeText(
        tr("Media in a PDF is chosen by whoever made the file. Playing it "
           "hands the data to a decoder, in this build possibly to a player "
           "outside OpenPDF Studio. Play it only if you trust the document."));
    QAbstractButton *once  = question.addButton(tr("Play once"), QMessageBox::AcceptRole);
    QAbstractButton *trust = question.addButton(tr("Always for this document"),
                                                QMessageBox::AcceptRole);
    question.addButton(QMessageBox::Cancel);

    question.setDefaultButton(QMessageBox::Cancel);
    question.exec();

    if (question.clickedButton() == trust) {
        m_playbackTrusted = true;
        return true;
    }
    return question.clickedButton() == once;
}

void MediaLayer::playPlaced(const Placed &placed)
{
    if (!confirmPlayback(placed)) return;

    if (placed.pending) {

        playFile(placed.spec.source, placed);
        return;
    }

    if (!placed.asset.isEmbedded()) {
        QMessageBox::information(
            QApplication::activeWindow(), tr("Rich Media"),
            tr("This media points to a file outside the document, "
               "so OpenPDF Studio does not open it."));
        return;
    }

    const QString file = MediaExtractor::extract(m_documentPath, placed.asset);
    if (file.isEmpty()) {
        QMessageBox::warning(QApplication::activeWindow(), tr("Rich Media"),
                             tr("The media could not be read from the document."));
        return;
    }
    playFile(file, placed);
}

void MediaLayer::playFile(const QString &filePath, const Placed &placed)
{

    const QString playable = m_smallerCopies.value(filePath, filePath);

    AppSettings settings;
    const QString mode = settings.mediaPlayback();

    const bool wantsInApp = (mode == QLatin1String("inapp"));
    const QString command = mode == QLatin1String("custom")
                          ? settings.customPlayerCommand() : QString();

    if (wantsInApp) {
        closePlayer();
        QWidget *canvasWidget = m_canvas ? m_canvas->canvasWidget() : nullptr;
        const QRect canvasRect = pdfToCanvas(placed.page, placed.bounds);
        if (!canvasWidget || canvasRect.isNull()) {
            QMessageBox::warning(QApplication::activeWindow(), tr("Rich Media"),
                                 tr("The page this media sits on is not on "
                                    "screen, so it cannot be played here."));
            return;
        }

        m_player = new MediaPlayerFrame(canvasWidget);
        m_player->setProperty("mediaFrame",
                              QVariant::fromValue<QObject *>(placed.frame));
        m_player->setGeometry(canvasRect);
        connect(m_player, &MediaPlayerFrame::closeRequested,
                this, &MediaLayer::closePlayer);
        connect(m_player, &MediaPlayerFrame::failed, this,
                [this, playable](const QString &message) {
            qWarning() << "[rich-media] in-app playback failed:" << message;
            closePlayer();
            reportPlaybackFailure(playable, message);
        });

        if (placed.pending && !placed.spec.poster.isNull())
            m_player->setPoster(placed.spec.poster);
        else
            m_player->setPoster(pageExcerpt(placed.page, placed.bounds));
        m_player->show();
        m_player->raise();
        m_player->play(playable,
                       placed.pending ? placed.spec.showControls : true,
                       placed.pending ? placed.spec.loop  : false,
                       placed.pending ? placed.spec.muted : false);
        return;
    }

    if (ExternalPlayer::play(playable, command)) return;

    QMessageBox::warning(
        QApplication::activeWindow(), tr("Rich Media"),
        tr("No media player was found. Install VLC or mpv, or set a custom "
           "player under Settings, Media Playback."));
}

void MediaLayer::closePlayer()
{
    if (!m_player) return;
    m_player->stop();
    m_player->deleteLater();
    m_player = nullptr;
}

bool MediaLayer::acceptsDroppedFile(const QString &path) const
{
    return !m_documentPath.isEmpty() && RichMediaWriter::available()
        && MediaDrop::isVideoFile(path);
}

bool MediaLayer::handleDroppedFile(const QString &path, int page,
                                   const QPoint &canvasPosition,
                                   QString *newDocument)
{
    if (!acceptsDroppedFile(path)) return false;
    if (newDocument) newDocument->clear();

    const QString source = ensurePlayableSource(path);
    if (source.isEmpty()) return true;

    if (page >= 0 && m_canvas) {
        const QLabel *label = m_canvas->pageLabel(page);
        const qreal scale = m_canvas->screenScale();
        if (label && scale > 0.0) {
            QImage poster = PosterFrame::grab(source, 1280);
            const MediaFormat::Info mediaInfo = MediaFormat::inspect(source);
            const QSize mediaPixels = mediaInfo.size.isValid() ? mediaInfo.size
                                                               : poster.size();
            if (poster.isNull())
                poster = PosterFrame::placeholder(QSize(960, 540));

            const QSizeF pageSize(label->width() / scale, label->height() / scale);
            const QPointF dropPoint((canvasPosition.x() - label->x()) / scale,
                                    (canvasPosition.y() - label->y()) / scale);

            MediaSpec spec;
            spec.type = MediaSpec::Type::Video;
            spec.source = source;
            spec.displayName = MediaDrop::displayNameFor(path, source);
            spec.mimeType = MediaDrop::mimeTypeFor(source);
            spec.poster = poster;
            spec.page = page;
            spec.bounds = MediaDrop::placementBoundsFor(pageSize, mediaPixels,
                                                        dropPoint);
            if (!spec.isValid()) return true;

            m_session.addInsert(spec);
            Placed placed;
            placed.pending = true;
            placed.spec = spec;
            placed.page = page;
            placed.bounds = spec.bounds;
            addFrame(std::move(placed));
            if (m_toolActive && !m_placed.isEmpty())
                selectFrame(m_placed.constLast().frame);
            return true;
        }
    }

    const QString work = MediaDrop::addAsOwnPage(
        m_documentPath, source, page >= 0 ? page : m_canvas->pageCount() - 1,
        MediaDrop::displayNameFor(path, source));
    if (work.isEmpty()) {
        QMessageBox::warning(QApplication::activeWindow(), tr("Rich Media"),
                             tr("The video could not be added as a new page."));
        return true;
    }
    if (newDocument) *newDocument = work;
    return true;
}

bool MediaLayer::writeTo(const QString &stagingPath)
{
    if (m_session.isEmpty()) return true;
    if (!RichMediaWriter::available()) {
        qWarning() << "[rich-media] nothing can be written without qpdf";
        return false;
    }

    return RichMediaWriter::apply(stagingPath, m_session,
                                  PdfPwStore::get(m_documentPath));
}
