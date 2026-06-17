#include "EditSession.hpp"

#include <QPainter>
#include <QFont>
#include <QBuffer>

#ifdef HAVE_QT_PDF
#include <QPdfWriter>
#include <QPageSize>
#endif

// ── qpdf content-stream text replacement ─────────────────────────────────────
// Original text operators are REMOVED from the content stream and replaced with
// new Helvetica text operators.  No white rectangle, no overlay, no raster.
// Unedited pages are written exactly as-is (full vector preserved).
#ifdef HAVE_QPDF

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <cmath>
#include <map>
#include <sstream>
#include <vector>

namespace {

// ── Minimal PDF content-stream tokenizer ─────────────────────────────────────

enum class TT { Num, Name, LStr, HStr, ArrOpen, ArrClose, DictOpen, DictClose, Op, Eof };

struct Tok {
    TT          type { TT::Eof };
    std::string raw;
    std::string str;   // decoded: LStr/HStr contents, Op keyword
    double      num  { 0.0 };
};

class CSTok {
public:
    explicit CSTok(const std::string &src) : m_s(src) {}
    bool atEnd() const { return m_i >= m_s.size(); }

    Tok next() {
        skip();
        if (m_i >= m_s.size()) return mk(TT::Eof);
        char c = m_s[m_i];
        if (c == '(')  return litStr();
        if (c == '<')  return (peek(1) == '<') ? dictOpen() : hexStr();
        if (c == '>')  return (peek(1) == '>') ? dictClose() : word();
        if (c == '[')  return simple(TT::ArrOpen,  1, "[");
        if (c == ']')  return simple(TT::ArrClose, 1, "]");
        if (c == '/')  return name();
        if (c == '-' || c == '+' || c == '.' || isdigit((unsigned char)c)) return num();
        return word();
    }

    // Return raw bytes from current position up to (not including) end of source.
    std::string tail() const { return m_s.substr(m_i); }

private:
    const std::string &m_s;
    size_t m_i = 0;

    char peek(int off = 0) const { size_t j = m_i+off; return j < m_s.size() ? m_s[j] : '\0'; }

    static bool isWS(char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0'; }
    static bool isDelim(char c) {
        return isWS(c)||c=='/'||c=='<'||c=='>'||c=='['||c==']'||c=='('||c==')'||c=='{'||c=='}'||c=='%';
    }

    void skip() {
        while (m_i < m_s.size()) {
            if (isWS(m_s[m_i])) { ++m_i; }
            else if (m_s[m_i] == '%') { while (m_i < m_s.size() && m_s[m_i]!='\n' && m_s[m_i]!='\r') ++m_i; }
            else break;
        }
    }

    Tok mk(TT t) const { Tok tk; tk.type = t; return tk; }
    Tok simple(TT t, int len, const char *r) { Tok tk; tk.type=t; tk.raw=r; m_i+=len; return tk; }

    Tok litStr() {
        size_t s0 = m_i++;
        std::string dec;
        int depth = 1;
        while (m_i < m_s.size() && depth > 0) {
            char c = m_s[m_i];
            if (c=='\\' && m_i+1 < m_s.size()) {
                char e = m_s[++m_i];
                switch (e) {
                case 'n':  dec+='\n'; break; case 'r': dec+='\r'; break;
                case 't':  dec+='\t'; break; case 'b': dec+='\b'; break;
                case 'f':  dec+='\f'; break; case '(': dec+='(';  break;
                case ')':  dec+=')';  break; case '\\':dec+='\\'; break;
                case '\n': case '\r': break; // line continuation
                default:
                    if (e>='0'&&e<='7') {
                        unsigned v=e-'0'; ++m_i;
                        for (int k=0;k<2&&m_i<m_s.size()&&m_s[m_i]>='0'&&m_s[m_i]<='7';++k) v=v*8+(m_s[m_i++]-'0');
                        dec+=static_cast<char>(v&0xFF); --m_i;
                    } else dec+=e;
                }
                ++m_i;
            } else if (c=='(') { ++depth; dec+=c; ++m_i; }
            else if (c==')') { if (--depth>0) dec+=c; ++m_i; }
            else { dec+=c; ++m_i; }
        }
        Tok t; t.type=TT::LStr; t.raw=m_s.substr(s0,m_i-s0); t.str=dec; return t;
    }

    Tok hexStr() {
        size_t s0 = m_i++;
        std::string dec;
        while (m_i < m_s.size() && m_s[m_i] != '>') {
            char hi = m_s[m_i++]; if (isWS(hi)) continue;
            char lo = '0';
            while (m_i<m_s.size()&&isWS(m_s[m_i])) ++m_i;
            if (m_i<m_s.size()&&m_s[m_i]!='>') lo=m_s[m_i++];
            auto hv=[](char c)->int{ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:0; };
            dec+=static_cast<char>((hv(hi)<<4)|hv(lo));
        }
        if (m_i<m_s.size()) ++m_i;
        Tok t; t.type=TT::HStr; t.raw=m_s.substr(s0,m_i-s0); t.str=dec; return t;
    }

    Tok dictOpen()  { Tok t; t.type=TT::DictOpen;  t.raw="<<"; m_i+=2; return t; }
    Tok dictClose() { Tok t; t.type=TT::DictClose; t.raw=">>"; m_i+=2; return t; }

    Tok name() {
        size_t s0=m_i++;
        while (m_i<m_s.size()&&!isDelim(m_s[m_i])) ++m_i;
        Tok t; t.type=TT::Name; t.raw=m_s.substr(s0,m_i-s0); t.str=t.raw.substr(1); return t;
    }

    Tok num() {
        size_t s0=m_i;
        if (m_s[m_i]=='-'||m_s[m_i]=='+') ++m_i;
        while (m_i<m_s.size()&&isdigit((unsigned char)m_s[m_i])) ++m_i;
        if (m_i<m_s.size()&&m_s[m_i]=='.') { ++m_i; while(m_i<m_s.size()&&isdigit((unsigned char)m_s[m_i])) ++m_i; }
        Tok t; t.type=TT::Num; t.raw=m_s.substr(s0,m_i-s0);
        try { t.num=std::stod(t.raw); } catch (...) {}
        return t;
    }

    Tok word() {
        size_t s0=m_i;
        while (m_i<m_s.size()&&!isDelim(m_s[m_i])) ++m_i;
        if (m_i==s0) ++m_i;
        Tok t; t.type=TT::Op; t.raw=m_s.substr(s0,m_i-s0); t.str=t.raw; return t;
    }
};

// ── Content-stream filter ─────────────────────────────────────────────────────
// Removes every BT..ET block that contains a text-show op (Tj/TJ/' /") whose
// start position falls within targetBounds (Qt PDF-point coords, Y=0 at top).
// Returns the filtered stream and the captured font size (0 if not found).

static std::pair<std::string, double> removeTextInBounds(const std::string &cs,
                                                          const QRectF      &target,
                                                          double             pageH)
{
    CSTok tok(cs);
    std::string out; out.reserve(cs.size());

    bool        inBT = false, drop = false;
    std::string btBuf;
    std::vector<Tok> ops;
    double textX=0, textY=0, leading=0, fontSize=12;
    double capturedFs = 0.0;  // font size from the dropped block

    auto flush = [&](std::string &dst) {
        for (auto &o : ops) { dst += o.raw; dst += ' '; }
        ops.clear();
    };

    while (true) {
        Tok t = tok.next();
        if (t.type == TT::Eof) break;

        if (!inBT) {
            if (t.type == TT::Op && t.str == "BT") {
                inBT = true; drop = false; textX=textY=leading=0;
                btBuf = "BT\n"; ops.clear();
            } else if (t.type == TT::Op) {
                if (t.str == "BI") {
                    // Inline image: pass through verbatim until EI
                    flush(out); out += "BI "; ops.clear();
                    Tok u; do { u = tok.next(); out += u.raw; out += ' '; } while (u.type != TT::Eof && !(u.type == TT::Op && u.str == "EI"));
                    out += '\n';
                } else { flush(out); out += t.raw; out += '\n'; ops.clear(); }
            } else { ops.push_back(t); }
        } else {
            if (t.type == TT::Op && t.str == "ET") {
                inBT = false; ops.clear();
                if (!drop) { btBuf += "ET\n"; out += btBuf; }
                btBuf.clear();
            } else if (t.type == TT::Op) {
                const std::string &o = t.str;
                if      (o=="Tm" && ops.size()>=6)        { textX=ops[ops.size()-2].num; textY=ops[ops.size()-1].num; }
                else if ((o=="Td"||o=="TD") && ops.size()>=2) { textX+=ops[ops.size()-2].num; textY+=ops[ops.size()-1].num; if(o=="TD") leading=-ops[ops.size()-1].num; }
                else if (o=="T*")                         { textY -= leading; }
                else if (o=="Tf" && ops.size()>=2)        { fontSize = ops.back().num; }
                else if (o=="Tj"||o=="TJ"||o=="'"||o=="\"") {
                    double qtY = pageH - textY;
                    double exp = fontSize * 1.5;
                    if (target.adjusted(-4,-exp,4,exp).contains(QPointF(textX, qtY))) {
                        drop = true;
                        capturedFs = fontSize;  // capture exact font size from this block
                    }
                }
                if (!drop) { flush(btBuf); btBuf += t.raw; btBuf += '\n'; }
                ops.clear();
            } else { ops.push_back(t); }
        }
    }
    return { out, capturedFs };
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string pdfLit(const QString &s)
{
    std::string out;
    for (QChar qc : s) {
        const unsigned char c = qc.unicode()<256 ? static_cast<unsigned char>(qc.unicode()) : '?';
        if      (c=='(')  out += "\\(";
        else if (c==')')  out += "\\)";
        else if (c=='\\') out += "\\\\";
        else if (c>=0x20) out += static_cast<char>(c);
    }
    return out;
}

static std::string buildReplacement(const QRectF &bounds, const QString &text,
                                     double pageH, double fontSizeOverride = 0.0)
{
    const QStringList lines   = text.split(u'\n');
    const int         nLines  = lines.size();
    const double      fs      = fontSizeOverride > 0.0
                                    ? fontSizeOverride
                                    : std::max(6.0, bounds.height() / nLines * 0.78);
    const double      x       = bounds.left();
    const double      y0      = (pageH - bounds.top()) - fs * 0.80;
    const double      leading = fs * 1.20;

    std::ostringstream ss; ss << std::fixed; ss.precision(3);
    ss << "BT\n/OpenPDFHelv " << fs << " Tf\n0 0 0 rg\n"
       << x << ' ' << y0 << " Td\n(" << pdfLit(lines[0]) << ") Tj\n";
    for (int i = 1; i < nLines; ++i)
        ss << "0 " << -leading << " Td\n(" << pdfLit(lines[i]) << ") Tj\n";
    ss << "ET\n";
    return ss.str();
}

static std::string getPageContents(QPDFPageObjectHelper &ph)
{
    QPDFObjectHandle contents = ph.getObjectHandle().getKey("/Contents");
    if (contents.isNull()) return {};
    std::string result;
    // qpdf auto-dereferences indirect handles in type-check methods (isStream etc.)
    auto append = [&](QPDFObjectHandle s) {
        if (!s.isStream()) return;
        auto data = s.getStreamData(qpdf_dl_all);
        result.append(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
        result += '\n';
    };
    if (contents.isStream())
        append(contents);
    else if (contents.isArray())
        for (int i = 0; i < contents.getArrayNItems(); ++i) append(contents.getArrayItem(i));
    else // indirect reference to stream
        append(contents);
    return result;
}

static void ensureHelvetica(QPDF &qpdf, QPDFPageObjectHelper &ph)
{
    QPDFObjectHandle pageObj = ph.getObjectHandle();
    QPDFObjectHandle srcRes  = ph.getAttribute("/Resources", true);

    // Page-local copy so we don't mutate shared resource dicts.
    QPDFObjectHandle res = QPDFObjectHandle::newDictionary();
    if (srcRes.isDictionary())
        for (const auto &k : srcRes.getKeys()) res.replaceKey(k, srcRes.getKey(k));

    QPDFObjectHandle oldFont = res.getKey("/Font");
    QPDFObjectHandle font    = QPDFObjectHandle::newDictionary();
    if (oldFont.isDictionary())
        for (const auto &k : oldFont.getKeys()) font.replaceKey(k, oldFont.getKey(k));

    if (!font.hasKey("/OpenPDFHelv")) {
        auto hf = QPDFObjectHandle::newDictionary();
        hf.replaceKey("/Type",     QPDFObjectHandle::newName("/Font"));
        hf.replaceKey("/Subtype",  QPDFObjectHandle::newName("/Type1"));
        hf.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
        hf.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
        font.replaceKey("/OpenPDFHelv", qpdf.makeIndirectObject(hf));
    }
    res.replaceKey("/Font", font);
    pageObj.replaceKey("/Resources", res);
}

} // namespace

bool EditSession::saveVector(const QString &sourcePath, const QString &outputPath,
                              QPdfDocument * /*doc*/, int /*pageCount*/) const
{
    try {
        QPDF input;
        input.processFile(sourcePath.toLocal8Bit().constData());

        QPDFPageDocumentHelper pdh(input);
        auto pages  = pdh.getAllPages();
        const int n = static_cast<int>(pages.size());

        std::map<int, std::vector<const Edit*>> byPage;
        for (const auto &e : m_edits)
            if (e.page >= 0 && e.page < n) byPage[e.page].push_back(&e);

        for (auto &[pageIdx, edits] : byPage) {
            auto &ph        = pages[static_cast<std::size_t>(pageIdx)];
            QPDFObjectHandle pageObj = ph.getObjectHandle();

            double pageH = 841.89;
            QPDFObjectHandle mb = pageObj.getKey("/MediaBox");
            if (mb.isArray() && mb.getArrayNItems() == 4)
                pageH = std::abs(mb.getArrayItem(3).getNumericValue()
                               - mb.getArrayItem(1).getNumericValue());

            std::string cs = getPageContents(ph);

            // First pass: strip original text and capture font sizes
            std::vector<double> capturedFs(edits.size(), 0.0);
            for (size_t i = 0; i < edits.size(); ++i) {
                auto [filtered, fs] = removeTextInBounds(cs, edits[i]->pdfBounds, pageH);
                cs = std::move(filtered);
                capturedFs[i] = fs;
            }
            // Second pass: append replacements.
            // Priority: user-selected size (toolbar) > captured from content stream > height approx.
            for (size_t i = 0; i < edits.size(); ++i) {
                const double fs = edits[i]->fontSizePt > 0.0 ? edits[i]->fontSizePt : capturedFs[i];
                cs += buildReplacement(edits[i]->pdfBounds, edits[i]->newText, pageH, fs);
            }

            auto newStream = QPDFObjectHandle::newStream(&input, cs);
            pageObj.replaceKey("/Contents", input.makeIndirectObject(newStream));
            ensureHelvetica(input, ph);
        }

        QPDFWriter writer(input, outputPath.toLocal8Bit().constData());
        writer.write();
        return true;

    } catch (const std::exception &ex) {
        qWarning() << "[QPDF] saveVector failed:" << ex.what();
        return false;
    }
}

#endif // HAVE_QPDF

// ── Mutation ──────────────────────────────────────────────────────────────────

void EditSession::addEdit(int page, const QRectF &pdfBounds, const QString &newText,
                          double fontSizePt)
{
    removeEdit(page, pdfBounds);
    m_edits.append({ page, pdfBounds, newText, fontSizePt });
}

void EditSession::removeEdit(int page, const QRectF &pdfBounds)
{
    m_edits.removeIf([&](const Edit &e) {
        return e.page == page && e.pdfBounds == pdfBounds;
    });
}

void EditSession::clear()
{
    m_edits.clear();
}

// ── Queries ───────────────────────────────────────────────────────────────────

bool EditSession::hasEditsOnPage(int page) const
{
    for (const auto &e : m_edits)
        if (e.page == page) return true;
    return false;
}

QString EditSession::editTextAt(int page, const QRectF &pdfBounds) const
{
    for (const auto &e : m_edits)
        if (e.page == page && e.pdfBounds.intersects(pdfBounds))
            return e.newText;
    return QString(); // null = no existing edit
}

// ── Live-view raster rendering (QImage overlay) ───────────────────────────────

void EditSession::applyToImage(int page, QImage &img, qreal scale) const
{
    if (img.isNull() || !hasEditsOnPage(page)) return;
    QPainter p(&img);
    for (const auto &e : m_edits)
        if (e.page == page) paintEdit(p, e, scale);
}

void EditSession::paintEdit(QPainter &p, const Edit &e, qreal scale)
{
    const QRectF px(e.pdfBounds.topLeft() * scale, e.pdfBounds.size() * scale);
    p.fillRect(px.adjusted(-3, -3, 3, 3), Qt::white);

    int pixelSize;
    if (e.fontSizePt > 0.0) {
        pixelSize = qMax(6, qRound(e.fontSizePt * scale));
    } else {
        const int lineCount = qMax(1, e.newText.count(u'\n') + 1);
        pixelSize = qMax(8, int(px.height() / lineCount * 0.78));
    }
    QFont f = p.font();
    f.setPixelSize(pixelSize);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(px.toRect(),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               e.newText);
}

// ── Save ──────────────────────────────────────────────────────────────────────

#ifdef HAVE_QT_PDF

bool EditSession::saveToFile(const QString &outputPath,
                              QPdfDocument  *doc,
                              int            pageCount,
                              const QString &sourcePath) const
{
#ifdef HAVE_QPDF
    if (!sourcePath.isEmpty())
        return saveVector(sourcePath, outputPath, doc, pageCount);
#else
    Q_UNUSED(sourcePath)
#endif
    return saveRaster(outputPath, doc, pageCount);
}

bool EditSession::saveRaster(const QString &outputPath,
                               QPdfDocument  *doc,
                               int            pageCount) const
{
    if (!doc || pageCount <= 0) return false;

    QPdfWriter writer(outputPath);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(300);

    const QSizeF firstPts = doc->pagePointSize(0);
    writer.setPageSize(QPageSize(firstPts, QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter(&writer);
    if (!painter.isActive()) return false;

    constexpr qreal kSaveDpi = 300.0;
    constexpr qreal kPts2Px  = kSaveDpi / 72.0;

    for (int i = 0; i < pageCount; ++i) {
        if (i > 0 && !writer.newPage()) return false;

        const QSizeF pts = doc->pagePointSize(i);
        const QSize  px(int(pts.width() * kPts2Px), int(pts.height() * kPts2Px));

        QImage img = doc->render(i, px);
        if (img.isNull()) continue;

        applyToImage(i, img, kPts2Px);

        const QRect pageRect(0, 0, painter.device()->width(),
                             painter.device()->height());
        painter.drawImage(pageRect, img);
    }

    painter.end();
    return true;
}

#endif // HAVE_QT_PDF
