#include "ui/view/ImageAnnotationLayer.hpp"

#include "ui/theme/Theme.hpp"
#include "ui/view/ImageAnnotation.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditCodec.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif
#include "engine/import/ImageImport.hpp"

#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QRubberBand>
#include <QPixmap>
#include <QStyleOptionMenuItem>
#include <QTransform>
#include <QWidget>

#include <algorithm>

namespace {

// QKeySequence names the key through this; Qt's own catalogue is not loaded.
[[maybe_unused]] const char *const DelKeyName = QT_TRANSLATE_NOOP("QShortcut", "Del");

QColor dangerColor()
{
    return Theme::DarkMode ? QColor(QStringLiteral("#F87171"))
                           : QColor(QStringLiteral("#DC2626"));
}

/// Image context menu that draws its destructive entry in the danger colour.
class ImageMenu : public QMenu
{
public:
    using QMenu::QMenu;
    QAction *danger { nullptr };

protected:
    void initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const override
    {
        QMenu::initStyleOption(option, action);
        if (!action->isEnabled()) return;
        const QColor text = action == danger ? dangerColor()
                                             : option->palette.color(QPalette::ButtonText);
        for (const QPalette::ColorRole role : { QPalette::Text, QPalette::ButtonText,
                                                QPalette::WindowText, QPalette::HighlightedText })
            option->palette.setColor(role, text);
    }
};

}

ImageAnnotationLayer::ImageAnnotationLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{}

#ifdef HAVE_PDF_RENDERING
void ImageAnnotationLayer::setSource(PdfRenderer *renderer, EditSession *session)
{
    m_renderer = renderer;
    m_session  = session;
    clearDetectedHighlights();
}
#endif

void ImageAnnotationLayer::setToolActive(bool active)
{
    m_toolActive = active;
    if (!active) clearDetectedHighlights();
    for (const Entry &e : m_placed)
        if (e.widget)
            static_cast<ImageAnnotation *>(e.widget)->setEditActive(active);
}

QList<ImageAnnotationLayer::Placed> ImageAnnotationLayer::placedImages() const
{
    QList<Placed> out;
    out.reserve(m_placed.size());
    for (const Entry &e : m_placed)
        if (e.key.isEmpty()) out.append({ e.page, e.pdfBounds, e.image });
    return out;
}

void ImageAnnotationLayer::place(const QImage &img, const QPoint &canvasPos)
{
    if (img.isNull() || m_canvas->pageLabelCount() == 0) return;

    auto [pageIdx, lbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0 || !lbl) {

        pageIdx = 0;
        lbl = m_canvas->pageLabel(0);
        if (!lbl) return;
    }

    const qreal scale = m_canvas->screenScale();

    const QPoint localPx = canvasPos - lbl->pos();
    const qreal ptX = localPx.x() / scale;
    const qreal ptY = localPx.y() / scale;

    qreal ptW = img.width()  / scale;
    qreal ptH = img.height() / scale;
#ifdef HAVE_PDF_RENDERING
    if (m_renderer && m_canvas->pageCount() > 0) {
        const QSizeF pagePts = m_renderer->pageDisplaySize(pageIdx, 100);
        const qreal  maxW    = pagePts.width() - ptX;
        if (ptW > maxW) { ptH *= maxW / ptW; ptW = maxW; }
    }
#endif
    const QRectF pdfBounds(ptX, ptY, ptW, ptH);

    addEntry(pageIdx, pdfBounds, img);

#ifdef HAVE_PDF_RENDERING
    if (m_session) m_session->addImageEdit(pageIdx, pdfBounds, img);
    Q_EMIT pageNeedsRerender(pageIdx);
    Q_EMIT imageAdded(pageIdx);
#endif
}

void ImageAnnotationLayer::addEntry(int page, const QRectF &pdfBounds,
                                    const QImage &img, const QRect &canvasRect,
                                    const QList<int> &key)
{
    const QLabel *lbl = page >= 0 && page < m_canvas->pageLabelCount()
                            ? m_canvas->pageLabel(page) : nullptr;
    if (!lbl) return;

    const qreal scale = m_canvas->screenScale();
    const QRect geo = canvasRect.isNull()
        ? QRect(lbl->pos().x() + qRound(pdfBounds.left() * scale),
                lbl->pos().y() + qRound(pdfBounds.top()  * scale),
                qRound(pdfBounds.width()  * scale),
                qRound(pdfBounds.height() * scale))
        : canvasRect;

    auto *ann = new ImageAnnotation(QString(), m_canvas->canvasWidget());
    ann->setOriginalPixmap(QPixmap::fromImage(img));
    ann->setEditActive(m_toolActive);
    ann->setPageRect(lbl->geometry());
    ann->setGeometry(geo);
    ann->show();

    m_placed.append({ page, pdfBounds, img, ann, key, pdfBounds });
    connectAnnotation(ann);
}

void ImageAnnotationLayer::restoreImages(const QList<Placed> &images)
{
    QList<int> touched;
    for (const Entry &e : std::as_const(m_placed))
        if (e.key.isEmpty() && !touched.contains(e.page)) touched.append(e.page);

    m_placed.removeIf([](const Entry &e) {
        if (!e.key.isEmpty()) return false;
        delete e.widget;
        return true;
    });

#ifdef HAVE_PDF_RENDERING
    if (m_session) m_session->clearImageEdits();
#endif

    for (const Placed &p : images) {
        addEntry(p.page, p.pdfBounds, p.image);
        if (!touched.contains(p.page)) touched.append(p.page);
#ifdef HAVE_PDF_RENDERING
        if (m_session) m_session->addImageEdit(p.page, p.pdfBounds, p.image);
#endif
    }

    for (int page : std::as_const(touched))
        Q_EMIT pageNeedsRerender(page);
}

void ImageAnnotationLayer::placeInRect(const QImage &img, const QRect &canvasRect)
{
    if (img.isNull() || m_canvas->pageLabelCount() == 0) return;

    auto [pageIdx, lbl] = m_canvas->pageAtCanvasPos(canvasRect.topLeft());
    if (pageIdx < 0 || !lbl) {
        pageIdx = 0;
        lbl = m_canvas->pageLabel(0);
        if (!lbl) return;
    }

    const qreal  scale   = m_canvas->screenScale();
    const QPoint localTL = canvasRect.topLeft() - lbl->pos();
    const QRectF pdfBounds(localTL.x() / scale, localTL.y() / scale,
                           canvasRect.width() / scale, canvasRect.height() / scale);

    addEntry(pageIdx, pdfBounds, img, canvasRect);

#ifdef HAVE_PDF_RENDERING
    if (m_session) m_session->addImageEdit(pageIdx, pdfBounds, img);
    Q_EMIT pageNeedsRerender(pageIdx);
    Q_EMIT imageAdded(pageIdx);
#endif
}

void ImageAnnotationLayer::connectAnnotation(ImageAnnotation *ann)
{
    connect(ann, &ImageAnnotation::deleteRequested, this, [this, ann]() {
        for (int i = 0; i < m_placed.size(); ++i) {
            if (m_placed[i].widget != ann) continue;
#ifdef HAVE_PDF_RENDERING
            if (!m_placed[i].key.isEmpty())
                changeOriginal(m_placed[i], [](EditSession::ImageChange &c) { c.removed = true; });
            else if (m_session)
                m_session->removeImageEdit(m_placed[i].page, m_placed[i].pdfBounds);
            Q_EMIT pageNeedsRerender(m_placed[i].page);
            Q_EMIT imageRemoved(m_placed[i].page);
#endif
            m_placed.removeAt(i);
            break;
        }
        ann->deleteLater();
    });

    connect(ann, &ImageAnnotation::geometryChanged, this, [this, ann](const QRect &newGeo) {
        for (Entry &placed : m_placed) {
            if (placed.widget != ann) continue;
#ifdef HAVE_PDF_RENDERING
            const qreal sc = m_canvas->screenScale();
            if (placed.page < m_canvas->pageLabelCount()) {
                const QLabel *lbl2 = m_canvas->pageLabel(placed.page);
                if (!lbl2) break;
                const QPoint  local = newGeo.topLeft() - lbl2->pos();
                const QRectF  oldBounds = placed.pdfBounds;
                // A click is no change, and a move keeps the size the pixels only round.
                const QRect   oldGeo = canvasRect(placed.page, oldBounds);
                if (newGeo == oldGeo) break;
                const bool resized = newGeo.size() != oldGeo.size();
                placed.pdfBounds = resized
                    ? QRectF(local.x() / sc, local.y() / sc, newGeo.width() / sc, newGeo.height() / sc)
                    : QRectF(QPointF(local.x() / sc, local.y() / sc), oldBounds.size());
                const QRectF  to = placed.pdfBounds;
                if (!placed.key.isEmpty()) {
                    changeOriginal(placed, [to](EditSession::ImageChange &c) { c.to = to; });
                } else if (m_session) {
                    m_session->removeImageEdit(placed.page, oldBounds);
                    m_session->addImageEdit(placed.page, placed.pdfBounds, placed.image);
                }
                Q_EMIT pageNeedsRerender(placed.page);
                Q_EMIT imageChanged(placed.page, resized ? tr("Image resized") : tr("Image moved"));
            }
#endif
            break;
        }
    });

    connect(ann, &ImageAnnotation::copyRequested, this, [this, ann]() {
        copyImage(ann);
    });
    connect(ann, &ImageAnnotation::cutRequested, this, [this, ann]() {
        copyImage(ann);
        ann->deleteRequested();
    });

    connect(ann, &ImageAnnotation::pasteRequested, this, [this, ann]() {
        pasteImage(ann, {});
    });

    connect(ann, &ImageAnnotation::contextMenuRequested, this,
            [this, ann](const QPoint &globalPos) {
        showContextMenu(ann, globalPos);
    });
}

void ImageAnnotationLayer::showContextMenu(ImageAnnotation *ann, const QPoint &globalPos)
{

    const auto dvTr = [](const char *s) {
        return QCoreApplication::translate("DocumentView", s);
    };
    const auto icon = [](const char *name) {
        return Theme::makeIcon(QString::fromLatin1(name));
    };
    const QPoint canvasPos = m_canvas->canvasWidget()->mapFromGlobal(globalPos);
    int          region    = ann ? -1 : detectedAt(canvasPos);
    const bool   onPage    = ann || m_canvas->pageAtCanvasPos(canvasPos).first >= 0;
    const QMimeData *clip  = QGuiApplication::clipboard()->mimeData();

    ImageMenu menu(m_canvas->canvasWidget());
    menu.setObjectName(QStringLiteral("ImageMenu"));
    QAction *copy  = menu.addAction(icon("copy"),
                                    dvTr(QT_TRANSLATE_NOOP("DocumentView", "Copy")));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(ann || region >= 0);
    QAction *paste = menu.addAction(icon("clipboard-paste"),
                                    dvTr(QT_TRANSLATE_NOOP("DocumentView", "Paste")));
    paste->setShortcut(QKeySequence::Paste);
    paste->setEnabled(onPage && clip && clip->hasImage());
    menu.addSeparator();
    QAction *replace = menu.addAction(icon("image"), tr("Replace image"));
    menu.addSeparator();
    QAction *left  = menu.addAction(icon("rotate-ccw"), tr("Rotate 90° left"));
    QAction *right = menu.addAction(icon("rotate-cw"),  tr("Rotate 90° right"));
    menu.addSeparator();
    QAction *del   = menu.addAction(Theme::makeIcon(QStringLiteral("trash-2"), dangerColor(),
                                                    dangerColor()),
                                    dvTr(QT_TRANSLATE_NOOP("DocumentView", "Delete")));
    del->setShortcut(QKeySequence(Qt::Key_Delete));
    menu.danger = del;
    for (QAction *a : { replace, left, right, del })
        a->setEnabled(ann || region >= 0);

    QAction *triggered = menu.exec(globalPos);
    if (!triggered) return;

    // An image of the page itself is taken out of it before it changes.
    if (region >= 0 && (triggered == copy || triggered == del)) {
        if (triggered == copy) QGuiApplication::clipboard()->setImage(detectedImage(region));
        else                   removeDetected(region);
        return;
    }
    if (region >= 0 && (triggered == replace || triggered == left || triggered == right))
        ann = liftDetected(region);
    if (!ann && triggered != paste) return;

    if (triggered == copy)
        copyImage(ann);
    else if (triggered == paste)
        pasteImage(ann, canvasPos);
    else if (triggered == del)
        ann->deleteRequested();
    else if (triggered == replace)
        replaceImage(ann);
    else if (triggered == left || triggered == right)
        rotateImage(ann, triggered == left ? -90 : 90);
}

void ImageAnnotationLayer::changeOriginal(const Entry &e,
                                          const std::function<void(EditSession::ImageChange &)> &edit)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return;
    QList<EditSession::ImageChange> changes = m_session->imageChanges();
    auto it = std::find_if(changes.begin(), changes.end(), [&e](const EditSession::ImageChange &c) {
        return c.page == e.page && c.key == e.key;
    });
    if (it == changes.end()) {
        changes.append({ e.page, e.key, e.from, e.from });
        it = changes.end() - 1;
    }
    edit(*it);
    m_session->replaceImageChanges(std::move(changes));
#else
    Q_UNUSED(e) Q_UNUSED(edit)
#endif
}

ImageAnnotationLayer::Entry *ImageAnnotationLayer::entryFor(const ImageAnnotation *ann)
{
    for (Entry &e : m_placed)
        if (e.widget == ann) return &e;
    return nullptr;
}

void ImageAnnotationLayer::copyImage(const ImageAnnotation *ann)
{
    if (const Entry *e = entryFor(ann))
        QGuiApplication::clipboard()->setImage(e->image);
}

void ImageAnnotationLayer::pasteImage(const ImageAnnotation *ann, const QPoint &canvasPos)
{
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) return;
    if (!ann) { place(img, canvasPos); return; }
    QSize size = img.size();
    size.scale(ann->size(), Qt::KeepAspectRatio);
    placeInRect(img, QRect(ann->pos() + QPoint(20, 20), size));
}

void ImageAnnotationLayer::rotateImage(ImageAnnotation *ann, int degrees)
{
    Entry *e = entryFor(ann);
    const QLabel *lbl = e && e->page < m_canvas->pageLabelCount()
                            ? m_canvas->pageLabel(e->page) : nullptr;
    if (!lbl) return;

    // The box turns about its centre and stays on the page.
    const QSizeF page = QSizeF(lbl->size()) / m_canvas->screenScale();
    QSizeF size = e->pdfBounds.size().transposed();
    size.scale(size.boundedTo(page), Qt::KeepAspectRatio);
    QRectF bounds(QPointF(), size);
    bounds.moveCenter(e->pdfBounds.center());
    bounds.moveLeft(qBound(0.0, bounds.left(), page.width()  - bounds.width()));
    bounds.moveTop (qBound(0.0, bounds.top(),  page.height() - bounds.height()));

    changeEntry(*e, bounds, e->image.transformed(QTransform().rotate(degrees)),
                [degrees](EditSession::ImageChange &c) {
        if (c.replacement.isNull()) c.quarterTurns = (c.quarterTurns + degrees / 90 + 4) % 4;
        else c.replacement = c.replacement.transformed(QTransform().rotate(degrees));
    });
    Q_EMIT imageChanged(e->page, tr("Image rotated"));
}

void ImageAnnotationLayer::replaceImage(ImageAnnotation *ann)
{
    const QPointer<ImageAnnotation> guard(ann);
    const QString path = QFileDialog::getOpenFileName(m_canvas->canvasWidget(),
        tr("Replace image"), {},
        ImageImport::nameFilter() + QStringLiteral(";;") + tr("All files (*)"));
    if (path.isEmpty() || !guard) return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    Entry *e = entryFor(ann);
    if (img.isNull() || !e) return;

    // The new image fills the old box as far as its proportions allow.
    QSizeF size(img.size());
    size.scale(e->pdfBounds.size(), Qt::KeepAspectRatio);
    QRectF bounds(QPointF(), size);
    bounds.moveCenter(e->pdfBounds.center());

    changeEntry(*e, bounds, img, [img](EditSession::ImageChange &c) {
        c.replacement  = img;
        c.quarterTurns = 0;
    });
    Q_EMIT imageChanged(e->page, tr("Image replaced"));
}

void ImageAnnotationLayer::changeEntry(Entry &e, const QRectF &pdfBounds, const QImage &img,
                                       const std::function<void(EditSession::ImageChange &)> &original)
{
#ifdef HAVE_PDF_RENDERING
    if (!e.key.isEmpty()) {
        changeOriginal(e, [&](EditSession::ImageChange &c) { original(c); c.to = pdfBounds; });
    } else if (m_session) {
        m_session->removeImageEdit(e.page, e.pdfBounds);
        m_session->addImageEdit(e.page, pdfBounds, img);
    }
#else
    Q_UNUSED(original)
#endif
    e.pdfBounds = pdfBounds;
    e.image     = img;

    auto *ann = static_cast<ImageAnnotation *>(e.widget);
    ann->setOriginalPixmap(QPixmap::fromImage(img));
    if (const QLabel *lbl = m_canvas->pageLabel(e.page)) {
        const qreal scale = m_canvas->screenScale();
        ann->setGeometry(lbl->pos().x() + qRound(pdfBounds.left()   * scale),
                         lbl->pos().y() + qRound(pdfBounds.top()    * scale),
                         qRound(pdfBounds.width()  * scale),
                         qRound(pdfBounds.height() * scale));
    }
    Q_EMIT pageNeedsRerender(e.page);
}

bool ImageAnnotationLayer::handlePress(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    auto [pageIdx, lbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0 || !lbl) return false;
    m_dragPageRect = lbl->geometry();
#else
    m_dragPageRect = {};
#endif
    m_dragStart = QPoint(
        qBound(m_dragPageRect.left(), canvasPos.x(), m_dragPageRect.right()),
        qBound(m_dragPageRect.top(),  canvasPos.y(), m_dragPageRect.bottom()));
    m_tracking = true;
    m_dragging = false;
    return true;
}

bool ImageAnnotationLayer::handleMove(const QPoint &canvasPos, const QPoint &scroll)
{
    if (!m_tracking) return false;
    QPoint pos = canvasPos;
    if (!m_dragPageRect.isEmpty()) {
        pos.setX(qBound(m_dragPageRect.left(), pos.x(), m_dragPageRect.right()));
        pos.setY(qBound(m_dragPageRect.top(),  pos.y(), m_dragPageRect.bottom()));
    }
    if (!m_dragging && (pos - m_dragStart).manhattanLength() > 6)
        m_dragging = true;
    if (m_dragging) {
        m_band->setGeometry(QRect(m_dragStart - scroll, pos - scroll).normalized());
        m_band->show();
    }
    return true;
}

bool ImageAnnotationLayer::handleRelease(const QPoint &scroll)
{
    if (!m_tracking) return false;
    m_tracking = false;
    if (!m_dragging) return true;
    m_dragging = false;

    const QRect band = m_band->geometry();
    m_band->hide();
    if (band.width() <= 20 || band.height() <= 20) return true;
    const QString path = QFileDialog::getOpenFileName(m_canvas->canvasWidget(),
        tr("Insert image"), {},
        ImageImport::nameFilter() + QStringLiteral(";;") + tr("All files (*)"));
    if (path.isEmpty()) return true;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (!img.isNull()) placeInRect(img, band.translated(scroll));
    return true;
}

void ImageAnnotationLayer::relayout()
{
    if (m_hovered >= 0)
        m_hoverFrame->setGeometry(canvasRect(m_detected[m_hovered].page,
                                             m_detected[m_hovered].pdfRect));
    if (m_placed.isEmpty()) return;
#ifdef HAVE_PDF_RENDERING
    const qreal scale = m_canvas->screenScale();
    for (const Entry &e : m_placed) {
        if (!e.widget || e.page >= m_canvas->pageLabelCount()) continue;
        const QLabel *lbl = m_canvas->pageLabel(e.page);
        if (!lbl) continue;
        const int x = lbl->pos().x() + qRound(e.pdfBounds.left() * scale);
        const int y = lbl->pos().y() + qRound(e.pdfBounds.top()  * scale);
        const int w = qRound(e.pdfBounds.width()  * scale);
        const int h = qRound(e.pdfBounds.height() * scale);
        e.widget->setGeometry(x, y, w, h);
        static_cast<ImageAnnotation *>(e.widget)->setPageRect(lbl->geometry());
    }
#endif
}

void ImageAnnotationLayer::clear()
{
    for (const Entry &e : m_placed) delete e.widget;
    m_placed.clear();
    clearDetectedHighlights();
}

void ImageAnnotationLayer::clearDetectedHighlights()
{
    m_detected.clear();
    m_scanned.clear();
    m_hovered = -1;
    if (m_hoverFrame) m_hoverFrame->hide();
}

QRect ImageAnnotationLayer::canvasRect(int page, const QRectF &pdfRect) const
{
    const QLabel *lbl = page < m_canvas->pageLabelCount() ? m_canvas->pageLabel(page) : nullptr;
    if (!lbl) return {};
    const qreal scale = m_canvas->screenScale();
    return QRect(lbl->pos().x() + qRound(pdfRect.left() * scale),
                 lbl->pos().y() + qRound(pdfRect.top()  * scale),
                 qRound(pdfRect.width()  * scale),
                 qRound(pdfRect.height() * scale));
}

void ImageAnnotationLayer::scanPage(int page)
{
#ifdef HAVE_PDF_RENDERING
    if (page < 0 || m_scanned.contains(page) || !m_renderer || !m_renderer->backend()) return;
    m_scanned.insert(page);

    // A picture filling the page is its background, not an image on it.
    const QSizeF size = m_renderer->backend()->pageSizePts(page);
    const qreal  pageArea = size.width() * size.height();
    for (const PdfBackend::PageImage &image : m_renderer->backend()->pageImages(page)) {
        if (pageArea > 0 && image.bounds.width() * image.bounds.height() > 0.9 * pageArea)
            continue;
        const bool changed = m_session && std::any_of(
            m_session->imageChanges().cbegin(), m_session->imageChanges().cend(),
            [&](const EditSession::ImageChange &c) { return c.page == page && c.key == image.key; });
        const bool lifted = std::any_of(m_placed.cbegin(), m_placed.cend(), [&](const Entry &e) {
            return e.page == page && e.key == image.key;
        });
        if (!changed && !lifted) m_detected.append({ page, image.bounds, image.key });
    }
#else
    Q_UNUSED(page)
#endif
}

void ImageAnnotationLayer::scanVisiblePage(int firstVisiblePage)
{
    clearDetectedHighlights();
    scanPage(firstVisiblePage);
}

int ImageAnnotationLayer::detectedAt(const QPoint &canvasPos)
{
    scanPage(m_canvas->pageAtCanvasPos(canvasPos).first);
    // Of nested images the innermost.
    int    best = -1;
    qreal  bestArea = 0;
    for (int i = 0; i < m_detected.size(); ++i) {
        if (!canvasRect(m_detected[i].page, m_detected[i].pdfRect).contains(canvasPos)) continue;
        const qreal area = m_detected[i].pdfRect.width() * m_detected[i].pdfRect.height();
        if (best < 0 || area < bestArea) { best = i; bestArea = area; }
    }
    return best;
}

QImage ImageAnnotationLayer::detectedImage(int index) const
{
#ifdef HAVE_PDF_RENDERING
    if (index < 0 || index >= m_detected.size() || !m_renderer) return {};
    const Detected &d = m_detected[index];
    if (m_renderer->backend()) {
        const QImage own = m_renderer->backend()->pageImage(d.page, d.key);
        if (!own.isNull()) return own;
    }
    const qreal  scale = qMax(m_canvas->screenScale(), 2.0);
    const QImage page  = m_renderer->renderPage(d.page, scale);
    const QRectF px(d.pdfRect.topLeft() * scale, d.pdfRect.size() * scale);
    return page.isNull() ? QImage() : page.copy(px.toAlignedRect());
#else
    Q_UNUSED(index)
    return {};
#endif
}

bool ImageAnnotationLayer::hoverAt(const QPoint &canvasPos)
{
    const int hit = m_tracking ? -1 : detectedAt(canvasPos);
    if (hit == m_hovered) return hit >= 0;
    m_hovered = hit;
    if (hit < 0) { if (m_hoverFrame) m_hoverFrame->hide(); return false; }
    if (!m_hoverFrame) {
        m_hoverFrame = new QFrame(m_canvas->canvasWidget());
        m_hoverFrame->setObjectName(QStringLiteral("ImageRegionOverlay"));
        m_hoverFrame->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    m_hoverFrame->setGeometry(canvasRect(m_detected[hit].page, m_detected[hit].pdfRect));
    m_hoverFrame->raise();
    m_hoverFrame->show();
    return true;
}

ImageAnnotation *ImageAnnotationLayer::liftDetected(int index)
{
    const QImage image = detectedImage(index);
    if (image.isNull()) return nullptr;
    const Detected d = m_detected.takeAt(index);
    m_hovered = -1;
    if (m_hoverFrame) m_hoverFrame->hide();
    addEntry(d.page, d.pdfRect, image, QRect(), d.key);
    return static_cast<ImageAnnotation *>(m_placed.last().widget);
}

void ImageAnnotationLayer::removeDetected(int index)
{
    if (ImageAnnotation *ann = liftDetected(index)) ann->deleteRequested();
}

bool ImageAnnotationLayer::takeDetectedRegionAt(const QPoint &canvasPos)
{
    const int hit = detectedAt(canvasPos);
    if (hit < 0) return false;
    liftDetected(hit);
    return true;
}

QByteArray ImageAnnotationLayer::changeState() const
{
#ifdef HAVE_PDF_RENDERING
    if (m_session) return EditCodec::encode(m_session->imageChanges());
#endif
    return {};
}

void ImageAnnotationLayer::restoreChanges(const QByteArray &data)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return;
    QList<EditSession::ImageChange> changes;
    if (!data.isEmpty() && !EditCodec::decode(data, &changes)) return;
    if (changes == m_session->imageChanges()) return;

    QList<int> touched;
    for (const auto *list : { &m_session->imageChanges(), &std::as_const(changes) })
        for (const EditSession::ImageChange &c : *list)
            if (!touched.contains(c.page)) touched.append(c.page);
    m_placed.removeIf([](const Entry &e) {
        if (e.key.isEmpty()) return false;
        delete e.widget;
        return true;
    });
    m_session->replaceImageChanges(changes);
    for (const EditSession::ImageChange &c : std::as_const(changes)) {
        if (c.removed) continue;
        QImage image = c.replacement;
        if (image.isNull() && m_renderer && m_renderer->backend())
            image = m_renderer->backend()->pageImage(c.page, c.key)
                        .transformed(QTransform().rotate(90.0 * c.quarterTurns));
        addEntry(c.page, c.to, image, QRect(), c.key);
        m_placed.last().from = c.from;
    }
    clearDetectedHighlights();
    for (int page : std::as_const(touched)) Q_EMIT pageNeedsRerender(page);
#else
    Q_UNUSED(data)
#endif
}
