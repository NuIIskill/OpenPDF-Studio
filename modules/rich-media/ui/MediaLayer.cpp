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
#include <QRubberBand>
#include <QVariant>
#include <QWidget>

namespace {

/// The tool id the module registers under.
const QLatin1String kToolId { "video" };

/// Smallest drag that counts as one, in canvas pixels.
constexpr int kMinDrag = 12;

} // namespace

// Built once by the window (ToolPanels) and driven by whichever layer is
// active. Defined in MediaModule.cpp.
RichMediaPanel *richMediaPanel();

MediaLayer::MediaLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{
    if (QWidget *widget = m_canvas ? m_canvas->canvasWidget() : nullptr) {
        // Installed after the view's own filter and therefore asked first:
        // while the media tool is active the clicks are ours.
        widget->installEventFilter(this);
        m_band = new QRubberBand(QRubberBand::Rectangle, widget);
    }
}

MediaLayer::~MediaLayer()
{
    // Torn down synchronously, not through deleteLater: on shutdown there is
    // no event loop left to run it, and the decoder threads would keep going.
    if (m_player) {
        m_player->stop();
        delete m_player.data();
    }
    closePlayer();
    MediaExtractor::clearCache(m_documentPath);
    for (const QString &temp : std::as_const(m_tempFiles))
        QFile::remove(temp);
}

// ── Document ─────────────────────────────────────────────────────────────────

void MediaLayer::setDocument(const QString &path)
{
    closePlayer();
    cancelPlacement();
    clearFrames();
    m_session.clear();

    if (!m_documentPath.isEmpty() && m_documentPath != path)
        MediaExtractor::clearCache(m_documentPath);

    m_documentPath = path;
    // Trust covers exactly one document; a new one starts at zero.
    m_playbackTrusted = false;
    if (path.isEmpty()) {
        if (m_panel) m_panel->clearPlacement();
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
        unbindPanel();
    }
}

// ── Removing ─────────────────────────────────────────────────────────────────

void MediaLayer::showMenuFor(MediaFrame *frame, const QPoint &globalPos)
{
    int index = -1;
    for (int i = 0; i < m_placed.size(); ++i)
        if (m_placed.at(i).frame == frame) { index = i; break; }
    if (index < 0) return;
    const Placed placed = m_placed.at(index);
    if (placed.removed) return;

    QMenu menu;
    QAction *play = menu.addAction(tr("Play"));
    QAction *save = menu.addAction(tr("Save media as..."));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove"));
    remove->setShortcut(QKeySequence::Delete);

    const bool playable = placed.pending || placed.asset.isEmbedded();
    play->setEnabled(playable);
    save->setEnabled(playable);

    QAction *chosen = menu.exec(globalPos);
    if (chosen == play)        playPlaced(placed);
    else if (chosen == save)   saveMediaAs(placed);
    else if (chosen == remove) removeFrame(frame);
}

void MediaLayer::removeFrame(MediaFrame *frame)
{
    for (int i = 0; i < m_placed.size(); ++i) {
        Placed &placed = m_placed[i];
        if (placed.frame != frame || placed.removed) continue;

        if (placed.pending) {
            // Noch nichts im Dokument: Auftrag zurücknehmen, Rahmen weg, fertig.
            m_session.dropInsert(placed.spec);
            frame->deleteLater();
            m_placed.removeAt(i);
            return;
        }

        // Already in the document. It leaves on save; until then the frame
        // covers the spot, because the page below still shows the poster.
        m_session.addRemoval(placed.asset);
        placed.removed = true;
        frame->setInteractive(false);
        frame->setCoverColor(pageColorAround(placed.page, placed.bounds));
        frame->setMode(MediaFrame::Mode::Removed);
        return;
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

QColor MediaLayer::pageColorAround(int page, const QRectF &pdfBounds) const
{
    const QColor fallback(Qt::white);
    if (!m_canvas) return fallback;
    const QLabel *label = m_canvas->pageLabel(page);
    if (!label) return fallback;
    const QPixmap pixmap = label->pixmap();
    if (pixmap.isNull()) return fallback;

    const qreal scale = m_canvas->screenScale();
    const QRect inPixels(qRound(pdfBounds.left()   * scale),
                         qRound(pdfBounds.top()    * scale),
                         qRound(pdfBounds.width()  * scale),
                         qRound(pdfBounds.height() * scale));

    // A ring just outside the area. Median, not mean, so a rule or a letter
    // at the edge does not skew the result.
    const QImage image = pixmap.toImage();
    const int margin = 4;
    QList<int> reds, greens, blues;
    const QList<QPoint> probes {
        { inPixels.center().x(),  inPixels.top()    - margin },
        { inPixels.center().x(),  inPixels.bottom() + margin },
        { inPixels.left()  - margin, inPixels.center().y() },
        { inPixels.right() + margin, inPixels.center().y() },
        { inPixels.left()  - margin, inPixels.top()    - margin },
        { inPixels.right() + margin, inPixels.top()    - margin },
        { inPixels.left()  - margin, inPixels.bottom() + margin },
        { inPixels.right() + margin, inPixels.bottom() + margin },
    };
    for (const QPoint &probe : probes) {
        if (!image.rect().contains(probe)) continue;
        const QColor sample = image.pixelColor(probe);
        reds   << sample.red();
        greens << sample.green();
        blues  << sample.blue();
    }
    if (reds.isEmpty()) return fallback;
    const auto median = [](QList<int> values) {
        std::sort(values.begin(), values.end());
        return values.at(values.size() / 2);
    };
    return QColor(median(reds), median(greens), median(blues));
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

// ── Frames ───────────────────────────────────────────────────────────────────

void MediaLayer::clearFrames()
{
    for (Placed &placed : m_placed)
        delete placed.frame;
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

    // Looked up by frame and not by index: indices shift once an entry goes,
    // and then a click on one video plays another.
    connect(frame, &MediaFrame::activated, this, [this, frame]() {
        for (const Placed &placed : m_placed)
            if (placed.frame == frame) { playPlaced(placed); return; }
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
    const QRect canvasRect = pdfToCanvas(placed.page, placed.bounds);
    if (canvasRect.isNull()) {
        placed.frame->hide();
        return;
    }
    placed.frame->setGeometry(canvasRect);
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

    // The player hangs where its frame does.
    if (m_player) {
        for (const Placed &placed : m_placed) {
            if (placed.frame != m_player->property("mediaFrame").value<QObject *>())
                continue;
            const QRect canvasRect = pdfToCanvas(placed.page, placed.bounds);
            if (!canvasRect.isNull()) m_player->setGeometry(canvasRect);
        }
    }
}

// ── Coordinates ──────────────────────────────────────────────────────────────

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

// ── Drag-to-frame ────────────────────────────────────────────────────────────

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

        // Clamp to the page the drag started on.
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

// ── Panel ────────────────────────────────────────────────────────────────────

void MediaLayer::bindPanel()
{
    RichMediaPanel *panel = richMediaPanel();
    if (!panel || m_panel == panel) return;

    // One panel per window, one layer per tab. Whoever holds the tool binds
    // to it, and the previous connection is cut, or the wrong document gets
    // the video.
    unbindPanel();
    m_panel = panel;
    connect(panel, &RichMediaPanel::insertRequested,
            this, &MediaLayer::commitInsert, Qt::UniqueConnection);
    connect(panel, &RichMediaPanel::placementEdited, this, [this](const QRectF &bounds) {
        if (!m_placement || m_placementPage < 0) return;
        m_placementBounds = bounds;
        m_placement->setGeometry(pdfToCanvas(m_placementPage, bounds));
    }, Qt::UniqueConnection);
    connect(panel, &RichMediaPanel::previewChanged, this, [this](const QImage &poster) {
        if (m_placement) m_placement->setPoster(poster);
    }, Qt::UniqueConnection);

    if (m_placementPage >= 0)
        panel->setPlacement(m_placementPage, m_placementBounds);
    else
        panel->clearPlacement();
}

void MediaLayer::unbindPanel()
{
    if (m_panel) disconnect(m_panel, nullptr, this, nullptr);
    m_panel = nullptr;
}

// ── Inserting ────────────────────────────────────────────────────────────────

/// Makes sure the source is something that plays everywhere, or that the user
/// knows it is not. Empty return means cancelled.
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
    // The converted file is ours now and goes with the session.
    m_tempFiles << target;
    return target;
}

void MediaLayer::commitInsert(const MediaSpec &requested)
{
    MediaSpec spec = requested;
    spec.page   = m_placementPage;
    spec.bounds = m_placementBounds;
    if (!spec.isValid()) return;

    // Check before inserting: a document that shows only a still image at the
    // recipient is the costliest failure here, because it surfaces after the
    // file was sent.
    if (spec.type == MediaSpec::Type::Video) {
        const QString playable = ensurePlayableSource(spec.source);
        if (playable.isEmpty()) return;
        if (playable != spec.source) {
            spec.displayName = MediaDrop::displayNameFor(spec.source, playable);
            spec.source      = playable;
            spec.mimeType    = QStringLiteral("video/mp4");
        }
    }

    // The poster is taken now and not at save time, or the image in the
    // document would depend on the source file still being there.
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

// ── Playing ──────────────────────────────────────────────────────────────────

/// The facts a report needs to be worth reading.
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

/// What to say when the built-in player gives up.
///
/// The facts about the file, because "could not be played" alone sends the
/// user looking for a fault in their video. And two ways out that they choose:
/// a smaller copy, which is what a decoder refusing 4K actually needs, and the
/// player outside the program. The setting forbids handing media out behind
/// the user's back, not offering it once the program has failed in front of
/// them.
void MediaLayer::reportPlaybackFailure(const QString &filePath,
                                       const QString &message)
{
    // One failure, one dialog. A player can report the same trouble more than
    // once on its way down, and a box that keeps coming back looks broken.
    if (m_reportingFailure) return;
    m_reportingFailure = true;
    const auto done = qScopeGuard([this] { m_reportingFailure = false; });

    const MediaFormat::Info info = MediaFormat::inspect(filePath);
    const bool large = info.size.height() > 1080;

    // Built as whole sentences and joined, so a missing piece cannot leave a
    // stray full stop standing on its own.
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
    // Everything needed to tell the two possible causes apart, in a block the
    // user can copy: a decoder that will not take this file, or a media
    // backend that never loaded in the first place. Without it the same
    // report comes back with no more information than the last one.
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

/// Makes a 1080p copy next to the extracted file and plays that.
///
/// Only the copy being played is smaller. What sits in the document is
/// untouched, so the recipient still gets the video the author put there.
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
        }, /*maxHeight=*/1080);
        progress.close();

        if (!ok) {
            if (!progress.wasCanceled())
                QMessageBox::warning(parent, tr("Rich Media"),
                                     tr("The smaller copy could not be made."));
            return;
        }
        m_tempFiles << target;
    }

    // Remembered under the original, so every later click goes straight to it.
    m_smallerCopies.insert(filePath, target);

    for (const Placed &placed : m_placed) {
        if (placed.pending || !placed.asset.isEmbedded()) continue;
        playFile(filePath, placed);
        return;
    }
}

bool MediaLayer::confirmPlayback(const Placed &placed)
{
    // What the user just inserted needs no clearance; they picked the file.
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
    // The careful answer is the preset one: dismissing the question allows
    // nothing.
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
        // Not saved yet, so the source file is still where it came from.
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
    // A medium that needed a smaller copy once needs it every time. Without
    // this the next click runs into the same failure and the same dialog.
    const QString playable = m_smallerCopies.value(filePath, filePath);

    AppSettings settings;
    const QString mode = settings.mediaPlayback();

    const bool wantsInApp = (mode == QLatin1String("inapp"));
    const QString command = mode == QLatin1String("custom")
                          ? settings.customPlayerCommand() : QString();

    // "In OpenPDF Studio" means exactly that. It never hands the file to a
    // player outside this program, not even when the built-in one fails: the
    // user picked where their media may be opened, and quietly starting a
    // foreign process against that choice is worse than not playing at all.
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
        // The poster stands until the first frame arrives. For a medium
        // already in the document that is exactly the piece of rendered page
        // the player is about to cover.
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

    // The other two modes are the user asking for a player outside this
    // program, so here it is allowed.
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

// ── Dropping ─────────────────────────────────────────────────────────────────

bool MediaLayer::acceptsDroppedFile(const QString &path) const
{
    return !m_documentPath.isEmpty() && RichMediaWriter::available()
        && MediaDrop::isVideoFile(path);
}

bool MediaLayer::handleDroppedFile(const QString &path, int page,
                                   QString *newDocument)
{
    if (!acceptsDroppedFile(path)) return false;

    // The file is ours from here on even if nothing comes of it, or the
    // caller would go on to place a video as an image.
    const QString source = ensurePlayableSource(path);
    if (source.isEmpty()) return true;

    const QString work = MediaDrop::addAsOwnPage(
        m_documentPath, source, page >= 0 ? page : m_canvas->pageCount() - 1,
        MediaDrop::displayNameFor(path, source));
    if (work.isEmpty()) {
        QMessageBox::warning(QApplication::activeWindow(), tr("Rich Media"),
                             tr("The video could not be added as a new page."));
        return true;
    }
    *newDocument = work;
    return true;
}

// ── Saving ───────────────────────────────────────────────────────────────────

bool MediaLayer::writeTo(const QString &stagingPath)
{
    if (m_session.isEmpty()) return true;
    if (!RichMediaWriter::available()) {
        qWarning() << "[rich-media] nothing can be written without qpdf";
        return false;
    }
    // The staging file inherits the document's encryption but sits under a
    // path the password store knows nothing about.
    return RichMediaWriter::apply(stagingPath, m_session,
                                  PdfPwStore::get(m_documentPath));
}
