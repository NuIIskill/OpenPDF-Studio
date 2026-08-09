#include "ImageAnnotationLayer.hpp"
#include "app/PdfPwStore.hpp"

#include "ui/tools/ImageAnnotation.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#  include "engine/view/PdfRenderer.hpp"
#endif
#include "engine/ocr/OcrEngine.hpp"

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <array>
#  include <cstring>
#  include <set>
#  include <string>
#  include <vector>
#endif

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QWidget>

ImageAnnotationLayer::ImageAnnotationLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{}

#ifdef HAVE_PDF_RENDERING
void ImageAnnotationLayer::setSource(PdfRenderer *renderer, EditSession *session,
                                     OcrEngine *ocr, const QString &filePath)
{
    m_renderer = renderer;
    m_session  = session;
    m_ocr      = ocr;
    m_filePath = filePath;
}
#endif

void ImageAnnotationLayer::setToolActive(bool active)
{
    m_toolActive = active;
    for (const Entry &e : m_placed)
        if (e.widget)
            static_cast<ImageAnnotation *>(e.widget)->setEditActive(active);
}

QList<ImageAnnotationLayer::Placed> ImageAnnotationLayer::placedImages() const
{
    QList<Placed> out;
    out.reserve(m_placed.size());
    for (const Entry &e : m_placed)
        out.append({ e.page, e.pdfBounds, e.image });
    return out;
}

// ── Placing ───────────────────────────────────────────────────────────────────

void ImageAnnotationLayer::place(const QImage &img, const QPoint &canvasPos)
{
    if (img.isNull() || m_canvas->pageLabelCount() == 0) return;

    auto [pageIdx, lbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0 || !lbl) {
        // If click landed in scroll margins, use the first visible page.
        pageIdx = 0;
        lbl = m_canvas->pageLabel(0);
        if (!lbl) return;
    }

    const qreal scale = m_canvas->screenScale();
    // Position within the page in PDF points.
    const QPoint localPx = canvasPos - lbl->pos();
    const qreal ptX = localPx.x() / scale;
    const qreal ptY = localPx.y() / scale;

    // Natural image size in PDF points: keep 1 image-pixel = 1 pt, capped at page width.
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

    // Create overlay widget.
    const int px = qRound(ptX * scale), py = qRound(ptY * scale);
    const int pw = qRound(ptW * scale), ph = qRound(ptH * scale);
    auto *ann = new ImageAnnotation(QString(), m_canvas->canvasWidget());
    ann->setOriginalPixmap(QPixmap::fromImage(img));
    ann->setEditActive(m_toolActive);
    ann->setPageRect(lbl->geometry());
    ann->setGeometry(lbl->pos().x() + px, lbl->pos().y() + py, pw, ph);
    ann->show();

    m_placed.append({ pageIdx, pdfBounds, img, ann });
    connectAnnotation(ann);

#ifdef HAVE_PDF_RENDERING
    if (m_session) m_session->addImageEdit(pageIdx, pdfBounds, img);
    Q_EMIT pageNeedsRerender(pageIdx);
#endif
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

    auto *ann = new ImageAnnotation(QString(), m_canvas->canvasWidget());
    ann->setOriginalPixmap(QPixmap::fromImage(img));
    ann->setEditActive(m_toolActive);
    ann->setPageRect(lbl->geometry());
    ann->setGeometry(canvasRect);
    ann->show();

    m_placed.append({ pageIdx, pdfBounds, img, ann });
    connectAnnotation(ann);

#ifdef HAVE_PDF_RENDERING
    if (m_session) m_session->addImageEdit(pageIdx, pdfBounds, img);
    Q_EMIT pageNeedsRerender(pageIdx);
#endif
}

void ImageAnnotationLayer::connectAnnotation(ImageAnnotation *ann)
{
    connect(ann, &ImageAnnotation::deleteRequested, this, [this, ann]() {
        for (int i = 0; i < m_placed.size(); ++i) {
            if (m_placed[i].widget != ann) continue;
#ifdef HAVE_PDF_RENDERING
            if (m_session) m_session->removeImageEdit(m_placed[i].page,
                                                      m_placed[i].pdfBounds);
            Q_EMIT pageNeedsRerender(m_placed[i].page);
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
                placed.pdfBounds = QRectF(local.x() / sc, local.y() / sc,
                                          newGeo.width() / sc, newGeo.height() / sc);
                if (m_session) {
                    m_session->removeImageEdit(placed.page, oldBounds);
                    m_session->addImageEdit(placed.page, placed.pdfBounds, placed.image);
                }
                Q_EMIT pageNeedsRerender(placed.page);
            }
#endif
            break;
        }
    });

    connect(ann, &ImageAnnotation::contextMenuRequested, this,
            [this, ann](const QPoint &globalPos) {
        showContextMenu(ann, globalPos);
    });
}

void ImageAnnotationLayer::showContextMenu(ImageAnnotation *ann, const QPoint &globalPos)
{
    // These strings were translated under the DocumentView context before the
    // extraction. Naming the context explicitly keeps all 11 existing
    // translations valid instead of orphaning them in every .ts file.
    const auto dvTr = [](const char *s) {
        return QCoreApplication::translate("DocumentView", s);
    };

    QMenu menu(m_canvas->canvasWidget());
    QAction *copy  = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                                    dvTr("Kopieren"));
    QAction *cut   = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")),
                                    dvTr("Ausschneiden"));
    QAction *paste = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-paste")),
                                    dvTr("Einfügen"));
    menu.addSeparator();
    QAction *del   = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                    dvTr("Löschen"));
    paste->setEnabled(!m_clipboard.isNull());

    QAction *triggered = menu.exec(globalPos);
    if (!triggered) return;

    if (triggered == copy || triggered == cut) {
        for (const Entry &e : m_placed)
            if (e.widget == ann) { m_clipboard = e.image; break; }
    }
    if (triggered == paste && !m_clipboard.isNull())
        placeInRect(m_clipboard, ann->geometry().translated(20, 20));
    if (triggered == cut || triggered == del)
        ann->deleteRequested();  // signal → connected lambda removes + deleteLater
}

// ── Drag to frame ─────────────────────────────────────────────────────────────

bool ImageAnnotationLayer::handlePress(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    auto [pageIdx, lbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0 || !lbl) return false;   // outside page
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

QPoint ImageAnnotationLayer::handleMove(const QPoint &canvasPos, bool *dragging)
{
    QPoint pos = canvasPos;
    if (!m_dragPageRect.isEmpty()) {
        pos.setX(qBound(m_dragPageRect.left(), pos.x(), m_dragPageRect.right()));
        pos.setY(qBound(m_dragPageRect.top(),  pos.y(), m_dragPageRect.bottom()));
    }
    if (!m_dragging && (pos - m_dragStart).manhattanLength() > 6)
        m_dragging = true;
    *dragging = m_dragging;
    return pos;
}

bool ImageAnnotationLayer::handleRelease()
{
    m_tracking = false;
    if (!m_dragging) return false;
    m_dragging = false;
    return true;
}

// ── Overlay bookkeeping ───────────────────────────────────────────────────────

void ImageAnnotationLayer::relayout()
{
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
    for (QFrame *f : m_detectedFrames) f->deleteLater();
    m_detectedFrames.clear();
}

bool ImageAnnotationLayer::takeDetectedRegionAt(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    for (int fi = 0; fi < m_detectedFrames.size(); ++fi) {
        if (!m_detectedFrames[fi]->geometry().contains(canvasPos)) continue;
        QFrame *fr = m_detectedFrames[fi];
        auto [pageIdx, lbl] = m_canvas->pageAtCanvasPos(canvasPos);
        if (pageIdx >= 0 && lbl && m_renderer) {
            const qreal  sc      = m_canvas->screenScale();
            const QRect  pageRel = fr->geometry().translated(-lbl->pos());
            const QImage pageImg = m_renderer->renderPage(pageIdx, sc);
            if (!pageImg.isNull()) {
                const QImage cropped = pageImg.copy(pageRel);
                if (!cropped.isNull())
                    placeInRect(cropped, fr->geometry());
            }
        }
        fr->deleteLater();
        m_detectedFrames.removeAt(fi);
        return true;
    }
#else
    Q_UNUSED(canvasPos)
#endif
    return false;
}

// ── qpdf-based PDF image region detector ─────────────────────────────────────
// Recursively parses content streams (page + Form XObjects) tracking the CTM.
// Returns bounding boxes in PDF points, top-left origin.
#ifdef HAVE_QPDF

namespace {

using M6 = std::array<double, 6>;

static M6 identity() { return {1,0,0,1,0,0}; }

// Apply m1 first, then m2.
static M6 compose(const M6 &m1, const M6 &m2) {
    return {
        m1[0]*m2[0] + m1[1]*m2[2],   m1[0]*m2[1] + m1[1]*m2[3],
        m1[2]*m2[0] + m1[3]*m2[2],   m1[2]*m2[1] + m1[3]*m2[3],
        m1[4]*m2[0] + m1[5]*m2[2] + m2[4],
        m1[4]*m2[1] + m1[5]*m2[3] + m2[5]
    };
}

// (x,y) → (x',y') via [a b c d e f]: x'=ax+cy+e, y'=bx+dy+f
static std::pair<double,double> tx(const M6 &m, double x, double y) {
    return { m[0]*x + m[2]*y + m[4], m[1]*x + m[3]*y + m[5] };
}

// Decode PDF name #XX hex escapes so our tokenized names match qpdf's ditems() keys.
static std::string decodePdfName(const std::string &raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '#' && i + 2 < raw.size()) {
            auto hexDigit = [](char c) -> int {
                if (c>='0'&&c<='9') return c-'0';
                if (c>='a'&&c<='f') return c-'a'+10;
                if (c>='A'&&c<='F') return c-'A'+10;
                return -1;
            };
            int hi = hexDigit(raw[i+1]), lo = hexDigit(raw[i+2]);
            if (hi >= 0 && lo >= 0) { out += char(hi*16+lo); i += 2; continue; }
        }
        out += raw[i];
    }
    return out;
}

// InlineImg is a synthetic token emitted for BI...ID...EI blocks; scanStream uses it
// to record the image position from the current CTM without parsing binary pixel data.
struct Tok { enum { Num, Name, Op, InlineImg } type; double num{0}; std::string s; };

static std::vector<Tok> tokenise(const std::string &cs) {
    std::vector<Tok> toks;
    size_t i = 0, n = cs.size();
    auto isWS = [](char c){ return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'; };
    while (i < n) {
        while (i < n && isWS(cs[i])) ++i;
        if (i >= n) break;
        char c = cs[i];
        if (c == '%') { while (i<n && cs[i]!='\n' && cs[i]!='\r') ++i; continue; }
        if (c == '(') {
            int d=1; ++i;
            while (i<n && d>0) {
                if (cs[i]=='\\'){i+=2;continue;}
                if (cs[i]=='(') ++d; else if(cs[i]==')') --d;
                ++i;
            }
            continue;
        }
        if (c=='<' && i+1<n && cs[i+1]!='<') {
            while (i<n && cs[i]!='>') ++i; if(i<n)++i; continue;
        }
        if ((c=='<'&&i+1<n&&cs[i+1]=='<')||(c=='>'&&i+1<n&&cs[i+1]=='>')) { i+=2; continue; }
        if (c=='['||c==']') { ++i; continue; }
        if (c == '/') {
            size_t s0=i++;
            while (i<n && !isWS(cs[i]) && cs[i]!='/' && cs[i]!='[' && cs[i]!=']' &&
                   cs[i]!='('&& cs[i]!=')'&& cs[i]!='<'&& cs[i]!='>') ++i;
            Tok t; t.type=Tok::Name; t.s=decodePdfName(cs.substr(s0,i-s0)); toks.push_back(t);
            continue;
        }
        if (c=='-'||c=='+'||c=='.'||(c>='0'&&c<='9')) {
            size_t s0=i;
            if (cs[i]=='-'||cs[i]=='+') ++i;
            while (i<n&&cs[i]>='0'&&cs[i]<='9') ++i;
            if (i<n&&cs[i]=='.'){++i; while(i<n&&cs[i]>='0'&&cs[i]<='9')++i;}
            Tok t; t.type=Tok::Num;
            try{t.num=std::stod(cs.substr(s0,i-s0));}catch(...){}
            toks.push_back(t); continue;
        }
        {
            size_t s0=i;
            while (i<n&&!isWS(cs[i])&&cs[i]!='/'&&cs[i]!='('&&cs[i]!=')'&&
                   cs[i]!='<'&&cs[i]!='>'&&cs[i]!='['&&cs[i]!=']') ++i;
            if (i==s0){++i;continue;}
            std::string opStr = cs.substr(s0, i-s0);

            if (opStr == "BI") {
                // Inline image: emit a marker, then skip past ID and the binary blob to EI.
                Tok t; t.type=Tok::InlineImg; toks.push_back(t);
                // Advance through the inline-image parameter dict until "ID".
                while (i < n) {
                    while (i < n && isWS(cs[i])) ++i;
                    if (i+1 < n && cs[i]=='I' && cs[i+1]=='D' &&
                        (i+2 >= n || isWS(cs[i+2]))) { i += 2; break; }
                    while (i < n && !isWS(cs[i])) ++i;
                }
                // Raw-scan the binary blob for "EI" preceded and followed by whitespace.
                while (i < n) {
                    if (cs[i]=='E' && i+1<n && cs[i+1]=='I') {
                        if ((i==0||isWS(cs[i-1])) && (i+2>=n||isWS(cs[i+2])))
                            { i += 2; break; }
                    }
                    ++i;
                }
                continue;
            }

            Tok t; t.type=Tok::Op; t.s=opStr; toks.push_back(t);
        }
    }
    return toks;
}

static std::string getContentStream(QPDFObjectHandle obj) {
    std::string cs;
    auto contents = obj.getKey("/Contents");
    auto append = [&](QPDFObjectHandle s) {
        if (!s.isStream()) return;
        try {
            auto data = s.getStreamData(qpdf_dl_specialized);
            cs.append(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
            cs += ' ';
        } catch (const std::exception &ex) {
            qWarning() << "[ImageScan] stream decode error:" << ex.what();
        }
    };
    if (contents.isArray()) {
        for (int k=0;k<contents.getArrayNItems();++k) append(contents.getArrayItem(k));
    } else {
        append(contents);
    }
    return cs;
}

// Add XObjects from res into the map, skipping keys that are already present
// (so the first call's entries — local/child resources — take priority).
static void collectXObjects(QPDFObjectHandle res,
                            std::map<std::string, QPDFObjectHandle> &xobjs)
{
    if (!res.isDictionary()) return;
    auto xobjDict = res.getKey("/XObject");
    if (!xobjDict.isDictionary()) return;
    for (auto &kv : xobjDict.ditems())
        xobjs.emplace(kv.first, kv.second);  // emplace: no-op if key already exists
}

// Compute image bbox from current CTM and append if large enough.
static void addImageBBox(const M6 &m, double pageH, QList<QRectF> &result) {
    double xs[4], ys[4];
    auto [x0,y0]=tx(m,0,0); xs[0]=x0; ys[0]=y0;
    auto [x1,y1]=tx(m,1,0); xs[1]=x1; ys[1]=y1;
    auto [x2,y2]=tx(m,1,1); xs[2]=x2; ys[2]=y2;
    auto [x3,y3]=tx(m,0,1); xs[3]=x3; ys[3]=y3;
    double xMin=xs[0],xMax=xs[0],yMin=ys[0],yMax=ys[0];
    for (int k=1;k<4;++k){
        xMin=std::min(xMin,xs[k]); xMax=std::max(xMax,xs[k]);
        yMin=std::min(yMin,ys[k]); yMax=std::max(yMax,ys[k]);
    }
    const QRectF r(xMin, pageH-yMax, xMax-xMin, yMax-yMin);
    if (r.width()>5 && r.height()>5) result.append(r);
}

// Forward declaration.
static void scanStream(const std::string &cs,
                       QPDFObjectHandle localRes, QPDFObjectHandle pageRes,
                       std::vector<M6> &stack, double pageH,
                       QList<QRectF> &result, int depth);

static void scanStream(const std::string &cs,
                       QPDFObjectHandle localRes, QPDFObjectHandle pageRes,
                       std::vector<M6> &stack, double pageH,
                       QList<QRectF> &result, int depth)
{
    if (depth > 8 || cs.empty()) return;

    // Build XObject map: local (child) resources first, page resources as fallback.
    // emplace() never overrides, so local entries win for duplicate keys.
    std::map<std::string, QPDFObjectHandle> xobjs;
    collectXObjects(localRes, xobjs);
    collectXObjects(pageRes,  xobjs);

    std::vector<Tok> operands;
    for (const Tok &tok : tokenise(cs)) {
        if (tok.type == Tok::InlineImg) {
            // Inline image at current CTM position.
            addImageBBox(stack.back(), pageH, result);
            operands.clear();
            continue;
        }
        if (tok.type != Tok::Op) { operands.push_back(tok); continue; }

        const std::string &op = tok.s;
        if (op == "q") {
            stack.push_back(stack.back());
        } else if (op == "Q") {
            if (stack.size() > 1) stack.pop_back();
        } else if (op == "cm" && operands.size() >= 6) {
            const size_t k = operands.size();
            M6 m = { operands[k-6].num, operands[k-5].num,
                     operands[k-4].num, operands[k-3].num,
                     operands[k-2].num, operands[k-1].num };
            stack.back() = compose(m, stack.back());
        } else if (op == "Do" && !operands.empty()) {
            std::string name;
            for (int k=static_cast<int>(operands.size())-1; k>=0; --k)
                if (operands[k].type==Tok::Name){ name=operands[k].s; break; }

            // ditems() keys include the leading '/'; our tokenizer does too after decodePdfName.
            // Defensive: also try without '/' in case qpdf version strips it.
            auto it = xobjs.find(name);
            if (it == xobjs.end() && !name.empty() && name[0]=='/')
                it = xobjs.find(name.substr(1));

            if (it != xobjs.end()) {
                auto &xobj = it->second;
                if (!xobj.isStream()) { operands.clear(); continue; }

                auto sub = xobj.getDict().getKey("/Subtype");
                if (!sub.isName()) { operands.clear(); continue; }

                if (sub.getName() == "/Image") {
                    addImageBBox(stack.back(), pageH, result);

                } else if (sub.getName() == "/Form") {
                    auto formDict = xobj.getDict();
                    M6 formMatrix = identity();
                    auto matKey = formDict.getKey("/Matrix");
                    if (matKey.isArray() && matKey.getArrayNItems() == 6) {
                        for (int k=0;k<6;++k)
                            formMatrix[k] = matKey.getArrayItem(k).getNumericValue();
                    }
                    M6 composedCtm = compose(formMatrix, stack.back());

                    // Form XObject uses its own isolated stack.
                    std::vector<M6> formStack = { composedCtm };

                    // Form's local resources, with page resources as fallback.
                    QPDFObjectHandle formRes = formDict.getKey("/Resources");

                    try {
                        auto data = xobj.getStreamData(qpdf_dl_specialized);
                        std::string formCS(reinterpret_cast<const char*>(data->getBuffer()),
                                           data->getSize());
                        scanStream(formCS, formRes, pageRes, formStack, pageH, result, depth+1);
                    } catch (const std::exception &ex) {
                        qWarning() << "[ImageScan] Form XObject decode error:" << ex.what();
                    }
                }
            }
        }
        operands.clear();
    }
}

} // namespace

static QList<QRectF> detectPdfImageRegions(const QString &pdfPath, int pageIndex,
                                            double pageHeightPts)
{
    QList<QRectF> result;
    try {
        QPDF pdf;
        const std::string pw = PdfPwStore::forQpdf(pdfPath);
        pdf.processFile(pdfPath.toLocal8Bit().constData(),
                        pw.empty() ? nullptr : pw.c_str());

        QPDFPageDocumentHelper pdh(pdf);
        auto pages = pdh.getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size()))
            return result;

        QPDFPageObjectHelper ph = pages[pageIndex];
        // getAttribute handles /Resources inherited from parent page-tree nodes.
        QPDFObjectHandle resources = ph.getAttribute("/Resources", false);
        QPDFObjectHandle pageObj   = ph.getObjectHandle();
        const std::string cs = getContentStream(pageObj);

        qDebug() << "[ImageScan] page" << pageIndex
                 << "cs-length:" << cs.size()
                 << "resources-valid:" << resources.isDictionary();

        std::vector<M6> stack = { identity() };
        scanStream(cs, resources, resources, stack, pageHeightPts, result, 0);

        qDebug() << "[ImageScan] found" << result.size() << "image(s) on page" << pageIndex;
        for (const auto &r : result)
            qDebug() << "[ImageScan]  rect" << r;
    } catch (const std::exception &ex) {
        qWarning() << "[ImageScan] qpdf error:" << ex.what();
    }
    return result;
}

#endif // HAVE_QPDF

void ImageAnnotationLayer::scanVisiblePage(int firstVisiblePage)
{
#ifdef HAVE_PDF_RENDERING
    if (m_canvas->pageLabelCount() == 0 || m_filePath.isEmpty() || !m_renderer) return;

    clearDetectedHighlights();

    const int scanPage = firstVisiblePage;

    // Page height in PDF points (needed for coordinate conversion).
    const QSizeF pageSizePts = [&]() -> QSizeF {
        const QSize px100 = m_renderer->pageDisplaySize(scanPage, 100);
        const qreal s100  = PdfRenderer::screenScale(100);
        return QSizeF(px100.width() / s100, px100.height() / s100);
    }();

    QList<QRectF> regions;

#ifdef HAVE_QPDF
    // Primary: parse the PDF content stream directly — exact positions.
    regions = detectPdfImageRegions(m_filePath, scanPage, pageSizePts.height());
    qDebug() << "[ImageScan] qpdf found" << regions.size() << "image(s) on page" << scanPage;
#endif

#if defined(HAVE_QPDF) && defined(HAVE_TESSERACT)
    // Fallback: use Tesseract layout analysis when qpdf found nothing.
    if (regions.isEmpty() && m_ocr && m_ocr->isReady()) {
#elif defined(HAVE_TESSERACT)
    if (m_ocr && m_ocr->isReady()) {
#else
    if (false) {
#endif
#ifdef HAVE_TESSERACT
        const qreal ocrScale = 150.0 / 72.0;
        const QImage pageImg = m_renderer->renderPage(scanPage, ocrScale);
        if (!pageImg.isNull())
            regions = m_ocr->detectImageRegions(pageImg, pageSizePts, ocrScale);
        qDebug() << "[ImageScan] Tesseract fallback found" << regions.size() << "image(s)";
#endif
    }

    // Filter out images that cover the whole page — those are scanned-page backgrounds
    // and clicking them would replace the entire visible page with a floating widget.
    const double pageArea = pageSizePts.width() * pageSizePts.height();
    if (pageArea > 0) {
        regions.removeIf([pageArea](const QRectF &r) {
            return (r.width() * r.height()) / pageArea > 0.90;
        });
    }

    if (regions.isEmpty()) return;

    const qreal scale = m_canvas->screenScale();
    const QLabel *lbl = m_canvas->pageLabel(scanPage);
    if (!lbl) return;

    for (const QRectF &r : regions) {
        auto *frame = new QFrame(m_canvas->canvasWidget());
        frame->setObjectName(QStringLiteral("ImageRegionOverlay"));
        frame->setGeometry(
            lbl->pos().x() + qRound(r.left()   * scale),
            lbl->pos().y() + qRound(r.top()    * scale),
            qRound(r.width()  * scale),
            qRound(r.height() * scale));
        frame->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        frame->show();
        m_detectedFrames.append(frame);
    }
#else
    Q_UNUSED(firstVisiblePage)
#endif // HAVE_PDF_RENDERING
}
