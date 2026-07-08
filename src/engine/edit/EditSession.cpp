#include "EditSession.hpp"
#include "ContentMap.hpp"

#include <QPainter>
#include <QFont>
#include <QDebug>

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
    double textX=0, textY=0, leading=0, fontSize=12;
    double      capturedFs   = 0.0;
    std::string capturedFont;   // raw font-name token, e.g. "/F1"
    std::string currentFont;

    auto flush = [&](std::string &dst) {
        for (auto &o : ops) { dst += o.raw; dst += ' '; }
        ops.clear();
    };

    while (true) {
        Tok t = tok.next();
        if (t.type == TT::Eof) break;

        if (!inBT) {
            if (t.type == TT::Op && t.str == "BT") {
                inBT = true; textX=textY=leading=0;
                btBuf = "BT\n"; ops.clear();
            } else if (t.type == TT::Op) {
                if (t.str == "BI") {
                    flush(out); out += "BI "; ops.clear();
                    Tok u; do { u = tok.next(); out += u.raw; out += ' '; } while (u.type != TT::Eof && !(u.type == TT::Op && u.str == "EI"));
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
                if      (o=="Tm" && ops.size()>=6)             { textX=ops[ops.size()-2].num; textY=ops[ops.size()-1].num; }
                else if ((o=="Td"||o=="TD") && ops.size()>=2)  { textX+=ops[ops.size()-2].num; textY+=ops[ops.size()-1].num; if(o=="TD") leading=-ops[ops.size()-1].num; }
                else if (o=="T*")                               { textY -= leading; }
                else if (o=="TL" && !ops.empty())              { leading = ops.back().num; }
                else if (o=="Tf" && ops.size()>=2)             { fontSize = ops.back().num; currentFont = ops[ops.size()-2].raw; }

                const bool isShowOp = (o=="Tj"||o=="TJ"||o=="'"||o=="\"");
                if (isShowOp) {
                    const double qtY = pageH - textY;
                    const double exp = fontSize * 0.5;
                    if (target.adjusted(-12,-exp,12,exp).contains(QPointF(textX, qtY))) {
                        capturedFs   = fontSize;
                        capturedFont = currentFont;
                        ops.clear();
                        btBuf += "() Tj\n";
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
                              QPdfDocument * /*doc*/, int /*pageCount*/) const
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

            // First pass: replace in-bounds Tj ops with "() Tj" in the main stream.
            // Also captures the original font name and size for reuse in pass 2.
            // In-place pairs (blank+text at same pdfBounds) share one removal so
            // the text edit inherits the font captured from the blank's removal.
            std::vector<double>      capturedFs(edits.size(), 0.0);
            std::vector<std::string> capturedFont(edits.size());
            struct Stripped { QRectF rect; double fs; std::string font; QColor bg; };
            std::vector<Stripped> strippedCache;
            for (size_t i = 0; i < edits.size(); ++i) {
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
            // Erase pass: fill each edited area with the background colour so that
            // text rendered via form XObjects (which removeTextInBounds cannot reach)
            // is covered.  Uses the stored bgColor; falls back to white.
            for (const auto &st : strippedCache)
                cs += bgErase(st.rect, pageH, st.bg);

            // Second pass: append replacement text — blank edits are erase-only.
            // Font size priority: toolbar override > captured > height estimate.
            // Font resource: user changed the font → standard-14 mapping of the
            // chosen family; otherwise keep the original font resource so the
            // replacement matches the surrounding text exactly.
            for (size_t i = 0; i < edits.size(); ++i) {
                if (edits[i]->newText.isNull()) continue;  // blank edit: erase only
                const Edit &e = *edits[i];
                const double fs = e.fontSizePt > 0.0 ? e.fontSizePt : capturedFs[i];

                std::string fontRef;
                if (e.fontChanged || capturedFont[i].empty())
                    fontRef = ensureStdFont(input, ph,
                                            stdFontName(e.fontFamily, e.bold, e.italic));
                else
                    fontRef = capturedFont[i];

                qWarning() << "[SAVE] page" << pageIdx
                           << "text=" << e.newText.left(30)
                           << "bounds=" << e.pdfBounds
                           << "fs=" << fs << "font=" << QString::fromStdString(fontRef);
                cs += buildReplacement(e.pdfBounds, e.newText, pageH, fs,
                                       e.textColor, fontRef);
            }

            auto newStream = QPDFObjectHandle::newStream(&input, cs);
            pageObj.replaceKey("/Contents", input.makeIndirectObject(newStream));
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

bool EditSession::isBlankAt(int page, const QRectF &pdfBounds) const
{
    for (const auto &blank : m_edits) {
        // Use EXACT pdfBounds equality so that a blank created for a drag-move
        // source (blank at P1) does not block clicks on native text blocks that
        // merely overlap P1 — only the move-source area itself is blocked.
        if (blank.page != page || blank.pdfBounds != pdfBounds || !blank.newText.isNull())
            continue;
        // Found a blank at the exact location. In-place edits pair blank+text at
        // the same pdfBounds — if a companion text edit also covers this area the
        // spot has content and must open an editor, not be silently ignored.
        for (const auto &t : m_edits)
            if (t.page == page && !t.newText.isEmpty() && t.pdfBounds == pdfBounds)
                return false;
        return true;
    }
    return false;
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
        // Two-pass: all blanks before all text draws.
        for (const auto &e : m_edits)
            if (e.page == page && e.newText.isNull())
                paintBlankEdit(p, e, scale);
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

void EditSession::paintBlankEdit(QPainter &p, const Edit &e, qreal scale)
{
    const QRectF px(e.pdfBounds.topLeft() * scale, e.pdfBounds.size() * scale);
    const QRect  r = px.adjusted(-1, -1, 1, 1).toAlignedRect();
    p.fillRect(r, e.bgColor.isValid() ? e.bgColor : Qt::white);
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
    if (!sourcePath.isEmpty() && m_imageEdits.isEmpty())
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
