#include "engine/document/PdfiumText.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumPageSpace.hpp"
#include "engine/edit/StandardFont.hpp"

#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <QHash>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QRectF objectBoundsQt(FPDF_PAGEOBJECT obj, const PdfiumPageSpace &space)
{
    float left = 0, bottom = 0, right = 0, top = 0;
    if (!FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) return {};
    return space.toView(left, bottom, right, top);
}

QString typicalCharacters()
{
    QString out;
    for (char16_t c = 0x20; c < 0x7f; ++c) out += QChar(c);
    for (char16_t c = 0xa0; c <= 0xff; ++c) out += QChar(c);
    return out + QStringLiteral("\n\u2028\u2013\u2014\u2018\u2019\u201a\u201c\u201d\u201e\u2026\u2022\u20ac\u2212");
}

QString writableText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            out += c;
            out += text.at(++i);
        } else if (c == u'\t') {
            out += u' ';
        } else if (c == u'\n' || (c.unicode() >= 0x20 && !c.isSurrogate())) {
            out += c;
        }
    }
    return out;
}

QByteArray standardFontLike(FPDF_FONT font, const EditSession::Edit &edit)
{
    QString family = edit.fontFamily;
    bool bold      = edit.bold;
    bool italic    = edit.italic;
    if (font) {
        char name[128] = {};
        if (!edit.familyChanged && FPDFFont_GetBaseFontName(font, name, sizeof(name)) > 0) {
            family = QString::fromLatin1(name);
            constexpr int kSerifFlag = 1 << 1;
            if (FPDFFont_GetFlags(font) & kSerifFlag)
                family += QLatin1String(" Serif");
        }
        const PdfiumFonts::Face own = PdfiumFonts::faceOf(font);
        if (!edit.boldChanged)   bold   = bold || own.bold;
        if (!edit.italicChanged) italic = italic || own.italic;
    }
    return PdfiumFonts::standardFontFor(family, bold, italic);
}

void rotateObject(FPDF_PAGEOBJECT object, const EditSession::Edit &edit,
                  const PdfiumPageSpace &space)
{
    if (!object || qFuzzyIsNull(edit.box.rotationDeg)) return;
    const double a = qDegreesToRadians(-edit.box.rotationDeg);
    const double cs = std::cos(a), sn = std::sin(a);
    const QPointF center = space.toPdf(edit.pdfBounds.center());
    FPDFPageObj_Transform(object, cs, sn, -sn, cs,
                          center.x() - cs * center.x() + sn * center.y(),
                          center.y() - sn * center.x() - cs * center.y());
}

FPDF_PAGEOBJECT roundedRectPath(const QRectF &qtRect, double radius, const PdfiumPageSpace &space)
{
    const QRectF pdf = space.toPdf(qtRect);
    const double l = pdf.left(), r = pdf.right(), b = pdf.top(), t = pdf.bottom();
    const double rad = qBound(0.0, radius, qMin(qtRect.width(), qtRect.height()) / 2.0);
    if (rad <= 0.0) return FPDFPageObj_CreateNewRect(l, b, qtRect.width(), qtRect.height());
    constexpr double k = .5522847498;
    FPDF_PAGEOBJECT p = FPDFPageObj_CreateNewPath(l + rad, b);
    FPDFPath_LineTo(p, r - rad, b);
    FPDFPath_BezierTo(p, r - rad + k * rad, b, r, b + rad - k * rad, r, b + rad);
    FPDFPath_LineTo(p, r, t - rad);
    FPDFPath_BezierTo(p, r, t - rad + k * rad, r - rad + k * rad, t, r - rad, t);
    FPDFPath_LineTo(p, l + rad, t);
    FPDFPath_BezierTo(p, l + rad - k * rad, t, l, t - rad + k * rad, l, t - rad);
    FPDFPath_LineTo(p, l, b + rad);
    FPDFPath_BezierTo(p, l, b + rad - k * rad, l + rad - k * rad, b, l + rad, b);
    FPDFPath_Close(p);
    return p;
}

void setColor(FPDF_PAGEOBJECT object, const QColor &color, double opacity, bool stroke)
{
    const unsigned alpha = unsigned(qRound(color.alpha() * qBound(0.0, opacity, 1.0)));
    if (stroke) FPDFPageObj_SetStrokeColor(object, color.red(), color.green(), color.blue(), alpha);
    else        FPDFPageObj_SetFillColor(object, color.red(), color.green(), color.blue(), alpha);
}

void insertBoxDecoration(FPDF_PAGE page, const EditSession::Edit &edit,
                         const PdfiumPageSpace &space)
{
    const TextBoxProperties &box = edit.box;
    if (!box.backgroundEnabled && !box.borderEnabled) return;
    FPDF_PAGEOBJECT path = roundedRectPath(edit.pdfBounds, box.cornerRadiusPt, space);
    if (!path) return;
    if (box.backgroundEnabled) setColor(path, box.backgroundColor, box.opacity, false);
    const bool solidBorder = box.borderEnabled
                          && box.borderStyle == TextBoxProperties::BorderStyle::Solid;
    if (solidBorder) {
        setColor(path, box.borderColor, box.opacity, true);
        FPDFPageObj_SetStrokeWidth(path, qMax(.1, box.borderWidthPt));
    }
    FPDFPath_SetDrawMode(path, box.backgroundEnabled ? FPDF_FILLMODE_WINDING : FPDF_FILLMODE_NONE,
                         solidBorder);
    rotateObject(path, edit, space);
    FPDFPage_InsertObject(page, path);
    if (!box.borderEnabled || solidBorder) return;

    const QRectF r = space.toPdf(edit.pdfBounds);
    const double left = r.left(), right = r.right(), bottom = r.top(), top = r.bottom();
    FPDF_PAGEOBJECT dashed = FPDFPageObj_CreateNewPath(left, bottom);
    const double dash = box.borderStyle == TextBoxProperties::BorderStyle::Dotted
                            ? qMax(.2, box.borderWidthPt * .25)
                            : qMax(2., box.borderWidthPt * 4.);
    const double gap = qMax(2., box.borderWidthPt * 2.5);
    const auto edge = [&](double x1, double y1, double x2, double y2) {
        const double len = std::hypot(x2 - x1, y2 - y1);
        for (double at = 0; at < len; at += dash + gap) {
            const double e = qMin(len, at + dash);
            FPDFPath_MoveTo(dashed, x1 + (x2 - x1) * at / len, y1 + (y2 - y1) * at / len);
            FPDFPath_LineTo(dashed, x1 + (x2 - x1) * e / len, y1 + (y2 - y1) * e / len);
        }
    };
    edge(left, bottom, right, bottom);
    edge(right, bottom, right, top);
    edge(right, top, left, top);
    edge(left, top, left, bottom);
    setColor(dashed, box.borderColor, box.opacity, true);
    FPDFPageObj_SetStrokeWidth(dashed, qMax(.1, box.borderWidthPt));
    FPDFPath_SetDrawMode(dashed, FPDF_FILLMODE_NONE, true);
    rotateObject(dashed, edit, space);
    FPDFPage_InsertObject(page, dashed);
}

struct Sources {
    QList<FPDF_PAGEOBJECT> objects;
    QList<double>          rise;
};

// The text object each character of the original lines was drawn by, and how
// far it sits above the line's baseline; a space the text page made up takes
// the object of the character before it.
Sources sourceObjects(FPDF_TEXTPAGE textPage, const PdfiumPageSpace &space,
                      const QList<TextLayout::OriginalLine> &originals)
{
    struct Glyph {
        double x, y;
        FPDF_PAGEOBJECT object;
    };
    QList<Glyph> glyphs;
    for (int i = 0, n = FPDFText_CountChars(textPage); i < n; ++i) {
        FPDF_PAGEOBJECT object = FPDFText_GetTextObject(textPage, i);
        double x = 0, y = 0;
        if (!object || FPDFText_IsGenerated(textPage, i) == 1
                || !FPDFText_GetCharOrigin(textPage, i, &x, &y))
            continue;
        const QPointF shown = space.toView(x, y);
        glyphs.append({ shown.x(), shown.y(), object });
    }

    Sources out;
    for (int l = 0; l < originals.size(); ++l) {
        const TextLayout::OriginalLine &line = originals.at(l);
        if (l > 0) {
            out.objects.append(nullptr);
            out.rise.append(0.0);
        }
        const double reach = qMax(1.5, line.rect.height() * 0.6);
        QList<Glyph> row;
        for (const Glyph &g : std::as_const(glyphs))
            if (std::abs(g.y - line.baseline) <= reach) row.append(g);
        for (int k = 0; k < line.text.size(); ++k) {
            const Glyph *best = nullptr;
            if (!line.text.at(k).isSpace() && !line.text.at(k).isLowSurrogate()) {
                const double x = line.charX.value(k, line.endX);
                double bestScore = 0.75;
                for (const Glyph &g : std::as_const(row)) {
                    const double score = std::abs(g.x - x) + 0.1 * std::abs(g.y - line.baseline);
                    if (score < bestScore) { bestScore = score; best = &g; }
                }
            }
            if (best) {
                out.objects.append(best->object);
                out.rise.append(line.baseline - best->y);
            } else {
                out.objects.append(line.text.at(k).isLowSurrogate() && !out.objects.isEmpty()
                                       ? out.objects.last() : nullptr);
                out.rise.append(0.0);
            }
        }
    }
    FPDF_PAGEOBJECT last = nullptr;
    for (FPDF_PAGEOBJECT &object : out.objects) {
        if (object) last = object;
        else        object = last;
    }
    FPDF_PAGEOBJECT next = nullptr;
    for (int i = int(out.objects.size()) - 1; i >= 0; --i) {
        if (out.objects[i]) next = out.objects[i];
        else                out.objects[i] = next;
    }
    return out;
}

QByteArray baseFontName(FPDF_FONT font)
{
    char name[256] = {};
    return font && FPDFFont_GetBaseFontName(font, name, sizeof(name)) > 0 ? QByteArray(name)
                                                                          : QByteArray();
}

// PDFium's page writer names a font in the written content by its BaseFont,
// so of two fonts with one name both are written as whichever came first.
QHash<QByteArray, QSet<FPDF_FONT>> fontsByName(FPDF_PAGE page)
{
    QHash<QByteArray, QSet<FPDF_FONT>> out;
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (obj && FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_TEXT)
            if (FPDF_FONT font = FPDFTextObj_GetFont(obj)) out[baseFontName(font)].insert(font);
    }
    return out;
}

}

namespace PdfiumText {

QString objectText(FPDF_PAGEOBJECT obj, FPDF_TEXTPAGE textPage)
{
    if (!textPage) return {};
    const unsigned long bytes = FPDFTextObj_GetText(obj, textPage, nullptr, 0);
    if (bytes <= 2) return {};
    std::vector<unsigned short> buffer(bytes / 2 + 1, 0);
    FPDFTextObj_GetText(obj, textPage, buffer.data(), bytes);
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()));
}

/// Picks the font for each character of one style: its own where it can draw it, a similar one where not.
class FontChoice
{
public:
    FontChoice(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_FONT primary, const QString &known)
        : m_doc(doc), m_page(page), m_primary(primary)
    {
        for (const uint cp : known.toUcs4()) m_known.insert(cp);
    }

    void prepare(const QString &text)
    {
        QList<uint> unverified;
        for (const uint cp : text.toUcs4())
            if (!QChar::isSpace(cp) && !m_known.contains(cp) && !m_verified.contains(cp)
                    && !unverified.contains(cp) && PdfiumFonts::hasGlyph(m_primary, cp))
                unverified.append(cp);
        m_decodable += PdfiumFonts::decodable(m_doc, m_page, m_primary, unverified);
        m_verified += QSet<uint>(unverified.cbegin(), unverified.cend());
    }

    FPDF_FONT fontFor(char32_t cp)
    {
        const auto cached = m_fonts.constFind(uint(cp));
        if (cached != m_fonts.cend()) return *cached;
        if (!m_verified.contains(uint(cp))) prepare(QString::fromUcs4(&cp, 1));
        const bool own = QChar::isSpace(cp) || m_known.contains(uint(cp))
            || (PdfiumFonts::hasGlyph(m_primary, cp) && m_decodable.contains(uint(cp)));
        FPDF_FONT font = own ? m_primary : PdfiumFonts::fallbackFont(m_doc, m_primary, cp);
        m_fonts.insert(uint(cp), font);
        return font;
    }

private:
    FPDF_DOCUMENT          m_doc;
    FPDF_PAGE              m_page;
    FPDF_FONT              m_primary;
    QSet<uint>             m_known;
    QSet<uint>             m_verified;
    QSet<uint>             m_decodable;
    QHash<uint, FPDF_FONT> m_fonts;
};

Origin originOf(const QList<FPDF_PAGEOBJECT> &objects, FPDF_TEXTPAGE textPage)
{
    Origin out;
    QList<FPDF_FONT> fonts;
    QHash<FPDF_FONT, int> weight;
    QHash<FPDF_FONT, QString> written;
    for (FPDF_PAGEOBJECT obj : objects) {
        FPDF_FONT font = FPDFTextObj_GetFont(obj);
        if (!fonts.contains(font)) fonts.append(font);
        const QString text = objectText(obj, textPage);
        weight[font] += qMax<int>(1, text.trimmed().size());
        written[font] += text;
    }
    for (FPDF_FONT font : std::as_const(fonts))
        if (!out.font || weight.value(font) > weight.value(out.font)) out.font = font;
    if (!out.font) return out;
    out.text = written.value(out.font);
    for (FPDF_PAGEOBJECT obj : objects) {
        if (FPDFTextObj_GetFont(obj) != out.font) continue;
        float size = 0.f;
        FS_MATRIX m {};
        if (FPDFTextObj_GetFontSize(obj, &size) && size > 0.f) {
            const double scale = FPDFPageObj_GetMatrix(obj, &m) ? std::hypot(m.c, m.d) : 1.0;
            out.sizePt = scale > 0.0 ? size * scale : size;
        }
        break;
    }
    return out;
}

Typeset::Typeset(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_TEXTPAGE textPage,
                 const EditSession::Edit &edit, const Origin *origin, bool laidOut,
                 const QHash<int, FPDF_PAGE> *sourcePages, bool keepObjects)
    : m_doc(doc), m_page(page), m_textPage(textPage), m_edit(edit), m_givenPages(sourcePages)
{
    m_fontNames = fontsByName(page);
    m_text     = writableText(edit.newText);
    m_original = TextLayout::originalText(edit.originalLines);
    // A scan's text layer is invisible: its objects give neither style nor kept text.
    const Sources sources = textPage && !edit.originalLines.isEmpty() && !edit.scanned
        ? sourceObjects(textPage, PdfiumPageSpace(page), edit.originalLines) : Sources();
    m_mapped = sources.objects.size() == m_original.size()
            && !sources.objects.isEmpty() && sources.objects.first() != nullptr;
    if (m_mapped) {
        readStyles(sources.objects, sources.rise);
        m_sourceObjects = sources.objects;
    }
    else        addDefaultStyle(origin);
    // A scan is set in the face its pixels match; its text layer's glyphs are boxes.
    if (!edit.scanned) {
        findStyleSources();
        addSourceStyles();
    }
    for (const Style &s : std::as_const(m_styles)) m_ownFonts.append(s.font);
    applyOverrides();


    const QString measured = TextLayout::measuredCharacters(m_text, edit.box.listStyle)
                           + QLatin1Char(' ');
    for (const Style &s : std::as_const(m_styles)) {
        m_fonts.push_back(std::make_unique<FontChoice>(doc, page, s.font, s.known));
        m_fonts.back()->prepare(measured);
    }
    m_charStyles = TextLayout::styleIndices(m_text, m_original, m_originalStyles);
    for (qsizetype i = 0; i < m_charStyles.size() && !edit.scanned && m_text.size() == m_edit.sources.size(); ++i) {
        const qsizetype k = m_sources.indexOf(m_edit.sources.at(i));
        if (k >= 0) m_charStyles[i] = m_sourceStyles.at(k);
    }

    m_size = m_styles.at(TextLayout::dominantStyle(m_charStyles, int(m_styles.size()))).size;

    const TextBoxProperties &box = edit.box;
    // Lines where cut text was set already hold what the style changes did.
    // A change taken back again keeps the text where it is.
    const bool resized = edit.sizeChanged
        && !(edit.baseSizePt > 0.0 && std::abs(edit.fontSizePt - edit.baseSizePt) < 0.005);
    // A scan's text layer places its characters only roughly: the text flows in its own face.
    m_anchor = !edit.originalLines.isEmpty() && !edit.scanned && (edit.laid || (!m_restyled && !resized))
            && TextLayout::anchors(box);
    m_keep = keepObjects && m_anchor && !edit.scanned && !edit.colorChanged && !edit.underline
          && box.opacity >= 0.999 && qFuzzyIsNull(box.rotationDeg)
          && !box.backgroundEnabled && !box.borderEnabled;
    if (!laidOut) return;

    TextLayout::Params params;
    params.lineStepPt         = TextLayout::lineStep(m_size, edit.lineSpacingPt);
    params.paragraphSpacingPt = box.paragraphSpacingPt;
    params.charSpacingPt      = box.characterSpacingPt;
    params.list               = box.listStyle;
    params.align              = box.horizontalAlign;
    params.anchor             = m_anchor;
    params.keep               = m_keep;
    m_layout = TextLayout::layout(m_text, params, box, edit.pdfBounds, m_size, true,
                                  edit.hasTextOrigin, edit.textOriginOffset,
                                  edit.originalLines, advanceFunction());
}

Typeset::~Typeset()
{
    for (FPDF_FONT font : m_owned) FPDFFont_Close(font);
    for (const auto &[page, text] : m_sourcePages) {
        if (text) FPDFText_ClosePage(text);
        if (page) FPDF_ClosePage(page);
    }
}

void Typeset::addDefaultStyle(const Origin *origin)
{
    const EditSession::Edit &e = m_edit;
    Style s;
    s.size = e.fontSizePt > 0.0 ? e.fontSizePt : qMax(6.0, e.pdfBounds.height() * 0.8);
    if (!e.sizeChanged && origin && origin->sizePt > 0.0) s.size = origin->sizePt;
    if (!e.familyChanged && origin && origin->font && !PdfiumFonts::isType3(origin->font)) {
        s.font  = origin->font;
        s.known = origin->text;
    } else if (e.familyChanged || PdfiumFonts::hasFace(e.fontFamily, e.bold, e.italic)) {
        // Only the family itself; for a name the system lacks Qt would answer
        // with a substitute of its own choosing.
        s.font = PdfiumFonts::loadFont(m_doc, e.fontFamily, e.bold, e.italic);
    }
    if (!s.font) {
        s.font = standardFont(standardFontLike(origin ? origin->font : nullptr, e));
    }
    m_styles.append(s);
    m_originalStyles = QList<int>(m_original.size(), 0);
}

void Typeset::applyOverrides()
{
    const EditSession::Edit &e = m_edit;
    const QColor text = e.textColor.isValid() ? e.textColor : QColor(Qt::black);
    // A new size scales every size of the box alike, so an index or a larger
    // word inside it keeps its proportion.
    const double scale = e.sizeChanged && e.fontSizePt > 0.0 && e.baseSizePt > 0.0
                       ? e.fontSizePt / e.baseSizePt : 1.0;
    for (Style &s : m_styles) {
        if (e.sizeChanged && e.fontSizePt > 0.0)
            s.size = m_mapped && e.baseSizePt > 0.0 ? s.size * scale : e.fontSizePt;
        if (e.colorChanged || !s.fill.isValid()) s.fill = text;
        if (s.font && !writable(s.font)) {
            // A font sharing its name with another is written as a copy of its own program.
            if (FPDF_FONT copy = PdfiumFonts::embeddedCopy(m_doc, s.font)) {
                s.font = copy;
                s.known.clear();
                m_restyled |= restyle(s);
                continue;
            }
            const PdfiumFonts::Face own = PdfiumFonts::faceOf(s.font);
            FPDF_FONT font = !own.family.isEmpty() && PdfiumFonts::hasFace(own.family, own.bold, own.italic)
                ? PdfiumFonts::loadFont(m_doc, own.family, own.bold, own.italic) : nullptr;
            s.font = font ? font : standardFont(standardFontLike(s.font, e));
            s.known.clear();
        }
        m_restyled |= restyle(s);
    }
}

bool Typeset::restyle(Style &s)
{
    const EditSession::Edit &e = m_edit;
    if (!s.font || (!e.familyChanged && !e.boldChanged && !e.italicChanged)) return false;
    const PdfiumFonts::Face own = PdfiumFonts::faceOf(s.font);
    const bool drawnBold   = own.bold || s.mode == FPDF_TEXTRENDERMODE_FILL_STROKE;
    const bool drawnItalic = own.italic || std::abs(s.c) > 0.05;
    const QString family = e.familyChanged ? e.fontFamily : own.family;
    const bool bold   = e.boldChanged ? e.bold : drawnBold;
    const bool italic = e.italicChanged ? e.italic : drawnItalic;
    // Bold taken off again, or the family the text has: nothing changes.
    if ((!e.familyChanged || family == own.family) && bold == drawnBold && italic == drawnItalic)
        return false;

    // Each attribute changes on its own: making a box bold keeps its italic
    // words italic and its fonts where they were.
    if (!family.isEmpty() && PdfiumFonts::hasFace(family, bold, italic)) {
        if (FPDF_FONT font = PdfiumFonts::loadFont(m_doc, family, bold, italic)) {
            s.font = font;
            s.known.clear();
            s.c    = 0.0;
            s.mode = 0;
            return true;
        }
    }
    bool fontBold = own.bold, fontItalic = own.italic;
    if (e.familyChanged) {
        FPDF_FONT font = PdfiumFonts::loadFont(m_doc, family, false, false);
        if (!font) font = standardFont(standardFontLike(nullptr, e));
        if (font) {
            s.font = font;
            s.known.clear();
            fontBold = fontItalic = false;
        }
    }
    // Without the face on this system the glyphs at hand are slanted and
    // thickened instead of swapped for another font.
    if (italic && !fontItalic) s.c = std::abs(s.c) > 0.05 ? s.c : 0.21;
    else if (!italic)          s.c = 0.0;
    if (bold && !fontBold) {
        s.mode        = FPDF_TEXTRENDERMODE_FILL_STROKE;
        s.stroke      = s.fill;
        s.strokeWidth = s.size * 0.03;
    } else if (!bold) {
        s.mode = 0;
    }
    return true;
}

bool Typeset::writable(FPDF_FONT font) const
{
    // A Type 3 font cannot be written by PDFium's page writer at all.
    return !PdfiumFonts::isType3(font) && m_fontNames.value(baseFontName(font)).size() <= 1;
}

FPDF_FONT Typeset::standardFont(const QByteArray &name)
{
    if (m_fontNames.contains(name)) {
        // The page already has a font of this name; a font of our own keeps
        // the new text apart from it.
        const StandardFont::Kind kind = StandardFont::kindOf(QString::fromLatin1(name));
        const bool bold   = name.contains("Bold");
        const bool italic = name.contains("Italic") || name.contains("Oblique");
        for (const QString &family : StandardFont::qtFamilies(kind))
            if (FPDF_FONT font = PdfiumFonts::loadFont(m_doc, family, bold, italic)) return font;
    }
    FPDF_FONT font = FPDFText_LoadStandardFont(m_doc, name.constData());
    if (font) m_owned.push_back(font);
    return font;
}

double Typeset::advance(int style, char32_t codePoint) const
{
    if (style < 0 || style >= m_styles.size()) return 0.0;
    const Style &s = m_styles.at(style);
    FPDF_FONT font = m_fonts.at(size_t(style))->fontFor(codePoint);
    if (!font) return 0.0;
    return PdfiumFonts::glyphWidth(font, codePoint, s.size) * s.a + s.track;
}

int Typeset::styleAt(int position) const
{
    if (position >= 0 && position < m_charStyles.size()) return m_charStyles.at(position);
    return m_charStyles.value(0, 0);
}

TextLayout::Advance Typeset::advanceFunction() const
{
    return [this](int position, char32_t codePoint) {
        return advance(styleAt(position), codePoint);
    };
}

QList<FPDF_PAGEOBJECT> Typeset::replaces(const QList<FPDF_PAGEOBJECT> &objects,
                                         const PdfiumPageSpace &space) const
{
    if (!m_layout.anchored) return objects;
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    QList<int> keptCount(originals.size(), 0);
    for (const TextLayout::Line &line : m_layout.lines) {
        if (line.original < 0 || line.original >= originals.size()) continue;
        keptCount[line.original] = line.keep ? int(originals.at(line.original).text.size())
                                             : line.keptCount;
    }
    // An object stays exactly when the layout keeps every character of it,
    // decided by the characters, as the layout does, not by the ink, which
    // reaches past the last advance in italics.
    QSet<FPDF_PAGEOBJECT> kept, set;
    if (!m_sourceObjects.isEmpty()) {
        int at = 0;
        for (int j = 0; j < originals.size(); ++j) {
            const int n = int(originals.at(j).text.size());
            for (int i = 0; i < n; ++i)
                if (FPDF_PAGEOBJECT obj = m_sourceObjects.value(at + i))
                    (i < keptCount.at(j) ? kept : set).insert(obj);
            at += n + 1;
        }
    }
    QList<double> restartAt(originals.size(), -1.0);
    for (const TextLayout::Line &line : m_layout.lines) {
        if (line.original < 0 || line.original >= originals.size()) continue;
        restartAt[line.original] = line.keep
            ? std::numeric_limits<double>::max()
            : TextLayout::restartX(originals.at(line.original), line.keptCount);
    }
    QList<FPDF_PAGEOBJECT> out;
    for (FPDF_PAGEOBJECT obj : objects) {
        if (kept.contains(obj) || set.contains(obj)) {
            if (set.contains(obj)) out.append(obj);
            continue;
        }
        const QRectF bounds = objectBoundsQt(obj, space);
        // An object no line of the edit accounts for is taken out, as when
        // the lines are where text cut from another box was set.
        bool inLine = false;
        for (int j = 0; j < originals.size() && !inLine; ++j) {
            if (!originals.at(j).rect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(bounds.center()))
                continue;
            inLine = true;
            const double restart = restartAt.at(j) < 0.0 ? -1e9 : restartAt.at(j);
            if (bounds.right() > restart + 0.25) out.append(obj);
        }
        if (!inLine) out.append(obj);
    }
    return out;
}

void Typeset::insertInto(FPDF_PAGE page, const PdfiumPageSpace &space) const
{
    if (m_text.isEmpty() || m_styles.isEmpty()) return;
    insertBoxDecoration(page, m_edit, space);

    const QRectF &bounds = m_edit.pdfBounds;
    const QPointF base = m_layout.firstBaseline;
    const double spacing = m_edit.box.characterSpacingPt;
    const TextLayout::Advance advance = advanceFunction();
    for (const TextLayout::Line &line : m_layout.lines) {
        if (line.keep) continue;
        const double baseline = bounds.top() + base.y() + line.y;
        const double lineX = bounds.left() + base.x() + line.x;

        QList<Placed> glyphs;
        if (!line.prefix.isEmpty()) {
            double x = lineX - line.prefixWidth;
            const int style = styleAt(line.start);
            for (const char32_t cp : line.prefix.toUcs4()) {
                glyphs.append({ cp, x, style });
                x += advance(-1, cp) + spacing;
            }
        }
        double inkStart = -1.0;
        const int end = line.start + line.length;
        for (int i = line.start + line.keptCount; i < end;) {
            char32_t cp = m_text.at(i).unicode();
            int len = 1;
            if (m_text.at(i).isHighSurrogate() && i + 1 < end && m_text.at(i + 1).isLowSurrogate()) {
                cp  = QChar::surrogateToUcs4(m_text.at(i), m_text.at(i + 1));
                len = 2;
            }
            const double x = lineX + TextLayout::xAt(line, m_text, i, spacing, advance);
            if (inkStart < 0.0 && !QChar::isSpace(cp)) inkStart = x;
            glyphs.append({ cp, x, styleAt(i) });
            i += len;
        }
        insertRuns(page, glyphs, baseline, space);

        if (!m_edit.underline || inkStart < 0.0) continue;
        const Style &s = m_styles.at(styleAt(line.start));
        const double thickness = qMax(0.3, s.size * 0.06);
        const double width = lineX + line.width - inkStart;
        const QRectF under = space.toPdf(QRectF(inkStart, baseline + s.size * 0.12 - thickness,
                                                width, thickness));
        FPDF_PAGEOBJECT rule = FPDFPageObj_CreateNewRect(under.left(), under.top(),
                                                         under.width(), under.height());
        if (!rule) continue;
        setColor(rule, s.fill, m_edit.box.opacity, false);
        FPDFPath_SetDrawMode(rule, FPDF_FILLMODE_WINDING, false);
        rotateObject(rule, m_edit, space);
        FPDFPage_InsertObject(page, rule);
    }
}

void Typeset::insertRuns(FPDF_PAGE page, const QList<Placed> &glyphs, double baseline,
                         const PdfiumPageSpace &space) const
{
    struct Run {
        int          style;
        FPDF_FONT    font;
        QString      text;
        QList<double> x;
    };
    QList<Run> runs;
    for (const Placed &g : glyphs) {
        const QString ch = QString::fromUcs4(&g.codePoint, 1);
        // A run that starts with a space is read back without it, so a space
        // ends the run before it instead.
        if (QChar::isSpace(g.codePoint) && !runs.isEmpty()) {
            runs.last().text += ch;
            runs.last().x.append(g.x);
            continue;
        }
        FPDF_FONT font = m_fonts.at(size_t(g.style))->fontFor(g.codePoint);
        if (!font) continue;
        if (runs.isEmpty() || runs.last().style != g.style || runs.last().font != font)
            runs.append({ g.style, font, {}, {} });
        runs.last().text += ch;
        runs.last().x.append(g.x);
    }

    for (const Run &run : std::as_const(runs))
        if (!run.text.trimmed().isEmpty())
            insertRun(page, run.style, run.font, run.text.toUcs4(), run.x, baseline, space);
}

void Typeset::insertRun(FPDF_PAGE page, int style, FPDF_FONT font, const QList<uint> &codePoints,
                        const QList<double> &x, double baseline,
                        const PdfiumPageSpace &space) const
{
    const Style &s = m_styles.at(style);
    FPDF_PAGEOBJECT obj = FPDFPageObj_CreateTextObj(m_doc, font, float(s.size));
    if (!obj) return;
    PdfiumFonts::setText(obj, font, QString::fromUcs4(
        reinterpret_cast<const char32_t *>(codePoints.constData()), codePoints.size()));
    if (codePoints.size() > 1) {
        std::vector<float> positions;
        for (int i = 1; i < codePoints.size(); ++i)
            positions.push_back(float((x.at(i) - x.first()) / s.a));
        if (!FPDFText_SetPositions(obj, positions.data(), positions.size())) {
            FPDFPageObj_Destroy(obj);
            for (int i = 0; i < codePoints.size(); ++i)
                if (!QChar::isSpace(codePoints.at(i)))
                    insertRun(page, style, font, { codePoints.at(i) }, { x.at(i) }, baseline,
                              space);
            return;
        }
    }
    // The glyphs stand upright on the shown page, whatever way the page turns.
    const QPointF across = space.rightward() * s.a;
    const QPointF up = space.rightward() * s.c + space.upward();
    const QPointF origin = space.toPdf(QPointF(x.first(), baseline - s.rise));
    const FS_MATRIX m { float(across.x()), float(across.y()), float(up.x()), float(up.y()),
                        float(origin.x()), float(origin.y()) };
    FPDFPageObj_SetMatrix(obj, &m);
    setColor(obj, s.fill, m_edit.box.opacity, false);
    if (s.mode != 0) {
        FPDFTextObj_SetTextRenderMode(obj, FPDF_TEXT_RENDERMODE(s.mode));
        if (s.stroke.isValid()) setColor(obj, s.stroke, m_edit.box.opacity, true);
        FPDFPageObj_SetStrokeWidth(obj, float(s.strokeWidth));
    }
    rotateObject(obj, m_edit, space);
    FPDFPage_InsertObject(page, obj);
}

QList<TextLayout::OriginalLine> Typeset::laidLines(QList<QList<TextLayout::Source>> *sources) const
{
    // Where every character of the text is set, line by line, and a character
    // of the document in its style for each.
    QList<TextLayout::OriginalLine> out;
    const QRectF &bounds = m_edit.pdfBounds;
    const QPointF base = m_layout.firstBaseline;
    const double spacing = m_edit.box.characterSpacingPt;
    const TextLayout::Advance advance = advanceFunction();
    for (const TextLayout::Line &line : m_layout.lines) {
        TextLayout::OriginalLine laid;
        QList<TextLayout::Source> lineSources;
        laid.baseline = bounds.top() + base.y() + line.y;
        const double lineX = bounds.left() + base.x() + line.x;
        double top = laid.baseline, bottom = laid.baseline;
        const int end = line.start + line.length;
        for (int i = line.start; i < end && i < m_text.size(); ++i) {
            const QChar ch = m_text.at(i);
            if (ch == u'\n' || ch == QChar(TextLayout::kSoftBreak)) continue;
            const double size = m_styles.at(styleAt(i)).size;
            laid.text += ch;
            laid.charX.append(lineX + TextLayout::xAt(line, m_text, i, spacing, advance));
            laid.objectStart.append(false);
            top    = qMin(top, laid.baseline - size * 0.8);
            bottom = qMax(bottom, laid.baseline + size * 0.25);
            lineSources.append(m_styleSources.value(styleAt(i)));
        }
        if (laid.text.isEmpty()) continue;
        // What the layout keeps is still the original objects, set where
        // they were, so it is kept again when the box is edited again.
        if (m_keep && line.original >= 0 && line.original < m_edit.originalLines.size()) {
            const TextLayout::OriginalLine &o = m_edit.originalLines.at(line.original);
            const int kept = line.keep ? int(o.text.size()) : line.keptCount;
            for (int k = 0; k < kept && k < laid.objectStart.size() && k < o.objectStart.size(); ++k)
                laid.objectStart[k] = o.objectStart.at(k);
        }
        laid.endX = lineX + TextLayout::xAt(line, m_text, end, spacing, advance);
        laid.rect = QRectF(QPointF(laid.charX.first(), top), QPointF(laid.endX, bottom));
        out.append(laid);
        if (sources) sources->append(lineSources);
    }
    return out;
}

TextLayout::Metrics Typeset::metrics() const
{
    TextLayout::Metrics out;
    if (m_styles.isEmpty()) return out;
    out.original       = m_original;
    out.originalStyles = m_originalStyles;
    out.originals      = m_edit.originalLines;
    out.anchor         = m_anchor;
    out.boxTopLeft     = m_edit.pdfBounds.topLeft();
    out.sources        = m_sources;
    out.sourceStyles   = m_sourceStyles;
    out.styleSources   = m_styleSources;
    // The usual characters come along, so typing them needs no new metrics.
    const QString characters = TextLayout::measuredCharacters(m_text, m_edit.box.listStyle)
                             + typicalCharacters();
    const QList<uint> measured = characters.toUcs4();
    for (int s = 0; s < m_styles.size(); ++s) {
        const Style &style = m_styles.at(s);
        out.sizes.append(style.size);
        m_fonts.at(size_t(s))->prepare(characters);
        QHash<uint, double> table;
        for (const uint cp : measured)
            if (!table.contains(cp)) table.insert(cp, advance(s, char32_t(cp)));
        table.insert(uint(u'\t'), table.value(uint(u' ')));
        out.advances.append(table);
        // The family as the document names it, unless one was picked; bold
        // and italic as drawn, also where they are simulated.
        const PdfiumFonts::Face face = PdfiumFonts::faceOf(style.font);
        out.appearances.append({ m_edit.familyChanged
                                     ? m_edit.fontFamily
                                     : PdfiumFonts::familyName(m_ownFonts.value(s, style.font)),
                                 style.size,
                                 face.bold || style.mode == FPDF_TEXTRENDERMODE_FILL_STROKE,
                                 face.italic || std::abs(style.c) > 0.05, style.fill });
    }
    return out;
}

}

#endif
