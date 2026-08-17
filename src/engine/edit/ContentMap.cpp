#include "engine/edit/ContentMap.hpp"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════════
//  Backend-neutral part: font resolution, cluster classification, spatial lookup
// ═══════════════════════════════════════════════════════════════════════════════

// ── Font resolution ───────────────────────────────────────────────────────────

ResolvedFont resolvePdfFont(const QString &rawBaseFont)
{
    ResolvedFont out;
    if (rawBaseFont.isEmpty()) return out;

    QString n = rawBaseFont;
    // Strip subset prefix "ABCDEF+FontName"
    const int plus = n.indexOf(u'+');
    if (plus > 0 && plus <= 6) n = n.mid(plus + 1);

    const QString low = n.toLower();
    out.bold   = low.contains(QLatin1String("bold"))
              || low.contains(QLatin1String("black"))
              || low.contains(QLatin1String("heavy"))
              || low.contains(QLatin1String("semibold"))
              || low.contains(QLatin1String("demibold"));
    out.italic = low.contains(QLatin1String("italic"))
              || low.contains(QLatin1String("oblique"));

    // Family = part before the first '-' or ',' (style/subfamily separator)
    int cut = n.indexOf(u'-');
    const int cut2 = n.indexOf(u',');
    if (cut2 >= 0 && (cut < 0 || cut2 < cut)) cut = cut2;
    QString fam = (cut > 0) ? n.left(cut) : n;

    // Alias map for the standard-14 families and their common clones.
    const QString famLow = fam.toLower();
    if (famLow.contains(QLatin1String("helvetica")))     { out.family = QStringLiteral("Helvetica");       return out; }
    if (famLow.contains(QLatin1String("arial")))          { out.family = QStringLiteral("Arial");           return out; }
    if (famLow.contains(QLatin1String("times")))          { out.family = QStringLiteral("Times New Roman"); return out; }
    if (famLow.contains(QLatin1String("courier")))        { out.family = QStringLiteral("Courier New");     return out; }
    if (famLow.contains(QLatin1String("symbol")))         { out.family = QStringLiteral("Symbol");          return out; }
    if (famLow.contains(QLatin1String("zapf")))           { out.family = QStringLiteral("ZapfDingbats");    return out; }

    // Strip glued style suffixes ("VerdanaBold" → "Verdana")
    static const char *kStyleWords[] = {
        "BoldOblique", "BoldItalic", "Bold", "Italic", "Oblique",
        "Black", "Heavy", "Light", "Medium", "SemiBold", "Semibold",
        "DemiBold", "Condensed", "Regular"
    };
    bool again = true;
    while (again) {
        again = false;
        for (const char *w : kStyleWords) {
            const QLatin1String ws(w);
            if (fam.size() > int(ws.size())
                    && fam.endsWith(ws, Qt::CaseInsensitive)) {
                fam.chop(ws.size());
                again = true;
            }
        }
        while (fam.endsWith(u'-') || fam.endsWith(u',') || fam.endsWith(u' '))
            fam.chop(1);
    }

    // Camel-case split: "MinionPro" → "Minion Pro", "TrebuchetMS" → "Trebuchet MS"
    QString spaced;
    spaced.reserve(fam.size() + 4);
    for (int i = 0; i < fam.size(); ++i) {
        if (i > 0 && fam[i].isUpper() && fam[i - 1].isLower())
            spaced += u' ';
        spaced += fam[i];
    }
    out.family = spaced.trimmed();
    return out;
}

// ── Cluster classification ────────────────────────────────────────────────────

namespace {

struct Seg {
    QRectF  bounds;
    QString text;
    int     firstIdx { -1 };   // index into cluster list (style source)
    QPointF origin;            // pen origin of the leftmost run in the segment
};

struct RowSegs {
    double     y     { 0.0 };
    double     lineH { 0.0 };
    QList<Seg> segs;
    bool       table { false };
};

} // namespace

QList<ContentItem> classifyContentClusters(QList<ContentCluster> clusters,
                                           bool mergeVertical)
{
    // Drop empties and degenerate runs
    QList<ContentCluster> cl;
    cl.reserve(clusters.size());
    for (const ContentCluster &c : clusters)
        if (!c.text.trimmed().isEmpty() && c.bounds.height() > 0.5)
            cl.append(c);
    if (cl.isEmpty()) return {};

    std::sort(cl.begin(), cl.end(), [](const ContentCluster &a, const ContentCluster &b) {
        if (std::abs(a.bounds.center().y() - b.bounds.center().y()) > 3.0)
            return a.bounds.center().y() < b.bounds.center().y();
        return a.bounds.left() < b.bounds.left();
    });

    // Phase 1: group runs into visual lines by center-Y proximity
    struct Row { QList<int> idx; double refY { 0.0 }; double lineH { 0.0 }; };
    QList<Row> rows;
    for (int i = 0; i < cl.size(); ++i) {
        const double cy = cl[i].bounds.center().y();
        const double lh = std::max({ cl[i].fontSizePt,
                                     cl[i].bounds.height() * 0.8, 5.0 });
        bool placed = false;
        for (Row &row : rows) {
            if (std::abs(cy - row.refY) < std::max(row.lineH, lh) * 0.6) {
                row.idx.append(i);
                row.refY  = (row.refY * (row.idx.size() - 1) + cy) / row.idx.size();
                row.lineH = std::max(row.lineH, lh);
                placed = true;
                break;
            }
        }
        if (!placed)
            rows.append(Row{ { i }, cy, lh });
    }

    // Phase 2: split each line into segments at column-sized horizontal gaps
    QList<RowSegs> rsegs;
    rsegs.reserve(rows.size());
    for (Row &row : rows) {
        std::sort(row.idx.begin(), row.idx.end(), [&](int a, int b) {
            return cl[a].bounds.left() < cl[b].bounds.left();
        });
        RowSegs rs;
        rs.y     = row.refY;
        rs.lineH = row.lineH;

        Seg cur;
        for (int k = 0; k < row.idx.size(); ++k) {
            const ContentCluster &c = cl[row.idx[k]];
            if (cur.firstIdx < 0) {
                cur = Seg{ c.bounds, c.text, row.idx[k], c.origin };
                continue;
            }
            const double gap = c.bounds.left() - cur.bounds.right();
            if (gap >= std::max(row.lineH * 1.5, 10.0)) {
                rs.segs.append(cur);
                cur = Seg{ c.bounds, c.text, row.idx[k], c.origin };
            } else {
                if (gap > row.lineH * 0.18 && !cur.text.endsWith(u' ')
                        && !c.text.startsWith(u' '))
                    cur.text += u' ';
                cur.text  += c.text;
                cur.bounds = cur.bounds.united(c.bounds);
            }
        }
        if (cur.firstIdx >= 0) rs.segs.append(cur);
        rsegs.append(rs);
    }

    std::sort(rsegs.begin(), rsegs.end(),
              [](const RowSegs &a, const RowSegs &b) { return a.y < b.y; });

    // A bullet and its following text are one list line, not two table
    // columns. Without this normalization, several adjacent bullet rows meet
    // the table-alignment heuristic and become a bogus two-column table.
    for (RowSegs &row : rsegs) {
        if (row.segs.size() < 2) continue;
        const QString marker = row.segs.first().text.trimmed();
        if (marker != QStringLiteral("•") && marker != QStringLiteral("-")
                && marker != QStringLiteral("–") && marker != QStringLiteral("—")
                && marker != QStringLiteral("▪") && marker != QStringLiteral("◦"))
            continue;
        Seg merged = row.segs.first();
        // Font/style must come from the list text, not from the tiny OpenSymbol
        // bullet glyph (typically 3-4 pt bounding height).
        merged.firstIdx = row.segs[1].firstIdx;
        for (int n = 1; n < row.segs.size(); ++n) {
            merged.text += u' ' + row.segs[n].text;
            merged.bounds = merged.bounds.united(row.segs[n].bounds);
        }
        row.segs = { merged };
    }

    // Phase 3: table detection — a multi-segment row is a table row only when a
    // vertically adjacent row is also multi-segment with ≥2 aligned column lefts.
    // (An isolated two-part line — e.g. heading + page number — stays plain text.)
    for (int i = 0; i < rsegs.size(); ++i) {
        if (rsegs[i].segs.size() < 2) continue;
        for (int j = i + 1; j < rsegs.size(); ++j) {
            const double dy = rsegs[j].y - rsegs[i].y;
            if (dy > std::max(rsegs[i].lineH, rsegs[j].lineH) * 2.4) break;
            if (rsegs[j].segs.size() < 2) continue;
            const double tol = std::max(rsegs[i].lineH, rsegs[j].lineH) * 1.2;
            int aligned = 0;
            for (const Seg &si : rsegs[i].segs)
                for (const Seg &sj : rsegs[j].segs)
                    if (std::abs(si.bounds.left() - sj.bounds.left()) < tol) {
                        ++aligned;
                        break;
                    }
            if (aligned >= 2) {
                rsegs[i].table = true;
                rsegs[j].table = true;
            }
        }
    }

    // Phase 4: emit one item per segment
    QList<ContentItem> items;
    for (const RowSegs &rs : rsegs) {
        for (const Seg &seg : rs.segs) {
            const ContentCluster &src = cl[seg.firstIdx];
            ContentItem it;
            it.type        = (rs.table && rs.segs.size() >= 2)
                                 ? ContentItem::Type::TableCell
                                 : ContentItem::Type::Text;
            it.bounds        = seg.bounds;
            it.textOrigin    = seg.origin;
            it.text          = seg.text.trimmed();
            it.fontSizePt    = src.fontSizePt;
            it.fontSizeExact = src.fontSizeExact;
            it.rawFontName   = src.rawFontName;
            it.textColor     = src.textColor;
            const ResolvedFont rf = resolvePdfFont(src.rawFontName);
            it.fontFamily    = rf.family;
            it.bold          = rf.bold;
            it.italic        = rf.italic;
            if (it.isValid() && !it.text.isEmpty())
                items.append(std::move(it));
        }
    }

    if (!mergeVertical) {
        std::sort(items.begin(), items.end(), [](const ContentItem &a, const ContentItem &b) {
            if (std::abs(a.bounds.top() - b.bounds.top()) > 0.5)
                return a.bounds.top() < b.bounds.top();
            return a.bounds.left() < b.bounds.left();
        });
        return items;
    }

    // Phase 5: vertical merge — consecutive aligned Text lines become a
    // Paragraph; tightly stacked TableCell lines become one multi-line cell.
    // Search a small window of recent items so multi-column layouts merge
    // per column instead of blocking on interleaved sort order.
    std::sort(items.begin(), items.end(), [](const ContentItem &a, const ContentItem &b) {
        if (std::abs(a.bounds.top() - b.bounds.top()) > 0.5)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });

    QList<ContentItem> merged;
    // Top of the line most recently merged into each item — the reference for
    // the block's own line spacing, which the bounds alone no longer show once
    // several lines have been united.
    QList<double> lastLineTop;
    for (ContentItem &item : items) {
        ContentItem *host = nullptr;
        const int windowStart = std::max(0, int(merged.size()) - 12);
        for (int k = merged.size() - 1; k >= windowStart; --k) {
            ContentItem &p = merged[k];
            const bool textPair = (p.type == ContentItem::Type::Text
                                   || p.type == ContentItem::Type::Paragraph)
                                  && item.type == ContentItem::Type::Text;
            const bool cellPair = p.type == ContentItem::Type::TableCell
                                  && item.type == ContentItem::Type::TableCell;
            if (!textPair && !cellPair) continue;

            const double fs = std::max({ p.fontSizePt, item.fontSizePt, 6.0 });
            const double vGap = item.bounds.top() - p.bounds.bottom();
            if (vGap < -fs * 0.4 || vGap > fs * 1.0) continue;

            const bool leftAligned = std::abs(p.bounds.left() - item.bounds.left())
                                     < fs * 1.2;
            const bool centered = std::abs(p.bounds.center().x()
                                           - item.bounds.center().x()) < fs * 1.5;
            if (!leftAligned && !centered) continue;

            // Columns must overlap horizontally — guards diagonal merges.
            const double xOverlap = std::min(p.bounds.right(), item.bounds.right())
                                  - std::max(p.bounds.left(), item.bounds.left());
            if (xOverlap < 1.0) continue;

            if (p.fontSizePt > 0.0 && item.fontSizePt > 0.0) {
                const double ratio = std::min(p.fontSizePt, item.fontSizePt)
                                   / std::max(p.fontSizePt, item.fontSizePt);
                if (ratio < 0.75) continue;
            }
            if (!p.rawFontName.isEmpty() && !item.rawFontName.isEmpty()
                    && p.rawFontName != item.rawFontName)
                continue;

            host = &p;
            break;
        }

        if (host) {
            const int hi = int(host - merged.constData());
            const double step = item.bounds.top() - lastLineTop[hi];
            if (step > 0.5)
                host->lineSpacingPt = host->lineSpacingPt > 0.0
                                          ? (host->lineSpacingPt + step) / 2.0
                                          : step;
            lastLineTop[hi] = item.bounds.top();
            host->bounds = host->bounds.united(item.bounds);
            host->text  += u'\n' + item.text;
            if (host->type == ContentItem::Type::Text)
                host->type = ContentItem::Type::Paragraph;
        } else {
            lastLineTop.append(item.bounds.top());
            merged.append(std::move(item));
        }
    }

    return merged;
}

// ── Spatial lookup ────────────────────────────────────────────────────────────

ContentItem contentItemAt(const QList<ContentItem> &items, const QPointF &pdfPt,
                          unsigned typeMask, double maxDistance)
{
    const auto prio = [](ContentItem::Type t) -> int {
        switch (t) {
        case ContentItem::Type::FormField: return 3;
        case ContentItem::Type::TableCell: return 2;
        case ContentItem::Type::Paragraph:
        case ContentItem::Type::Text:      return 1;
        default:                           return 0;
        }
    };

    // Pass 1: exact containment (3 pt tolerance); priority, then smaller area
    const ContentItem *best = nullptr;
    int    bestPrio = -1;
    double bestArea = 1e18;
    for (const ContentItem &item : items) {
        if (!(typeMask & contentTypeBit(item.type))) continue;
        if (!item.bounds.adjusted(-3, -3, 3, 3).contains(pdfPt)) continue;
        const int    p    = prio(item.type);
        const double area = item.bounds.width() * item.bounds.height();
        if (p > bestPrio || (p == bestPrio && area < bestArea)) {
            best = &item; bestPrio = p; bestArea = area;
        }
    }
    if (best) return *best;

    // Pass 2: nearest edge within maxDistance, biased toward fields and cells
    double bestDist = maxDistance;
    for (const ContentItem &item : items) {
        if (!(typeMask & contentTypeBit(item.type))) continue;
        const double dx = std::max(0.0, std::max(item.bounds.left() - pdfPt.x(),
                                                 pdfPt.x() - item.bounds.right()));
        const double dy = std::max(0.0, std::max(item.bounds.top() - pdfPt.y(),
                                                 pdfPt.y() - item.bounds.bottom()));
        double dist = std::hypot(dx, dy);
        if (item.type == ContentItem::Type::FormField)      dist *= 0.5;
        else if (item.type == ContentItem::Type::TableCell) dist *= 0.8;
        if (dist < bestDist) { bestDist = dist; best = &item; }
    }
    return best ? *best : ContentItem{};
}

// ═══════════════════════════════════════════════════════════════════════════════
//  qpdf content-stream scanner
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef HAVE_QPDF

#include <map>

// ── Minimal PDF content-stream tokenizer (local copy) ─────────────────────────
// Intentionally duplicated from EditSession.cpp so the two files remain
// independently compilable and the tokenizer stays in anonymous namespace.

namespace {

enum class TT { Num, Name, LStr, HStr, ArrOpen, ArrClose, DictOpen, DictClose, Op, Eof };

struct Tok {
    TT          type { TT::Eof };
    std::string raw, str;
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
        if (c == '<')  return (peek(1)=='<') ? dictOpen() : hexStr();
        if (c == '>')  return (peek(1)=='>') ? dictClose() : word();
        if (c == '[')  return simple(TT::ArrOpen,  1, "[");
        if (c == ']')  return simple(TT::ArrClose, 1, "]");
        if (c == '/')  return name();
        if (c=='-'||c=='+'||c=='.'||isdigit((unsigned char)c)) return numTok();
        return word();
    }

private:
    const std::string &m_s;
    size_t m_i = 0;

    char peek(int off=0) const { size_t j=m_i+off; return j<m_s.size()?m_s[j]:'\0'; }
    static bool isWS(char c){ return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0'; }
    static bool isDelim(char c){
        return isWS(c)||c=='/'||c=='<'||c=='>'||c=='['||c==']'||c=='('||c==')'||c=='{'||c=='}'||c=='%';
    }
    void skip(){
        while(m_i<m_s.size()){
            if(isWS(m_s[m_i]))++m_i;
            else if(m_s[m_i]=='%'){while(m_i<m_s.size()&&m_s[m_i]!='\n'&&m_s[m_i]!='\r')++m_i;}
            else break;
        }
    }
    Tok mk(TT t) const { Tok k; k.type=t; return k; }
    Tok simple(TT t, int n, const char *r){ Tok k; k.type=t; k.raw=r; m_i+=n; return k; }

    Tok litStr(){
        size_t s0=m_i++; std::string dec; int depth=1;
        while(m_i<m_s.size()&&depth>0){
            char c=m_s[m_i];
            if(c=='\\'&&m_i+1<m_s.size()){
                char e=m_s[++m_i];
                switch(e){
                case 'n':dec+='\n';break;case 'r':dec+='\r';break;case 't':dec+='\t';break;
                case '(':dec+='(';break;case ')':dec+=')';break;case '\\':dec+='\\';break;
                case '\n':case'\r':break;
                default:
                    if(e>='0'&&e<='7'){unsigned v=e-'0';++m_i;
                        for(int k=0;k<2&&m_i<m_s.size()&&m_s[m_i]>='0'&&m_s[m_i]<='7';++k)v=v*8+(m_s[m_i++]-'0');
                        dec+=static_cast<char>(v&0xFF);--m_i;}
                    else dec+=e;
                }++m_i;
            }else if(c=='('){++depth;dec+=c;++m_i;}
            else if(c==')'){if(--depth>0)dec+=c;++m_i;}
            else{dec+=c;++m_i;}
        }
        Tok t;t.type=TT::LStr;t.raw=m_s.substr(s0,m_i-s0);t.str=dec;return t;
    }
    Tok hexStr(){
        size_t s0=m_i++; std::string dec;
        while(m_i<m_s.size()&&m_s[m_i]!='>'){
            char hi=m_s[m_i++];if(isWS(hi))continue;
            char lo='0';
            while(m_i<m_s.size()&&isWS(m_s[m_i]))++m_i;
            if(m_i<m_s.size()&&m_s[m_i]!='>')lo=m_s[m_i++];
            auto hv=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:0;};
            dec+=static_cast<char>((hv(hi)<<4)|hv(lo));
        }
        if(m_i<m_s.size())++m_i;
        Tok t;t.type=TT::HStr;t.raw=m_s.substr(s0,m_i-s0);t.str=dec;return t;
    }
    Tok dictOpen() { Tok t;t.type=TT::DictOpen; t.raw="<<";m_i+=2;return t; }
    Tok dictClose(){ Tok t;t.type=TT::DictClose;t.raw=">>";m_i+=2;return t; }
    Tok name(){
        size_t s0=m_i++;
        while(m_i<m_s.size()&&!isDelim(m_s[m_i]))++m_i;
        Tok t;t.type=TT::Name;t.raw=m_s.substr(s0,m_i-s0);t.str=t.raw.substr(1);return t;
    }
    Tok numTok(){
        size_t s0=m_i;
        if(m_s[m_i]=='-'||m_s[m_i]=='+')++m_i;
        while(m_i<m_s.size()&&isdigit((unsigned char)m_s[m_i]))++m_i;
        if(m_i<m_s.size()&&m_s[m_i]=='.'){++m_i;while(m_i<m_s.size()&&isdigit((unsigned char)m_s[m_i]))++m_i;}
        Tok t;t.type=TT::Num;t.raw=m_s.substr(s0,m_i-s0);
        try{t.num=std::stod(t.raw);}catch(...){}return t;
    }
    Tok word(){
        size_t s0=m_i;
        while(m_i<m_s.size()&&!isDelim(m_s[m_i]))++m_i;
        if(m_i==s0)++m_i;
        Tok t;t.type=TT::Op;t.raw=m_s.substr(s0,m_i-s0);t.str=t.raw;return t;
    }
};

// ── Text decoding ─────────────────────────────────────────────────────────────

QString decodeStr(const std::string &s)
{
    if (s.size() >= 2) {
        const auto b0 = (unsigned char)s[0], b1 = (unsigned char)s[1];
        if (b0 == 0xFE && b1 == 0xFF) {            // UTF-16 BE BOM
            QString r;
            for (size_t i = 2; i + 1 < s.size(); i += 2)
                r += QChar((unsigned char)s[i] * 256 + (unsigned char)s[i+1]);
            return r;
        }
        if (b0 == 0xFF && b1 == 0xFE) {            // UTF-16 LE BOM
            QString r;
            for (size_t i = 2; i + 1 < s.size(); i += 2)
                r += QChar((unsigned char)s[i+1] * 256 + (unsigned char)s[i]);
            return r;
        }
    }
    return QString::fromLatin1(s.data(), (int)s.size());
}

// Extract displayable text from the operand stack at a text-show operator.
// Tj/'/": last LStr/HStr token.
// TJ: all LStr/HStr tokens; treat kerning adjustment < -200 units as a space.
QString extractText(const std::vector<Tok> &ops, const std::string &op)
{
    if (op == "TJ") {
        QString r;
        for (const Tok &t : ops) {
            if (t.type == TT::LStr || t.type == TT::HStr)
                r += decodeStr(t.str);
            else if (t.type == TT::Num && t.num < -200)
                r += ' ';
        }
        return r;
    }
    // Tj, ', "
    for (auto it = ops.rbegin(); it != ops.rend(); ++it)
        if (it->type == TT::LStr || it->type == TT::HStr)
            return decodeStr(it->str);
    return {};
}

// ── CTM helper (row-vector convention: p' = p · M) ────────────────────────────

struct M { double a=1,b=0,c=0,d=1,e=0,f=0; };

// concatM(L, R): apply L first, then R.
M concatM(const M &L, const M &R)
{
    return { L.a*R.a+L.b*R.c, L.a*R.b+L.b*R.d,
             L.c*R.a+L.d*R.c, L.c*R.b+L.d*R.d,
             L.e*R.a+L.f*R.c+R.e, L.e*R.b+L.f*R.d+R.f };
}

// ── Scanner state ─────────────────────────────────────────────────────────────

struct GState {
    M      ctm;
    QColor fill { Qt::black };
};

struct FillRec {
    QRectF rect;    // page space, Y=0 at top
    QColor color;
};

struct ScanOut {
    double                pageH { 0.0 };
    QList<ContentCluster> clusters;
    std::vector<FillRec>  fills;      // paint order
    QList<ContentItem>    images;
};

QString baseFontFor(const std::string &resName, QPDFObjectHandle resources)
{
    try {
        if (resName.empty() || !resources.isDictionary()) return {};
        auto fd = resources.getKey("/Font");
        if (!fd.isDictionary()) return {};
        auto f = fd.getKey("/" + resName);
        if (!f.isDictionary()) return {};
        auto bf = f.getKey("/BaseFont");
        if (!bf.isName()) return {};
        std::string nm = bf.getName();
        if (!nm.empty() && nm[0] == '/') nm = nm.substr(1);
        return QString::fromStdString(nm);
    } catch (...) { return {}; }
}

// Scans one content stream. Recurses one level into Form XObjects so text in
// letterheads/templates is detected too. All output coords: page space, Y=top.
void scanStream(const std::string &cs, ScanOut &out, const M &baseCtm,
                QPDFObjectHandle resources, int depth)
{
    CSTok tok(cs);
    GState gs;
    gs.ctm = baseCtm;
    std::vector<GState> gsStack;

    bool inBT = false;
    std::vector<Tok> ops;
    // Full text LINE matrix (Tlm): Td/TD/T* translations are text-space and
    // must be transformed through it — scalar x/y tracking breaks on the
    // mirrored `1 0 0 -1 0 0 Tm` that Qt & friends emit. penX tracks the
    // show-op advance along the line (text space); resets on line moves.
    M tlm;
    double penX = 0;
    double leading = 0, fontSize = 12;
    int    renderMode = 0;
    const auto tlmTranslate = [&](double tx, double ty) {
        tlm.e = tx * tlm.a + ty * tlm.c + tlm.e;
        tlm.f = tx * tlm.b + ty * tlm.d + tlm.f;
    };
    QString curFont;
    std::map<std::string, QString> fontCache;

    // Path tracking for background-fill detection (PDF coords, untransformed)
    std::vector<QRectF> pathRects;
    double pMinX=0, pMinY=0, pMaxX=0, pMaxY=0;
    bool   inPath = false;
    auto resetPath = [&]() { pathRects.clear(); inPath = false; };
    auto addPathPt = [&](double x, double y) {
        if (!inPath) { pMinX = pMaxX = x; pMinY = pMaxY = y; inPath = true; }
        else {
            pMinX = std::min(pMinX, x); pMaxX = std::max(pMaxX, x);
            pMinY = std::min(pMinY, y); pMaxY = std::max(pMaxY, y);
        }
    };

    auto xf = [&](double x, double y) -> QPointF {
        return { x * gs.ctm.a + y * gs.ctm.c + gs.ctm.e,
                 x * gs.ctm.b + y * gs.ctm.d + gs.ctm.f };
    };
    // PDF-coords rect → page-space Qt rect (Y=0 at top), CTM applied.
    auto rectToQt = [&](const QRectF &r) -> QRectF {
        const QPointF p1 = xf(r.left(),  r.top());
        const QPointF p2 = xf(r.right(), r.top());
        const QPointF p3 = xf(r.left(),  r.bottom());
        const QPointF p4 = xf(r.right(), r.bottom());
        const double minX = std::min({ p1.x(), p2.x(), p3.x(), p4.x() });
        const double maxX = std::max({ p1.x(), p2.x(), p3.x(), p4.x() });
        const double minY = std::min({ p1.y(), p2.y(), p3.y(), p4.y() });
        const double maxY = std::max({ p1.y(), p2.y(), p3.y(), p4.y() });
        return QRectF(minX, out.pageH - maxY, maxX - minX, maxY - minY);
    };

    // Non-stroking color operators — valid both inside and outside BT.
    auto handleColorOp = [&](const std::string &o) -> bool {
        const auto num = [&](int fromEnd) {
            return std::clamp(ops[ops.size() - std::size_t(fromEnd)].num, 0.0, 1.0);
        };
        if (o == "g" && ops.size() >= 1) {
            const double v = num(1);
            gs.fill = QColor::fromRgbF(v, v, v);
            return true;
        }
        if (o == "rg" && ops.size() >= 3) {
            gs.fill = QColor::fromRgbF(num(3), num(2), num(1));
            return true;
        }
        if (o == "k" && ops.size() >= 4) {
            gs.fill = QColor::fromCmykF(num(4), num(3), num(2), num(1)).toRgb();
            return true;
        }
        if (o == "sc" || o == "scn") {
            // Only plain numeric operands — pattern names (`/P1 scn`, or mixed
            // `… /P1 scn`) would misalign the from-end indexing.
            int nNum = 0;
            for (const Tok &t : ops) if (t.type == TT::Num) ++nNum;
            if (nNum != int(ops.size())) return true;
            if      (nNum == 1) { const double v = num(1); gs.fill = QColor::fromRgbF(v, v, v); }
            else if (nNum == 3) gs.fill = QColor::fromRgbF(num(3), num(2), num(1));
            else if (nNum == 4) gs.fill = QColor::fromCmykF(num(4), num(3), num(2), num(1)).toRgb();
            return true;
        }
        return false;
    };

    // /FontName size Tf — resolve the BaseFont via /Resources (cached).
    // Valid both inside and outside BT.
    auto applyTf = [&]() {
        fontSize = ops.back().num;
        for (auto it = ops.rbegin(); it != ops.rend(); ++it)
            if (it->type == TT::Name) {
                auto cached = fontCache.find(it->str);
                if (cached == fontCache.end())
                    cached = fontCache.emplace(it->str,
                                 baseFontFor(it->str, resources)).first;
                curFont = cached->second;
                break;
            }
    };

    while (true) {
        Tok t = tok.next();
        if (t.type == TT::Eof) break;

        if (t.type != TT::Op) { ops.push_back(t); continue; }
        const std::string &o = t.str;

        if (!inBT) {
            if (o == "BT") {
                inBT = true;
                tlm = M{};
                penX = leading = 0;
            }
            else if (o == "q") { gsStack.push_back(gs); }
            else if (o == "Q") { if (!gsStack.empty()) { gs = gsStack.back(); gsStack.pop_back(); } }
            else if (o == "cm" && ops.size() >= 6) {
                M m;
                m.a = ops[ops.size()-6].num; m.b = ops[ops.size()-5].num;
                m.c = ops[ops.size()-4].num; m.d = ops[ops.size()-3].num;
                m.e = ops[ops.size()-2].num; m.f = ops[ops.size()-1].num;
                gs.ctm = concatM(m, gs.ctm);   // new matrix applies first
            }
            else if (handleColorOp(o)) { /* fill color updated */ }
            else if (o == "re" && ops.size() >= 4) {
                const double x = ops[ops.size()-4].num, y = ops[ops.size()-3].num;
                const double w = ops[ops.size()-2].num, h = ops[ops.size()-1].num;
                pathRects.emplace_back(x, y, w, h);
                addPathPt(x, y); addPathPt(x + w, y + h);
            }
            else if (o == "m" && ops.size() >= 2) {
                addPathPt(ops[ops.size()-2].num, ops[ops.size()-1].num);
            }
            else if ((o == "l" || o == "c" || o == "v" || o == "y") && ops.size() >= 2) {
                addPathPt(ops[ops.size()-2].num, ops[ops.size()-1].num);
            }
            else if (o=="f"||o=="F"||o=="f*"||o=="b"||o=="b*"||o=="B"||o=="B*") {
                if (!pathRects.empty()) {
                    for (const QRectF &r : pathRects) {
                        const QRectF q = rectToQt(r.normalized());
                        if (q.width() >= 4.0 && q.height() >= 4.0)
                            out.fills.push_back({ q, gs.fill });
                    }
                } else if (inPath && pMaxX > pMinX && pMaxY > pMinY) {
                    const QRectF q = rectToQt(QRectF(QPointF(pMinX, pMinY),
                                                     QPointF(pMaxX, pMaxY)));
                    if (q.width() >= 4.0 && q.height() >= 4.0)
                        out.fills.push_back({ q, gs.fill });
                }
                resetPath();
            }
            else if (o == "n" || o == "S" || o == "s") { resetPath(); }
            else if (o == "Tf" && ops.size() >= 2)  { applyTf(); }
            else if (o == "Tr" && !ops.empty()) { renderMode = int(ops.back().num); }
            else if (o == "BI") {
                // Inline image: skip binary data up to EI so the tokenizer
                // doesn't misinterpret it as operators/strings.
                Tok u;
                do { u = tok.next(); }
                while (u.type != TT::Eof && !(u.type == TT::Op && u.str == "EI"));
            }
            else if (o == "Do" && !ops.empty()) {
                std::string xname;
                for (auto it = ops.rbegin(); it != ops.rend(); ++it)
                    if (it->type == TT::Name) { xname = it->str; break; }
                try {
                    if (!xname.empty() && resources.isDictionary()) {
                        auto xd = resources.getKey("/XObject");
                        if (xd.isDictionary()) {
                            auto xo = xd.getKey("/" + xname);
                            if (xo.isStream()) {
                                auto dict = xo.getDict();
                                auto sub  = dict.getKey("/Subtype");
                                if (sub.isName() && sub.getName() == "/Image") {
                                    const QRectF q = rectToQt(QRectF(0, 0, 1, 1));
                                    if (q.width() >= 2.0 && q.height() >= 2.0) {
                                        ContentItem img;
                                        img.type   = ContentItem::Type::Image;
                                        img.bounds = q;
                                        out.images.append(std::move(img));
                                    }
                                } else if (sub.isName() && sub.getName() == "/Form"
                                           && depth < 2) {
                                    M fm;
                                    auto mx = dict.getKey("/Matrix");
                                    if (mx.isArray() && mx.getArrayNItems() == 6) {
                                        fm.a = mx.getArrayItem(0).getNumericValue();
                                        fm.b = mx.getArrayItem(1).getNumericValue();
                                        fm.c = mx.getArrayItem(2).getNumericValue();
                                        fm.d = mx.getArrayItem(3).getNumericValue();
                                        fm.e = mx.getArrayItem(4).getNumericValue();
                                        fm.f = mx.getArrayItem(5).getNumericValue();
                                    }
                                    auto innerRes = dict.getKey("/Resources");
                                    if (!innerRes.isDictionary()) innerRes = resources;
                                    auto data = xo.getStreamData(qpdf_dl_all);
                                    std::string inner(
                                        reinterpret_cast<const char *>(data->getBuffer()),
                                        data->getSize());
                                    scanStream(inner, out, concatM(fm, gs.ctm),
                                               innerRes, depth + 1);
                                }
                            }
                        }
                    }
                } catch (...) { /* unsupported stream — skip */ }
            }
            ops.clear();
            continue;
        }

        // ── Inside BT ──────────────────────────────────────────────────────────
        if (o == "ET") { inBT = false; ops.clear(); continue; }

        if (o == "Tm" && ops.size() >= 6) {
            tlm.a = ops[ops.size()-6].num; tlm.b = ops[ops.size()-5].num;
            tlm.c = ops[ops.size()-4].num; tlm.d = ops[ops.size()-3].num;
            tlm.e = ops[ops.size()-2].num; tlm.f = ops[ops.size()-1].num;
            penX  = 0;
        }
        else if ((o == "Td" || o == "TD") && ops.size() >= 2) {
            const double tx = ops[ops.size()-2].num;
            const double ty = ops[ops.size()-1].num;
            tlmTranslate(tx, ty);
            penX = 0;
            if (o == "TD") leading = -ty;
        }
        else if (o == "T*")                 { tlmTranslate(0, -leading); penX = 0; }
        else if (o == "TL" && !ops.empty()) { leading = ops.back().num; }
        else if (o == "Tf" && ops.size() >= 2)  { applyTf(); }
        else if (o == "Tr" && !ops.empty()) { renderMode = int(ops.back().num); }
        else if (handleColorOp(o)) { /* fill color updated */ }
        else if (o == "Tj" || o == "TJ" || o == "'" || o == "\"") {
            if (o == "'" || o == "\"") { tlmTranslate(0, -leading); penX = 0; }

            const QString text = extractText(ops, o);
            // Render mode 3 = invisible (OCR text layer), 7 = clip-only.
            if (!text.trimmed().isEmpty() && renderMode != 3 && renderMode != 7) {
                const double tlmScale = std::sqrt(tlm.b * tlm.b + tlm.d * tlm.d);
                const double ctmScaleY = std::sqrt(gs.ctm.c * gs.ctm.c
                                                   + gs.ctm.d * gs.ctm.d);
                const double effective = fontSize
                                       * (tlmScale  > 0.001 ? tlmScale  : 1.0)
                                       * (ctmScaleY > 0.001 ? ctmScaleY : 1.0);
                if (effective >= 1.0) {
                    // Pen offset along the line, through Tlm, then the CTM.
                    const double tx = penX * tlm.a + tlm.e;
                    const double ty = penX * tlm.b + tlm.f;
                    const QPointF p   = xf(tx, ty);
                    const double  qtY = out.pageH - p.y();
                    const double  estW = std::max(4.0,
                                             text.length() * effective * 0.55);
                    ContentCluster clu;
                    clu.bounds      = QRectF(p.x(), qtY - effective * 0.88,
                                             estW, effective * 1.05);
                    clu.origin      = QPointF(p.x(), qtY);
                    clu.text        = text;
                    // /Tf size through Tlm and CTM — the real thing, not an
                    // estimate: it can be written back to the file unchanged.
                    clu.fontSizePt    = effective;
                    clu.fontSizeExact = true;
                    clu.rawFontName   = curFont;
                    clu.textColor     = gs.fill;
                    out.clusters.append(std::move(clu));
                }
            }
            // Approximate pen advance (text space) so successive show ops on
            // one line don't overlap — also for skipped (invisible) text.
            // Resets on Td/TD/T*/Tm; it must NOT leak into the line matrix.
            penX += text.length() * 0.55 * fontSize;
        }

        ops.clear();
    }
}

// ── Annotation scan: AcroForm text fields + media ─────────────────────────────

QString annotStringKey(QPDFObjectHandle ann, const char *key)
{
    // Read a string key, walking up the /Parent chain (merged field/widget
    // dicts keep /T, /V on the field parent).
    try {
        QPDFObjectHandle cur = ann;
        for (int hop = 0; hop < 8 && cur.isDictionary(); ++hop) {
            auto v = cur.getKey(key);
            if (v.isString()) return QString::fromStdString(v.getUTF8Value());
            cur = cur.getKey("/Parent");
        }
    } catch (...) {}
    return {};
}

QString annotFieldType(QPDFObjectHandle ann)
{
    try {
        QPDFObjectHandle cur = ann;
        for (int hop = 0; hop < 8 && cur.isDictionary(); ++hop) {
            auto ft = cur.getKey("/FT");
            if (ft.isName()) return QString::fromStdString(ft.getName());
            cur = cur.getKey("/Parent");
        }
    } catch (...) {}
    return {};
}

void appendAnnotationItems(QList<ContentItem> &fields, QList<ContentItem> &media,
                           QPDFObjectHandle pageObj, double pageH)
{
    try {
        auto annots = pageObj.getKey("/Annots");
        if (!annots.isArray()) return;
        for (int i = 0; i < annots.getArrayNItems(); ++i) {
            auto ann = annots.getArrayItem(i);
            if (!ann.isDictionary()) continue;
            auto sub = ann.getKey("/Subtype");
            if (!sub.isName()) continue;
            const std::string subName = sub.getName();

            auto rectArr = ann.getKey("/Rect");
            if (!rectArr.isArray() || rectArr.getArrayNItems() != 4) continue;
            const double x1 = rectArr.getArrayItem(0).getNumericValue();
            const double y1 = rectArr.getArrayItem(1).getNumericValue();
            const double x2 = rectArr.getArrayItem(2).getNumericValue();
            const double y2 = rectArr.getArrayItem(3).getNumericValue();
            // PDF /Rect: [llx lly urx ury], Y=0 at bottom → Qt: Y=0 at top
            const QRectF bounds(std::min(x1, x2), pageH - std::max(y1, y2),
                                std::abs(x2 - x1), std::abs(y2 - y1));
            if (bounds.isEmpty()) continue;

            if (subName == "/Widget") {
                // Only text fields open the inline editor — buttons, checkboxes
                // and signatures are not text-editable areas.
                if (annotFieldType(ann) != QLatin1String("/Tx")) continue;

                ContentItem item;
                item.type      = ContentItem::Type::FormField;
                item.bounds    = bounds;
                item.fieldName = annotStringKey(ann, "/T");
                item.text      = annotStringKey(ann, "/V");

                // Font size from /DA ("0 g /Helv 12 Tf")
                const QString da = annotStringKey(ann, "/DA");
                if (!da.isEmpty()) {
                    const QStringList words = da.split(u' ', Qt::SkipEmptyParts);
                    double prev = 0.0;
                    for (const QString &w : words) {
                        bool ok = false;
                        const double v = w.toDouble(&ok);
                        if (ok) { prev = v; continue; }
                        if (w == QLatin1String("Tf") && prev > 0.0) {
                            item.fontSizePt    = prev;
                            item.fontSizeExact = true;
                            break;
                        }
                    }
                }
                fields.append(std::move(item));
            }
            else if (subName == "/Screen" || subName == "/RichMedia"
                     || subName == "/Movie") {
                ContentItem item;
                item.type      = ContentItem::Type::Media;
                item.bounds    = bounds;
                item.fieldName = annotStringKey(ann, "/T");
                media.append(std::move(item));
            }
        }
    } catch (...) {}
}

} // namespace

// ── Page assembly ─────────────────────────────────────────────────────────────

QList<ContentItem> qpdfBuildPageItems(const std::string &cs, double pageH,
                                      QPDFObjectHandle pageObj)
{
    ScanOut out;
    out.pageH = pageH;

    QPDFObjectHandle resources;
    try {
        if (pageObj.isDictionary()) resources = pageObj.getKey("/Resources");
    } catch (...) {}

    scanStream(cs, out, M{}, resources, 0);

    QList<ContentItem> items = classifyContentClusters(out.clusters);

    // Assign background colors: last-painted fill rect containing the item
    // center (shrunk 2 pt so thin borders never match).
    for (ContentItem &item : items) {
        const QPointF c = item.bounds.center();
        for (auto it = out.fills.rbegin(); it != out.fills.rend(); ++it) {
            if (it->rect.adjusted(2.0, 2.0, -2.0, -2.0).contains(c)) {
                item.bgColor = it->color;
                break;
            }
        }
    }

    // Form fields first so exact-hit lookups prefer them; media/images after
    // the text items (lowest priority in contentItemAt).
    QList<ContentItem> fields, media;
    if (!pageObj.isNull() && pageObj.isDictionary())
        appendAnnotationItems(fields, media, pageObj, pageH);

    QList<ContentItem> result;
    result.reserve(fields.size() + items.size() + media.size() + out.images.size());
    result += fields;
    result += items;
    result += media;
    result += out.images;

    qDebug() << "[ContentMap] clusters=" << out.clusters.size()
             << "items=" << items.size() << "fields=" << fields.size()
             << "media=" << media.size() << "images=" << out.images.size()
             << "fills=" << out.fills.size();

    return result;
}

#endif // HAVE_QPDF
