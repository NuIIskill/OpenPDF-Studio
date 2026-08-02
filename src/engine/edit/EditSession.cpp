#include "EditSession.hpp"
#include "ContentMap.hpp"
#include "app/SafeWrite.hpp"

#include <QPainter>
#include <QFont>
#include <QDebug>
#include <climits>

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
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>

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

public:
    // Raw bytes of an inline image (call right after consuming BI), up to and
    // including the EI operator. The binary payload must pass through
    // UNTOUCHED — re-tokenizing it inserts whitespace into the image data and
    // corrupts the stream from that point on.
    std::string rawInlineImage() {
        const size_t start = m_i;
        while (m_i + 1 < m_s.size()) {
            if (m_s[m_i] == 'E' && m_s[m_i+1] == 'I'
                    && (m_i == 0 || isWS(m_s[m_i-1]))
                    && (m_i + 2 >= m_s.size() || isWS(m_s[m_i+2])
                        || isDelim(m_s[m_i+2]))) {
                m_i += 2;
                return m_s.substr(start, m_i - start);
            }
            ++m_i;
        }
        m_i = m_s.size();
        return m_s.substr(start);
    }
};

// ── Content-stream filter ─────────────────────────────────────────────────────
// Replaces each text-show op (Tj/TJ/' /") whose start position falls within
// targetBounds (Qt PDF-point coords, Y=0 at top) with an empty "() Tj" so that
// subsequent relative Td moves in the same BT block stay correct.
// Returns: {filtered_stream, fontSize, fontName_raw (e.g. "/F1")}.
// A white fill rectangle is later appended to the stream to erase original
// text that may live in form XObjects not touched by this filter.

static std::tuple<std::string, double, std::string>
removeTextInBounds(const std::string &cs, const QRectF &target, double pageH)
{
    CSTok tok(cs);
    std::string out; out.reserve(cs.size());

    bool        inBT = false;
    std::string btBuf;
    std::vector<Tok> ops;
    double leading=0, fontSize=12;
    double      capturedFs   = 0.0;
    std::string capturedFont;   // raw font-name token, e.g. "/F1"
    std::string currentFont;

    // CTM tracking (row-vector convention, new matrix applies first).
    // Without it, PDFs that wrap their content in a `cm` transform (most
    // generator output!) report positions in the wrong space, NOTHING gets
    // removed, and the saved file resurrects the original text as a dupe.
    struct M { double a=1,b=0,c=0,d=1,e=0,f=0; };
    const auto concatM = [](const M &L, const M &R) -> M {
        return { L.a*R.a + L.b*R.c,  L.a*R.b + L.b*R.d,
                 L.c*R.a + L.d*R.c,  L.c*R.b + L.d*R.d,
                 L.e*R.a + L.f*R.c + R.e,
                 L.e*R.b + L.f*R.d + R.f };
    };
    M ctm;
    std::vector<M> ctmStack;

    // Text LINE matrix (Tlm). Td/TD/T* translations are expressed in text
    // space and transformed through Tlm — scalar x/y tracking silently breaks
    // on the mirrored `1 0 0 -1 0 0 Tm` that Qt & friends emit, removing
    // RANDOM glyphs instead of the targeted ones.
    M tlm;
    const auto tlmTranslate = [&](double tx, double ty) {
        tlm.e = tx * tlm.a + ty * tlm.c + tlm.e;
        tlm.f = tx * tlm.b + ty * tlm.d + tlm.f;
    };

    auto flush = [&](std::string &dst) {
        for (auto &o : ops) { dst += o.raw; dst += ' '; }
        ops.clear();
    };

    while (true) {
        Tok t = tok.next();
        if (t.type == TT::Eof) break;

        if (!inBT) {
            if (t.type == TT::Op && t.str == "BT") {
                inBT = true; leading=0; tlm = M{};
                btBuf = "BT\n"; ops.clear();
            } else if (t.type == TT::Op) {
                // Interpret the graphics state BEFORE flushing (ops still hold
                // the operands); the tokens themselves pass through verbatim.
                if (t.str == "q") {
                    ctmStack.push_back(ctm);
                } else if (t.str == "Q") {
                    if (!ctmStack.empty()) { ctm = ctmStack.back(); ctmStack.pop_back(); }
                } else if (t.str == "cm" && ops.size() >= 6) {
                    M m;
                    m.a = ops[ops.size()-6].num; m.b = ops[ops.size()-5].num;
                    m.c = ops[ops.size()-4].num; m.d = ops[ops.size()-3].num;
                    m.e = ops[ops.size()-2].num; m.f = ops[ops.size()-1].num;
                    ctm = concatM(m, ctm);
                }
                if (t.str == "BI") {
                    flush(out); ops.clear();
                    out += "BI";
                    out += tok.rawInlineImage();   // verbatim — never re-tokenize
                    out += '\n';
                } else { flush(out); out += t.raw; out += '\n'; ops.clear(); }
            } else { ops.push_back(t); }
        } else {
            if (t.type == TT::Op && t.str == "ET") {
                inBT = false; ops.clear();
                out += btBuf + "ET\n";
                btBuf.clear();
            } else if (t.type == TT::Op) {
                const std::string &o = t.str;
                if      (o=="Tm" && ops.size()>=6)             {
                    tlm.a = ops[ops.size()-6].num; tlm.b = ops[ops.size()-5].num;
                    tlm.c = ops[ops.size()-4].num; tlm.d = ops[ops.size()-3].num;
                    tlm.e = ops[ops.size()-2].num; tlm.f = ops[ops.size()-1].num;
                }
                else if ((o=="Td"||o=="TD") && ops.size()>=2)  {
                    const double tx = ops[ops.size()-2].num;
                    const double ty = ops[ops.size()-1].num;
                    tlmTranslate(tx, ty);
                    if (o=="TD") leading = -ty;
                }
                else if (o=="T*")                               { tlmTranslate(0, -leading); }
                else if (o=="TL" && !ops.empty())              { leading = ops.back().num; }
                else if (o=="Tf" && ops.size()>=2)             { fontSize = ops.back().num; currentFont = ops[ops.size()-2].raw; }

                const bool isShowOp = (o=="Tj"||o=="TJ"||o=="'"||o=="\"");
                if (isShowOp) {
                    // ' and " advance to the next line BEFORE showing.
                    if (o == "'" || o == "\"") tlmTranslate(0, -leading);
                    // Map the line-matrix origin through the CTM into page space.
                    const double px   = tlm.e*ctm.a + tlm.f*ctm.c + ctm.e;
                    const double py   = tlm.e*ctm.b + tlm.f*ctm.d + ctm.f;
                    const double qtY  = pageH - py;
                    const double tlmS = std::sqrt(tlm.b*tlm.b + tlm.d*tlm.d);
                    const double ctmS = std::sqrt(ctm.c*ctm.c + ctm.d*ctm.d);
                    const double effFs = fontSize
                                       * (tlmS > 0.001 ? tlmS : 1.0)
                                       * (ctmS > 0.001 ? ctmS : 1.0);
                    const double exp = effFs * 0.5;
                    if (target.adjusted(-12,-exp,12,exp).contains(QPointF(px, qtY))) {
                        capturedFs   = effFs;
                        capturedFont = currentFont;
                        // Preserve the positioning side effects of the removed
                        // operator: ' and " perform a T* line advance — dropping
                        // it collapses every following line of the block onto
                        // one position.
                        if (o == "'") {
                            btBuf += "T* () Tj\n";
                        } else if (o == "\"") {
                            double aw = 0.0, ac = 0.0; int seen = 0;
                            for (const Tok &tk : ops)
                                if (tk.type == TT::Num) {
                                    (seen == 0 ? aw : ac) = tk.num;
                                    if (++seen == 2) break;
                                }
                            std::ostringstream ssq;
                            ssq << aw << ' ' << ac << " () \"\n";
                            btBuf += ssq.str();
                        } else {
                            btBuf += "() Tj\n";
                        }
                        ops.clear();
                    } else {
                        flush(btBuf); btBuf += t.raw; btBuf += '\n'; ops.clear();
                    }
                } else {
                    flush(btBuf); btBuf += t.raw; btBuf += '\n'; ops.clear();
                }
            } else { ops.push_back(t); }
        }
    }
    return { out, capturedFs, capturedFont };
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Graphics state at the end of a content stream: how many q pushes remain
// unpopped, and which CTM will be in effect once they are popped. Streams
// commonly apply their base transform (`0.24 0 0 -0.24 0 792 cm`) OUTSIDE
// any q — that one can never be popped, so appended content must be wrapped
// in its INVERSE to land in default page space.
struct StreamEndState {
    int depth { 0 };
    double a{1}, b{0}, c{0}, d{1}, e{0}, f{0};   // effective base CTM
    bool isIdentity() const {
        return std::abs(a-1) < 1e-9 && std::abs(b) < 1e-9 && std::abs(c) < 1e-9
            && std::abs(d-1) < 1e-9 && std::abs(e) < 1e-9 && std::abs(f) < 1e-9;
    }
};

static StreamEndState streamEndState(const std::string &cs)
{
    struct M6 { double a=1,b=0,c=0,d=1,e=0,f=0; };
    M6 ctm;
    std::vector<M6> stack;
    std::vector<Tok> ops;
    CSTok tok(cs);
    while (true) {
        Tok t = tok.next();
        if (t.type == TT::Eof) break;
        if (t.type != TT::Op) { ops.push_back(t); continue; }
        if (t.str == "q") { stack.push_back(ctm); }
        else if (t.str == "Q") { if (!stack.empty()) { ctm = stack.back(); stack.pop_back(); } }
        else if (t.str == "cm" && ops.size() >= 6) {
            M6 m;
            m.a = ops[ops.size()-6].num; m.b = ops[ops.size()-5].num;
            m.c = ops[ops.size()-4].num; m.d = ops[ops.size()-3].num;
            m.e = ops[ops.size()-2].num; m.f = ops[ops.size()-1].num;
            // new matrix applies first (row-vector convention)
            M6 r;
            r.a = m.a*ctm.a + m.b*ctm.c;        r.b = m.a*ctm.b + m.b*ctm.d;
            r.c = m.c*ctm.a + m.d*ctm.c;        r.d = m.c*ctm.b + m.d*ctm.d;
            r.e = m.e*ctm.a + m.f*ctm.c + ctm.e; r.f = m.e*ctm.b + m.f*ctm.d + ctm.f;
            ctm = r;
        }
        else if (t.str == "BI") { tok.rawInlineImage(); }
        ops.clear();
    }
    // After the caller appends depth × "Q", the effective CTM is the one
    // saved at the FIRST unmatched q — or the current one if balanced.
    const M6 base = stack.empty() ? ctm : stack.front();
    StreamEndState st;
    st.depth = int(stack.size());
    st.a = base.a; st.b = base.b; st.c = base.c;
    st.d = base.d; st.e = base.e; st.f = base.f;
    return st;
}

// "q <inverse-of-base> cm" prefix that maps appended content back into
// default page space. Empty when the base CTM is already identity.
static std::string inverseCtmPrefix(const StreamEndState &st)
{
    if (st.isIdentity()) return {};
    const double det = st.a * st.d - st.b * st.c;
    if (std::abs(det) < 1e-12) return {};   // degenerate — nothing sane to do
    const double ia = st.d / det,  ib = -st.b / det;
    const double ic = -st.c / det, id = st.a / det;
    const double ie = -(st.e * ia + st.f * ic);
    const double if_ = -(st.e * ib + st.f * id);
    std::ostringstream ss; ss << std::fixed; ss.precision(8);
    ss << "q " << ia << ' ' << ib << ' ' << ic << ' ' << id << ' '
       << ie << ' ' << if_ << " cm\n";
    return ss.str();
}

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

// Append a background-filled rectangle at bounds to paint over any original text,
// including text that lives in form XObjects (unreachable by removeTextInBounds).
// bounds are in Qt PDF coords (Y=0 at top); pageH converts to PDF user coords.
// Falls back to white when bg is invalid.
static std::string bgErase(const QRectF &bounds, double pageH, const QColor &bg)
{
    const double r = bg.isValid() ? bg.redF()   : 1.0;
    const double g = bg.isValid() ? bg.greenF() : 1.0;
    const double b = bg.isValid() ? bg.blueF()  : 1.0;
    std::ostringstream ss; ss << std::fixed; ss.precision(3);
    const double x = bounds.left();
    const double y = pageH - bounds.bottom();
    const double w = bounds.width();
    const double h = bounds.height();
    ss << "q " << r << ' ' << g << ' ' << b << " rg "
       << x << ' ' << y << ' ' << w << ' ' << h << " re f Q\n";
    return ss.str();
}

// fontRef: font resource key, resolved by the caller (original resource or a
// standard-14 font registered via ensureStdFont).
static std::string buildReplacement(const QRectF &bounds, const QString &text,
                                     double pageH, double fontSizeOverride,
                                     const QColor &color,
                                     const std::string &fontRef)
{
    const QStringList lines   = text.split(u'\n');
    const int         nLines  = lines.size();
    const double      fs      = fontSizeOverride > 0.0
                                    ? fontSizeOverride
                                    : std::max(6.0, bounds.height() / nLines * 0.78);
    const double      x       = bounds.left();
    const double      y0      = (pageH - bounds.top()) - fs * 0.80;
    const double      leading = fs * 1.20;

    const double r = (color.isValid() ? color.redF()   : 0.0);
    const double g = (color.isValid() ? color.greenF() : 0.0);
    const double b = (color.isValid() ? color.blueF()  : 0.0);

    std::ostringstream ss; ss << std::fixed; ss.precision(3);
    ss << "BT\n" << fontRef << " " << fs << " Tf\n"
       << r << ' ' << g << ' ' << b << " rg\n"
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

// Map a Qt font family + style to one of the standard-14 PDF fonts. Every
// conforming reader renders these without embedding.
static std::string stdFontName(const QString &family, bool bold, bool italic)
{
    const QString f = family.toLower();
    if (f.contains(QLatin1String("courier")) || f.contains(QLatin1String("mono"))) {
        if (bold && italic) return "Courier-BoldOblique";
        if (bold)           return "Courier-Bold";
        if (italic)         return "Courier-Oblique";
        return "Courier";
    }
    if (f.contains(QLatin1String("times"))
            || f.contains(QLatin1String("georgia"))
            || f.contains(QLatin1String("garamond"))
            || f.contains(QLatin1String("book"))
            || (f.contains(QLatin1String("serif")) && !f.contains(QLatin1String("sans")))) {
        if (bold && italic) return "Times-BoldItalic";
        if (bold)           return "Times-Bold";
        if (italic)         return "Times-Italic";
        return "Times-Roman";
    }
    if (bold && italic) return "Helvetica-BoldOblique";
    if (bold)           return "Helvetica-Bold";
    if (italic)         return "Helvetica-Oblique";
    return "Helvetica";
}

// Registers a standard-14 font in the page's /Resources /Font dict (page-local
// copy so shared dicts are never mutated). Returns the resource key ("/Opdf…").
static std::string ensureStdFont(QPDF &qpdf, QPDFPageObjectHelper &ph,
                                 const std::string &baseFont)
{
    std::string key = "/Opdf";
    for (char c : baseFont)
        if (c != '-') key += c;

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

    if (!font.hasKey(key)) {
        auto hf = QPDFObjectHandle::newDictionary();
        hf.replaceKey("/Type",     QPDFObjectHandle::newName("/Font"));
        hf.replaceKey("/Subtype",  QPDFObjectHandle::newName("/Type1"));
        hf.replaceKey("/BaseFont", QPDFObjectHandle::newName("/" + baseFont));
        hf.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
        font.replaceKey(key, qpdf.makeIndirectObject(hf));
    }
    res.replaceKey("/Font", font);
    pageObj.replaceKey("/Resources", res);
    return key;
}

// Updates AcroForm text-field values (/V) and regenerates their appearance
// streams so the new value renders in every viewer. Matches by fully
// qualified name, name suffix, or partial name (/T of the widget).
static void applyFormFieldEdits(QPDF &input,
                                const std::vector<const EditSession::Edit*> &fieldEdits)
{
    try {
        QPDFAcroFormDocumentHelper afdh(input);
        auto fields = afdh.getFormFields();
        bool changed = false;
        for (const auto *e : fieldEdits) {
            for (auto &f : fields) {
                const QString fqn = QString::fromStdString(f.getFullyQualifiedName());
                const QString pn  = QString::fromStdString(f.getPartialName());
                if (fqn != e->formField && pn != e->formField
                        && !fqn.endsWith(QLatin1Char('.') + e->formField))
                    continue;
                f.setV(QPDFObjectHandle::newUnicodeString(
                           e->newText.toStdString()), true);
                changed = true;
                break;
            }
        }
        if (changed) {
            // Bake appearance streams; if generation fails, NeedAppearances
            // stays set (from setV) and viewers regenerate on open.
            try { afdh.generateAppearancesIfNeeded(); } catch (...) {}
        }
    } catch (const std::exception &ex) {
        qWarning() << "[QPDF] form-field update failed:" << ex.what();
    }
}

} // namespace

bool EditSession::saveVector(const QString &sourcePath, const QString &outputPath,
                              QPdfDocument * /*doc*/, int /*pageCount*/,
                              const QSet<int> &overlayPages) const
{
    try {
        QPDF input;
        input.processFile(sourcePath.toLocal8Bit().constData());

        QPDFPageDocumentHelper pdh(input);
        auto pages  = pdh.getAllPages();
        const int n = static_cast<int>(pages.size());

        // AcroForm field edits update the field value (/V) — the widget's
        // appearance stream renders the text, so the content stream is left
        // untouched for them (blank companions included).
        std::vector<const Edit*> fieldEdits;
        std::map<int, std::vector<const Edit*>> byPage;
        for (const auto &e : m_edits) {
            if (e.page < 0 || e.page >= n) continue;
            if (!e.formField.isEmpty()) {
                if (!e.newText.isNull()) fieldEdits.push_back(&e);
            } else {
                byPage[e.page].push_back(&e);
            }
        }

        if (!fieldEdits.empty())
            applyFormFieldEdits(input, fieldEdits);

        for (auto &[pageIdx, edits] : byPage) {
            auto &ph        = pages[static_cast<std::size_t>(pageIdx)];
            QPDFObjectHandle pageObj = ph.getObjectHandle();

            // Use getAttribute to resolve /MediaBox inherited from page-tree nodes.
            double pageH = 841.89;
            QPDFObjectHandle mb = ph.getAttribute("/MediaBox", false);
            if (mb.isArray() && mb.getArrayNItems() == 4)
                pageH = std::abs(mb.getArrayItem(3).getNumericValue()
                               - mb.getArrayItem(1).getNumericValue());

            std::string cs = getPageContents(ph);
            const bool overlay = overlayPages.contains(pageIdx);

            // First pass: replace in-bounds Tj ops with "() Tj" in the main stream.
            // Also captures the original font name and size for reuse in pass 2.
            // In-place pairs (blank+text at same pdfBounds) share one removal so
            // the text edit inherits the font captured from the blank's removal.
            std::vector<double>      capturedFs(edits.size(), 0.0);
            std::vector<std::string> capturedFont(edits.size());
            struct Stripped { QRectF rect; double fs; std::string font; QColor bg; };
            std::vector<Stripped> strippedCache;
            for (size_t i = 0; !overlay && i < edits.size(); ++i) {
                const QRectF &b = edits[i]->pdfBounds;
                bool found = false;
                for (const auto &st : strippedCache) {
                    if (st.rect == b) {
                        capturedFs[i] = st.fs; capturedFont[i] = st.font;
                        found = true; break;
                    }
                }
                if (!found) {
                    auto [filtered, fs, fn] = removeTextInBounds(cs, b, pageH);
                    cs = std::move(filtered);
                    capturedFs[i]   = fs;
                    capturedFont[i] = fn;
                    strippedCache.push_back({ b, fs, fn, edits[i]->bgColor });
                }
            }
            if (!overlay) {
                // Erase pass — ONLY for BLANK edits whose removal found
                // nothing (text living in a form XObject it cannot reach).
                // Text-edit bounds must NEVER get a cover rect: a text box
                // over an empty area would paint a colored background block
                // into the page. Cover only the glyph rects.
                for (const auto *e : edits) {
                    if (!e->newText.isNull()) continue;   // blanks only
                    bool removalFailed = false;
                    for (const auto &st : strippedCache)
                        if (st.rect == e->pdfBounds) {
                            removalFailed = (st.fs == 0.0 && st.font.empty());
                            break;
                        }
                    if (!removalFailed) continue;
                    const QList<QRectF> areas = e->eraseRects.isEmpty()
                                                    ? QList<QRectF>{ e->pdfBounds }
                                                    : e->eraseRects;
                    for (const QRectF &r : areas)
                        cs += bgErase(r.adjusted(-2, -2, 2, 2), pageH, e->bgColor);
                }
            } else {
                // OVERLAY fallback (verified-save): the original stream stays
                // byte-identical; the erased text is covered per glyph rect
                // with the sampled background color. Append-only — cannot
                // corrupt anything.
                for (const auto *e : edits) {
                    if (!e->newText.isNull()) continue;
                    const QList<QRectF> areas = e->eraseRects.isEmpty()
                                                    ? QList<QRectF>{ e->pdfBounds }
                                                    : e->eraseRects;
                    for (const QRectF &r : areas)
                        cs += bgErase(r.adjusted(-2, -2, 2, 2), pageH, e->bgColor);
                }
            }

            // Neutralize leftover graphics state: pop unmatched q pushes, then
            // wrap everything we append in the INVERSE of the stream's base
            // CTM (applied outside any q — e.g. Qt's `0.24 0 0 -0.24 0 792 cm`)
            // so replacements land in default page space, not scaled/mirrored.
            const StreamEndState endState = streamEndState(cs);
            for (int d = endState.depth; d > 0; --d)
                cs += "Q\n";
            const std::string invPrefix = inverseCtmPrefix(endState);
            cs += invPrefix;

            // Second pass: append replacement text — blank edits are erase-only.
            // Font size priority: toolbar override > captured > height estimate.
            // Font resource: ALWAYS a standard-14 font mapped from the detected
            // family. Reusing the original resource renders garbage for subset/
            // custom-encoded fonts (our text is WinAnsi bytes, their encoding
            // is arbitrary glyph indices).
            for (size_t i = 0; i < edits.size(); ++i) {
                if (edits[i]->newText.isNull()) continue;  // blank edit: erase only
                const Edit &e = *edits[i];
                const double fs = e.fontSizePt > 0.0 ? e.fontSizePt : capturedFs[i];

                const std::string fontRef = ensureStdFont(
                    input, ph, stdFontName(e.fontFamily, e.bold, e.italic));

                qWarning() << "[SAVE] page" << pageIdx
                           << "text=" << e.newText.left(30)
                           << "bounds=" << e.pdfBounds
                           << "fs=" << fs << "font=" << QString::fromStdString(fontRef);
                cs += buildReplacement(e.pdfBounds, e.newText, pageH, fs,
                                       e.textColor, fontRef);
            }
            if (!invPrefix.empty())
                cs += "Q\n";

            auto newStream = QPDFObjectHandle::newStream(&input, cs);
            pageObj.replaceKey("/Contents", input.makeIndirectObject(newStream));
        }

        // Never write onto outputPath directly — when it is also the source
        // (any second save of the same document), truncating it pulls the
        // objects qpdf still has to read out from under it and the result is
        // an empty document. See SafeWrite.
        const QString staging = SafeWrite::stagingPath(outputPath);
        if (staging.isEmpty()) return false;
        {
            QPDFWriter writer(input, staging.toLocal8Bit().constData());
            writer.write();
        }
        return SafeWrite::commit(staging, outputPath);

    } catch (const std::exception &ex) {
        qWarning() << "[QPDF] saveVector failed:" << ex.what();
        return false;
    }
}

static inline QRgb overWhite(QRgb c);   // defined with paintBackgroundPatch

#if defined(HAVE_QT_PDF)
QSet<int> EditSession::verifyVectorSave(const QString &outputPath,
                                        QPdfDocument *doc) const
{
    QSet<int> bad;

    // Edited pages + their exclusion zones (dilated edit areas).
    QHash<int, QList<QRectF>> zones;
    for (const auto &e : m_edits) {
        if (!e.formField.isEmpty()) continue;   // field edits don't touch streams
        QList<QRectF> &z = zones[e.page];
        z.append(e.pdfBounds.adjusted(-8, -8, 8, 8));
        for (const QRectF &r : e.eraseRects)
            z.append(r.adjusted(-8, -8, 8, 8));
    }
    if (zones.isEmpty() || !doc) return bad;

    QPdfDocument savedDoc;
    if (savedDoc.load(outputPath) != QPdfDocument::Error::None) {
        for (auto it = zones.cbegin(); it != zones.cend(); ++it)
            bad.insert(it.key());
        return bad;
    }

    for (auto it = zones.cbegin(); it != zones.cend(); ++it) {
        const int page = it.key();
        const QSizeF pts = doc->pagePointSize(page);
        const QSize  px(int(pts.width()), int(pts.height()));   // 72 dpi
        const QImage a = doc->render(page, px);
        const QImage b = savedDoc.render(page, px);
        if (a.isNull() || b.isNull()) { bad.insert(page); continue; }

        int diff = 0;
        for (int y = 0; y < a.height() && y < b.height(); ++y) {
            for (int x = 0; x < a.width() && x < b.width(); ++x) {
                const QRgb ca = overWhite(a.pixel(x, y));
                const QRgb cb = overWhite(b.pixel(x, y));
                if (qAbs(qRed(ca) - qRed(cb)) + qAbs(qGreen(ca) - qGreen(cb))
                        + qAbs(qBlue(ca) - qBlue(cb)) <= 60) continue;
                bool excluded = false;
                for (const QRectF &z : it.value())
                    if (z.contains(x, y)) { excluded = true; break; }
                if (!excluded && ++diff > 60) break;
            }
            if (diff > 60) break;
        }
        if (diff > 60) bad.insert(page);
    }
    return bad;
}
#endif // HAVE_QT_PDF

#endif // HAVE_QPDF

// ── Mutation ──────────────────────────────────────────────────────────────────

void EditSession::addEdit(int page, const QRectF &pdfBounds, const QString &newText,
                          double fontSizePt, const QColor &color, const QRectF &sourceRect,
                          const QColor &bgColor)
{
    m_edits.append({ page, pdfBounds, sourceRect, newText, fontSizePt, color, bgColor });
}

void EditSession::removeEdit(int page, const QRectF &pdfBounds)
{
    m_edits.removeIf([&](const Edit &e) {
        return e.page == page && e.pdfBounds == pdfBounds;
    });
}

void EditSession::removeAllAt(int page, const QRectF &pdfBounds)
{
    m_edits.removeIf([&](const Edit &e) {
        return e.page == page && e.pdfBounds.intersects(pdfBounds);
    });
}

void EditSession::suspendEditsAt(int page, const QRectF &pdfBounds)
{
    m_suspendedEdits.clear();

    // Suspend ONLY edits whose pdfBounds exactly matches the clicked area.
    //
    // Previous companion-based logic (suspend everything linked via sourceRect)
    // caused "text jumping back": when text was moved P1→P2 (overlapping), clicking
    // on text(P2) also suspended blank(P1) via sourceRect, removing it from m_edits
    // and letting P1's original PDF content bleed through.
    //
    // The blank(P1) must persist in m_edits for as long as P1 is meant to be erased.
    // It is only suspended when the user directly clicks on P1's area — which isBlankAt
    // already prevents for move-source areas that have no companion text.
    QList<Edit> remaining;
    for (const Edit &e : m_edits) {
        if (e.page == page && e.pdfBounds == pdfBounds)
            m_suspendedEdits.append(e);
        else
            remaining.append(e);
    }
    m_edits = remaining;
}

bool EditSession::isBlankAt(int page, const QPointF &pdfPt) const
{
    // Point containment instead of exact rect equality: after a move, a fresh
    // click at the source position re-detects the native text with bounds that
    // never exactly match the stored blank (region-model expansion, clamping).
    // Clicks on neighbouring text stay unaffected — their click point lies
    // outside the blank.
    for (const auto &blank : m_edits) {
        if (blank.page != page || !blank.newText.isNull()
                || !blank.pdfBounds.contains(pdfPt))
            continue;
        // In-place edits pair blank+text at the same pdfBounds — that spot has
        // content and must open an editor, not be silently swallowed.
        bool hasCompanion = false;
        for (const auto &t : m_edits)
            if (t.page == page && !t.newText.isEmpty()
                    && t.pdfBounds == blank.pdfBounds) {
                hasCompanion = true;
                break;
            }
        if (!hasCompanion) return true;
    }
    return false;
}

bool EditSession::isBlankCovering(int page, const QRectF &bounds) const
{
    if (bounds.isEmpty()) return false;
    const double blockArea = bounds.width() * bounds.height();
    for (const auto &blank : m_edits) {
        if (blank.page != page || !blank.newText.isNull()) continue;
        const QRectF inter = blank.pdfBounds.intersected(bounds);
        if (inter.isEmpty()) continue;
        bool hasCompanion = false;
        for (const auto &t : m_edits)
            if (t.page == page && !t.newText.isEmpty()
                    && t.pdfBounds == blank.pdfBounds) {
                hasCompanion = true;
                break;
            }
        if (hasCompanion) continue;
        // Base the ratio on the SMALLER of the two areas: a text lookup that
        // merged the erased content into a larger block (neighbouring cell,
        // fuzzy snap) still counts as "covering" when the blank itself is
        // mostly inside the block.
        const double blankArea = blank.pdfBounds.width() * blank.pdfBounds.height();
        const double base      = qMax(1.0, qMin(blockArea, blankArea));
        if (inter.width() * inter.height() >= base * 0.5)
            return true;
    }
    return false;
}

QList<QRectF> EditSession::blankRegions(int page) const
{
    // Companion-less blanks = intentionally emptied areas. Text lookup must
    // treat them as if the text were gone — otherwise a click near them can
    // resurrect the invisible original.
    QList<QRectF> out;
    for (const auto &blank : m_edits) {
        if (blank.page != page || !blank.newText.isNull()) continue;
        bool hasCompanion = false;
        for (const auto &t : m_edits)
            if (t.page == page && !t.newText.isEmpty()
                    && t.pdfBounds == blank.pdfBounds) {
                hasCompanion = true;
                break;
            }
        if (!hasCompanion) out.append(blank.pdfBounds);
    }
    return out;
}

void EditSession::clearSuspended()
{
    m_suspendedEdits.clear();
}

void EditSession::restoreSuspended()
{
    // Re-insert suspended edits at the front so their original relative order
    // (blank before text) is preserved in applyToImage.
    m_edits = m_suspendedEdits + m_edits;
    m_suspendedEdits.clear();
}

void EditSession::clear()
{
    m_edits.clear();
    m_imageEdits.clear();
}

// ── Image-edit CRUD ───────────────────────────────────────────────────────────

void EditSession::addImageEdit(int page, const QRectF &pdfBounds, const QImage &image)
{
    m_imageEdits.append({ page, pdfBounds, image });
}

void EditSession::removeImageEdit(int page, const QRectF &pdfBounds)
{
    m_imageEdits.removeIf([&](const ImageEdit &ie) {
        return ie.page == page && ie.pdfBounds == pdfBounds;
    });
}

bool EditSession::hasImageEditsOnPage(int page) const
{
    for (const auto &ie : m_imageEdits)
        if (ie.page == page) return true;
    return false;
}

void EditSession::clearImageEdits()
{
    m_imageEdits.clear();
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
    // Iterate in reverse so the most-recently-added (topmost) text edit wins.
    // Skip blank edits (null newText) — they don't carry display text and would
    // make in-place edits (blank+text at same pdfBounds) show native text instead
    // of the session text.
    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.intersects(pdfBounds))
            return e.newText;
    }
    return QString(); // null = no existing edit
}

QColor EditSession::editColorAt(int page, const QRectF &pdfBounds) const
{
    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.intersects(pdfBounds))
            return e.textColor;
    }
    return QColor(); // invalid = no stored color
}

bool EditSession::findEditAt(int page, const QPointF &pdfPt, Edit *out) const
{
    // Iterate in reverse so the topmost (most recently drawn) session edit wins
    // when multiple overlapping edits contain the click point.
    // Skip blank edits — they are erase-only and should not open an editor.
    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.contains(pdfPt)) {
            if (out) *out = e;
            return true;
        }
    }
    return false;
}

// ── Live-view raster rendering (QImage overlay) ───────────────────────────────

void EditSession::applyToImage(int page, QImage &img, qreal scale) const
{
    if (img.isNull()) return;
    const bool hasText   = hasEditsOnPage(page);
    const bool hasImages = hasImageEditsOnPage(page);
    if (!hasText && !hasImages) return;

    QPainter p(&img);
    if (hasText) {
        // Two-pass: all blanks before all text draws. Blanks reconstruct the
        // background from the freshly rendered page pixels around them.
        for (const auto &e : m_edits)
            if (e.page == page && e.newText.isNull())
                paintBlankEdit(p, img, e, scale);
        for (const auto &e : m_edits)
            if (e.page == page && !e.newText.isEmpty())
                paintTextEdit(p, e, scale);
    }
    if (hasImages) {
        for (const ImageEdit &ie : m_imageEdits) {
            if (ie.page != page || ie.image.isNull()) continue;
            const QRect dst(
                qRound(ie.pdfBounds.left()   * scale),
                qRound(ie.pdfBounds.top()    * scale),
                qRound(ie.pdfBounds.width()  * scale),
                qRound(ie.pdfBounds.height() * scale));
            p.drawImage(dst, ie.image);
        }
    }
}

// Qt PDF renders pages onto a TRANSPARENT background — the "paper" is
// rgba(0,0,0,0), which reads as pitch black if alpha is ignored. Every
// sampled pixel must be composited over white (the paper color) first.
static inline QRgb overWhite(QRgb c)
{
    const int a = qAlpha(c);
    if (a == 255) return c;
    return qRgb((qRed(c)   * a + 255 * (255 - a)) / 255,
                (qGreen(c) * a + 255 * (255 - a)) / 255,
                (qBlue(c)  * a + 255 * (255 - a)) / 255);
}

void EditSession::paintBackgroundPatch(QPainter &p, const QImage &img,
                                       const QRect &rectPx)
{
    paintBackgroundPatch(p, img, QList<QRect>{ rectPx });
}

void EditSession::paintBackgroundPatch(QPainter &p, const QImage &img,
                                       const QList<QRect> &rectsPx)
{
    if (rectsPx.isEmpty()) return;
    const int W = img.width();
    const int H = img.height();

    int left = INT_MAX, right = INT_MIN, bTop = INT_MAX, bBot = INT_MIN;
    for (const QRect &r : rectsPx) {
        left  = qMin(left,  r.left());
        right = qMax(right, r.right());
        bTop  = qMin(bTop,  r.top());
        bBot  = qMax(bBot,  r.bottom());
    }
    left  = qMax(left, 0);
    right = qMin(right, W - 1);

    const auto closeRgb = [](QRgb a, QRgb b) {
        return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b))
             + qAbs(qBlue(a) - qBlue(b)) < 36;
    };

    // BAND color: text often sits in a colored stripe exactly as tall as the
    // block (table header rows!). Above/below sampling then sees only the
    // page outside the stripe and paints the wrong color into it. Sample
    // LEFT and RIGHT of the whole block at its mid height — if both sides
    // agree, that is the true background behind the text.
    QRgb bandColor = 0;
    bool hasBand   = false;
    if (bTop <= bBot) {
        const int midY = qBound(0, (bTop + bBot) / 2, H - 1);
        QRgb l[2], r[2]; int nL = 0, nR = 0;
        for (const int dx : { 3, 8 }) {
            const int xl = left - dx, xr = right + dx;
            if (xl >= 0 && nL < 2) l[nL++] = overWhite(img.pixel(xl, midY));
            if (xr <  W && nR < 2) r[nR++] = overWhite(img.pixel(xr, midY));
        }
        if (nL == 2 && nR == 2 && closeRgb(l[0], l[1]) && closeRgb(r[0], r[1])
                && closeRgb(l[0], r[0])) {
            bandColor = l[0];
            hasBand   = true;
        }
    }

    for (int x = left; x <= right; ++x) {
        // Column span across the WHOLE block: topmost..bottommost covering
        // rect. Sampling between tightly stacked lines would read the
        // neighbouring line's glyphs and smear them into the fill, so the
        // samples sit strictly outside the block for this column.
        int top = INT_MAX, bot = INT_MIN;
        for (const QRect &r : rectsPx)
            if (x >= r.left() && x <= r.right()) {
                top = qMin(top, r.top());
                bot = qMax(bot, r.bottom());
            }
        if (top > bot) continue;
        top = qMax(top, 0);
        bot = qMin(bot, H - 1);
        if (top > bot) continue;

        QRgb above[2]; int nA = 0;
        QRgb below[2]; int nB = 0;
        for (const int dy : { 2, 6 }) {
            const int ya = top - dy;
            const int yb = bot + dy;
            if (ya >= 0 && nA < 2) above[nA++] = overWhite(img.pixel(x, ya));
            if (yb <  H && nB < 2) below[nB++] = overWhite(img.pixel(x, yb));
        }

        const int spanH = bot - top + 1;

        // Colored band behind the text → the band color wins; the vertical
        // neighbours are outside the stripe and would punch wrong-colored
        // holes into it.
        if (hasBand) {
            p.fillRect(x, top, 1, spanH, QColor::fromRgb(bandColor));
            continue;
        }

        QRgb cand[4]; int n = 0;
        for (int i = 0; i < nA; ++i) cand[n++] = above[i];
        for (int i = 0; i < nB; ++i) cand[n++] = below[i];
        if (n == 0) {
            p.fillRect(x, top, 1, spanH, Qt::white);
            continue;
        }

        const auto close = closeRgb;

        QRgb best = cand[0]; int bestScore = -1;
        for (int i = 0; i < n; ++i) {
            int score = 0;
            for (int j = 0; j < n; ++j)
                if (close(cand[i], cand[j])) ++score;
            if (score > bestScore) { bestScore = score; best = cand[i]; }
        }

        if (bestScore >= 2 || n == 1) {
            // Clear majority (or only one sample) — solid column fill.
            p.fillRect(x, top, 1, spanH, QColor::fromRgb(best));
        } else {
            // Above and below disagree — the span probably crosses a
            // horizontal background boundary. Blend vertically instead of
            // hard-guessing.
            const QRgb ct = nA > 0 ? above[0] : best;
            const QRgb cb = nB > 0 ? below[0] : best;
            QLinearGradient g(QPointF(x, top), QPointF(x, bot + 1));
            g.setColorAt(0.0, QColor::fromRgb(ct));
            g.setColorAt(1.0, QColor::fromRgb(cb));
            p.fillRect(QRect(x, top, 1, spanH), g);
        }
    }
}

void EditSession::paintBlankEdit(QPainter &p, const QImage &img, const Edit &e,
                                 qreal scale)
{
    // Erase per glyph line rect when available — a whole-bounds erase would
    // wipe graphics (chart bars, images) sharing the area with the text.
    // All rects are passed as ONE block so the reconstruction samples outside
    // the block instead of between its lines.
    const QList<QRectF> areas = e.eraseRects.isEmpty()
                                    ? QList<QRectF>{ e.pdfBounds }
                                    : e.eraseRects;
    // Pad in PDF points, scaled to pixels: glyph antialiasing bleeds ~1-2 pt
    // beyond the word boxes. A fixed 1 px pad left visible text traces at
    // higher render scales (300 dpi save!).
    const qreal pad = qMax(1.0, 2.5 * scale);
    QList<QRect> rects;
    rects.reserve(areas.size());
    for (const QRectF &a : areas) {
        const QRectF px(a.topLeft() * scale, a.size() * scale);
        rects.append(px.adjusted(-pad, -pad, pad, pad).toAlignedRect());
    }
    if (!img.isNull()) {
        paintBackgroundPatch(p, img, rects);
    } else {
        for (const QRect &r : rects)
            p.fillRect(r, e.bgColor.isValid() ? e.bgColor : Qt::white);
    }
}

void EditSession::paintTextEdit(QPainter &p, const Edit &e, qreal scale)
{
    const QRectF px(e.pdfBounds.topLeft() * scale, e.pdfBounds.size() * scale);
    // No white fill here — the companion blank edit handles erasure.
    // Text edits are drawn as overlays so that a text box placed near (or on top
    // of) existing content does not silently destroy it.

    int pixelSize;
    if (e.fontSizePt > 0.0) {
        pixelSize = qMax(6, qRound(e.fontSizePt * scale));
    } else {
        const int lineCount = qMax(1, e.newText.count(u'\n') + 1);
        pixelSize = qMax(8, qRound(px.height() / lineCount / 0.72));
    }
    // Use the edit's stored family/style so the live view matches what the
    // save path writes (original font kept, or user-chosen family).
    QFont f(e.fontFamily.isEmpty() ? QStringLiteral("Helvetica") : e.fontFamily);
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(pixelSize);
    f.setBold(e.bold);
    f.setItalic(e.italic);
    p.setFont(f);
    p.setPen(e.textColor.isValid() ? e.textColor : QColor(0x11, 0x11, 0x11));
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
    // Fall back to raster when images are present — vector PDF manipulation
    // cannot embed raster images into the page stream cleanly.
    if (!sourcePath.isEmpty() && m_imageEdits.isEmpty()) {
        if (!saveVector(sourcePath, outputPath, doc, pageCount))
            return saveRaster(outputPath, doc, pageCount);
        // VERIFIED SAVE: render every edited page of the result against the
        // original. Any page whose content changed OUTSIDE the edit zones was
        // corrupted by the stream rewrite (unknown generator constructs) and
        // is re-written in append-only overlay mode — that mode cannot
        // corrupt anything, the original stream stays byte-identical.
        const QSet<int> bad = verifyVectorSave(outputPath, doc);
        if (!bad.isEmpty()) {
            qWarning() << "[SAVE] rewrite verification failed on pages" << bad
                       << "— falling back to overlay mode for those pages";
            if (!saveVector(sourcePath, outputPath, doc, pageCount, bad))
                return saveRaster(outputPath, doc, pageCount);
        }
        return true;
    }
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

    // Staged for the same reason as the vector path: `doc` renders lazily from
    // the file, which may well be the file being written.
    const QString staging = SafeWrite::stagingPath(outputPath);
    if (staging.isEmpty()) return false;

    QPdfWriter writer(staging);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(300);

    const QSizeF firstPts = doc->pagePointSize(0);
    writer.setPageSize(QPageSize(firstPts, QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter(&writer);
    if (!painter.isActive()) { SafeWrite::discard(staging); return false; }

    constexpr qreal kSaveDpi = 300.0;
    constexpr qreal kPts2Px  = kSaveDpi / 72.0;

    for (int i = 0; i < pageCount; ++i) {
        if (i > 0 && !writer.newPage()) { SafeWrite::discard(staging); return false; }

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
    return SafeWrite::commit(staging, outputPath);
}

#endif // HAVE_QT_PDF
