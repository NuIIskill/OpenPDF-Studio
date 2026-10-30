#include "engine/document/OriginalContent.hpp"

#include "app/PdfPwStore.hpp"

#include <QDebug>
#include <QFile>

#ifdef HAVE_QPDF
#  include <qpdf/Pl_Buffer.hh>
#  include <qpdf/Pl_Discard.hh>
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFWriter.hh>
#  include <qpdf/Buffer.hh>
#  include <cstring>

#  include <regex>
#  include <set>
#  include <string>
#  include <vector>
#endif

#ifdef HAVE_QPDF
namespace {

using Token = QPDFTokenizer::Token;

// Walks a page's content the way PDFium builds its object list and, when
// writing, passes every token through except the text shows that were removed.
class ObjectFilter : public QPDFObjectHandle::TokenFilter
{
public:
    ObjectFilter(QPDFObjectHandle resources, const OriginalContent::Page *page)
        : m_resources(resources), m_page(page) {}

    const OriginalContent::Scan &scanned() const { return m_scan; }
    int openSaves() const { return int(m_saved.size()); }

    void handleToken(const Token &token) override
    {
        if (m_inImage) {
            writeToken(token);
            if (token.getType() == QPDFTokenizer::tt_inline_image) m_inImage = false;
            return;
        }
        if (token.getType() == QPDFTokenizer::tt_eof) return;
        if (token.getType() != QPDFTokenizer::tt_word) {
            m_operands.push_back(token);
            return;
        }
        handleOperator(token);
        m_operands.clear();
    }

private:
    struct State { double charSpacing { 0.0 }; double wordSpacing { 0.0 }; double scale { 1.0 }; };

    std::vector<Token> values() const
    {
        std::vector<Token> out;
        for (const Token &t : m_operands)
            if (t.getType() != QPDFTokenizer::tt_space && t.getType() != QPDFTokenizer::tt_comment)
                out.push_back(t);
        return out;
    }

    static double number(const Token &t)
    {
        try { return std::stod(t.getValue()); } catch (...) { return 0.0; }
    }

    void pass(const Token &op)
    {
        for (const Token &t : m_operands) writeToken(t);
        writeToken(op);
    }

    bool showsText(const std::string &op) const
    {
        const std::vector<Token> v = values();
        if (op == "TJ") {
            int strings = 0;
            bool empty = true;
            for (const Token &t : v)
                if (t.getType() == QPDFTokenizer::tt_string) {
                    ++strings;
                    empty = empty && t.getValue().empty();
                }
            return strings > 1 || (strings == 1 && !empty);
        }
        return !v.empty() && v.back().getType() == QPDFTokenizer::tt_string
            && !v.back().getValue().empty();
    }

    int xobjectType(const std::vector<Token> &v) const
    {
        if (v.empty() || !m_resources.isDictionary()) return 0;
        QPDFObjectHandle dict = m_resources.getKey("/XObject");
        if (!dict.isDictionary() || !dict.hasKey(v.back().getValue())) return 0;
        QPDFObjectHandle x = dict.getKey(v.back().getValue());
        if (!x.isStream()) return 0;
        QPDFObjectHandle subtype = x.getDict().getKey("/Subtype");
        if (subtype.isNameAndEquals("/Image")) return OriginalContent::Image;
        if (subtype.isNameAndEquals("/Form")) return OriginalContent::Form;
        return 0;
    }

    bool hasShading(const std::vector<Token> &v) const
    {
        if (v.empty() || !m_resources.isDictionary()) return false;
        QPDFObjectHandle dict = m_resources.getKey("/Shading");
        return dict.isDictionary() && dict.hasKey(v.back().getValue());
    }

    int object(int type)
    {
        m_scan.objects.append(type);
        if (type == OriginalContent::Text)
            m_scan.spacedText.append(!qFuzzyIsNull(m_state.charSpacing)
                                     || !qFuzzyIsNull(m_state.wordSpacing));
        return m_scan.objects.size() - 1;
    }

    void writeAdvance(int keptIndex)
    {
        // A show that follows left-out text without its own positioning gets
        // the pen movement of that text, so it starts where it did.
        if (m_runStart < 0 || !m_page) return;
        const QTransform from = m_page->textMatrices.value(m_runStart);
        const QTransform to   = m_page->textMatrices.value(keptIndex);
        const double det = from.m11() * from.m22() - from.m12() * from.m21();
        if (qFuzzyIsNull(det) || qFuzzyIsNull(m_fontSize) || qFuzzyIsNull(m_state.scale)) return;
        const double dx = to.dx() - from.dx(), dy = to.dy() - from.dy();
        const double u = (from.m22() * dx - from.m21() * dy) / det;
        const double tx = u * m_runScale;
        const double adjust = -tx * 1000.0 / (m_fontSize * m_state.scale);
        write("[" + QByteArray::number(adjust, 'f', 4).toStdString() + "] TJ\n");
    }

    void handleOperator(const Token &opToken)
    {
        const std::string op = opToken.getValue();
        static const std::set<std::string> repositions { "BT", "ET", "Td", "TD", "Tm", "T*" };
        if (repositions.count(op)) m_runStart = -1;

        if (op == "BI") {
            object(OriginalContent::Image);
            pass(opToken);
            m_inImage = true;
            return;
        }
        if (op == "m") ++m_points;
        else if (op == "l") ++m_points;
        else if (op == "c" || op == "v" || op == "y") m_points += 3;
        else if (op == "re") m_points += 5;
        else if (op == "S" || op == "s" || op == "f" || op == "F" || op == "f*" || op == "B"
                 || op == "B*" || op == "b" || op == "b*") {
            if (m_points >= 2) object(OriginalContent::Path);
            m_points = 0;
        } else if (op == "n") {
            m_points = 0;
        } else if (op == "Do") {
            if (const int type = xobjectType(values())) object(type);
        } else if (op == "sh") {
            if (hasShading(values())) object(OriginalContent::Shading);
        } else if (op == "q") {
            m_saved.push_back(m_state);
        } else if (op == "Q") {
            // A Q without its q is ignored by readers; written, it would close the q around the original.
            if (m_saved.empty()) return;
            m_state = m_saved.back();
            m_saved.pop_back();
        } else if (op == "Tc" || op == "Tw" || op == "Tz") {
            const std::vector<Token> v = values();
            if (!v.empty()) {
                const double n = number(v.back());
                if (op == "Tc") m_state.charSpacing = n;
                else if (op == "Tw") m_state.wordSpacing = n;
                else m_state.scale = n / 100.0;
            }
        } else if (op == "Tf") {
            const std::vector<Token> v = values();
            if (v.size() >= 2) m_fontSize = number(v.back());
        } else if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
            showText(opToken);
            return;
        }
        pass(opToken);
    }

    void showText(const Token &opToken)
    {
        const std::string op = opToken.getValue();
        const std::vector<Token> v = values();
        if (op == "\"" && v.size() >= 2) {
            m_state.wordSpacing = number(v.at(0));
            m_state.charSpacing = number(v.at(1));
        }
        if (!showsText(op)) {
            pass(opToken);
            return;
        }
        const int index = object(OriginalContent::Text);
        const bool removed = m_page && m_page->removed.contains(index);
        if (!removed) {
            if (op == "Tj" || op == "TJ") writeAdvance(index);
            m_runStart = -1;
            pass(opToken);
            return;
        }
        if (op == "'" || op == "\"") {
            if (op == "\"" && v.size() >= 2)
                write(v.at(0).getRawValue() + " Tw " + v.at(1).getRawValue() + " Tc ");
            write("T*\n");
            m_runStart = -1;
        }
        if (m_runStart < 0) {
            m_runStart = index;
            m_runScale = m_state.scale;
        }
    }

    QPDFObjectHandle               m_resources;
    const OriginalContent::Page   *m_page;
    OriginalContent::Scan          m_scan;
    std::vector<Token>             m_operands;
    std::vector<State>             m_saved;
    State                          m_state;
    double                         m_fontSize { 0.0 };
    double                         m_runScale { 1.0 };
    int                            m_runStart { -1 };
    int                            m_points   { 0 };
    bool                           m_inImage  { false };
};

void addMissingResources(QPDF &out, QPDFObjectHandle from, QPDFObjectHandle into)
{
    for (const std::string &category : from.getKeys()) {
        QPDFObjectHandle entries = from.getKey(category);
        if (!entries.isDictionary()) {
            if (!into.hasKey(category)) into.replaceKey(category, out.copyForeignObject(entries));
            continue;
        }
        QPDFObjectHandle target = into.getKey(category);
        if (!target.isDictionary()) {
            target = QPDFObjectHandle::newDictionary();
            into.replaceKey(category, target);
        }
        for (const std::string &name : entries.getKeys())
            if (!target.hasKey(name))
                target.replaceKey(name, out.copyForeignObject(entries.getKey(name)));
    }
}

std::string toUnicodeMap(const QMap<quint32, uint> &glyphs)
{
    std::string out =
        "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    const auto hex = [](uint v, int digits) {
        return QByteArray::number(v, 16).rightJustified(digits, '0').toUpper().toStdString();
    };
    QList<quint32> ids = glyphs.keys();
    for (qsizetype start = 0; start < ids.size(); start += 100) {
        const qsizetype n = qMin<qsizetype>(100, ids.size() - start);
        out += std::to_string(n) + " beginbfchar\n";
        for (qsizetype i = start; i < start + n; ++i) {
            const char32_t cp = glyphs.value(ids.at(i));
            std::string unicode;
            for (const QChar c : QString::fromUcs4(&cp, 1)) unicode += hex(c.unicode(), 4);
            out += "<" + hex(ids.at(i), 4) + "> <" + unicode + ">\n";
        }
        out += "endbfchar\n";
    }
    return out + "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
}

void fixToUnicode(QPDF &out, const QHash<QByteArray, QMap<quint32, uint>> &fonts)
{
    for (QPDFObjectHandle &object : out.getAllObjects()) {
        if (!object.isDictionary() || !object.getKey("/Subtype").isNameAndEquals("/Type0"))
            continue;
        const QPDFObjectHandle base = object.getKey("/BaseFont");
        if (!base.isName()) continue;
        const QByteArray name = QByteArray::fromStdString(base.getName().substr(1));
        const auto glyphs = fonts.constFind(name);
        if (glyphs == fonts.cend()) continue;
        object.replaceKey("/ToUnicode", QPDFObjectHandle::newStream(&out, toUnicodeMap(*glyphs)));
    }
}

// PDFium embeds every font it loads as TrueType. A font with CFF outlines,
// an OpenType file starting "OTTO", has to be declared as one, or other
// readers refuse or misread it.
void fixOpenTypeFonts(QPDF &out, const QHash<QByteArray, QMap<quint32, uint>> &fonts)
{
    for (QPDFObjectHandle &object : out.getAllObjects()) {
        if (!object.isDictionary() || !object.getKey("/Subtype").isNameAndEquals("/Type0"))
            continue;
        const QPDFObjectHandle base = object.getKey("/BaseFont");
        if (!base.isName() || !fonts.contains(QByteArray::fromStdString(base.getName().substr(1))))
            continue;
        QPDFObjectHandle descendants = object.getKey("/DescendantFonts");
        if (!descendants.isArray() || descendants.getArrayNItems() < 1) continue;
        QPDFObjectHandle cid = descendants.getArrayItem(0);
        QPDFObjectHandle descriptor = cid.isDictionary() ? cid.getKey("/FontDescriptor")
                                                         : QPDFObjectHandle::newNull();
        if (!descriptor.isDictionary()) continue;
        QPDFObjectHandle file = descriptor.getKey("/FontFile2");
        if (!file.isStream()) continue;
        const std::shared_ptr<Buffer> data = file.getStreamData(qpdf_dl_generalized);
        if (!data || data->getSize() < 4 || std::memcmp(data->getBuffer(), "OTTO", 4) != 0)
            continue;
        file.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/OpenType"));
        descriptor.removeKey("/FontFile2");
        descriptor.replaceKey("/FontFile3", file);
        // A CFF that is not CID-keyed takes the CIDs as glyph indices.
        cid.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType0"));
        cid.removeKey("/CIDToGIDMap");
    }
}

// PDFium opens the stream it appends by undoing the matrix it found the
// content before it leaving set. That content is wrapped in q and Q here, so
// there is nothing left to undo.
QPDFObjectHandle fromInitialState(QPDF &out, QPDFObjectHandle appended)
{
    const std::shared_ptr<Buffer> data = appended.getStreamData(qpdf_dl_generalized);
    const std::string text(reinterpret_cast<const char *>(data->getBuffer()), data->getSize());
    static const std::regex undo(R"(^\s*q\s+(?:[-+.0-9]+\s+){6}cm\s)");
    std::smatch found;
    if (!std::regex_search(text, found, undo)) return appended;
    return QPDFObjectHandle::newStream(&out, "q\n" + text.substr(size_t(found.length(0))));
}

bool restorePage(QPDF &out, QPDFPageObjectHelper &source, QPDFPageObjectHelper &target,
                 const OriginalContent::Page &page)
{
    QPDFObjectHandle resources = source.getAttribute("/Resources", false);
    Pl_Buffer buffer("original");
    ObjectFilter filter(resources, &page);
    source.filterContents(&filter, &buffer);

    std::vector<QPDFObjectHandle> written = target.getPageContents();
    if (page.appended && written.empty()) return false;

    // What the original leaves set, a matrix or text state such as an OCR
    // layer's scaling and invisible mode, must not reach the text appended
    // after it: PDFium drew that from the initial state.
    std::string original = buffer.getString();
    if (page.appended) {
        original = "q\n" + original + "\n";
        for (int i = 0; i < filter.openSaves(); ++i) original += "Q\n";
        original += "Q\nBT 0 Tc 0 Tw 100 Tz 0 TL 0 Tr 0 Ts ET\n";
    }
    QPDFObjectHandle contents = QPDFObjectHandle::newArray();
    contents.appendItem(QPDFObjectHandle::newStream(&out, original));
    if (page.appended) contents.appendItem(fromInitialState(out, written.back()));
    target.getObjectHandle().replaceKey("/Contents", contents);

    QPDFObjectHandle targetResources = target.getAttribute("/Resources", true);
    if (!targetResources.isDictionary()) {
        targetResources = QPDFObjectHandle::newDictionary();
        target.getObjectHandle().replaceKey("/Resources", targetResources);
    }
    if (resources.isDictionary()) addMissingResources(out, resources, targetResources);
    return true;
}

}
#endif

QHash<int, OriginalContent::Scan> OriginalContent::scan(const QString &source,
                                                        const QList<int> &pages)
{
    QHash<int, Scan> out;
    if (pages.isEmpty()) return out;
#ifdef HAVE_QPDF
    try {
        QPDF src;
        const std::string password = PdfPwStore::forQpdf(source);
        src.processFile(source.toLocal8Bit().constData(),
                        password.empty() ? nullptr : password.c_str());
        std::vector<QPDFPageObjectHelper> srcPages = QPDFPageDocumentHelper(src).getAllPages();
        for (const int index : pages) {
            if (index < 0 || index >= int(srcPages.size())) continue;
            QPDFPageObjectHelper &page = srcPages[size_t(index)];
            ObjectFilter filter(page.getAttribute("/Resources", false), nullptr);
            Pl_Discard discard;
            page.filterContents(&filter, &discard);
            out.insert(index, filter.scanned());
        }
    } catch (const std::exception &ex) {
        qWarning() << "[OriginalContent] konnte die Seiten nicht lesen:" << ex.what();
        out.clear();
    }
#else
    Q_UNUSED(source)
#endif
    return out;
}

bool OriginalContent::restore(const QString &source, const WriteGrant &target,
                              const QHash<int, Page> &pages,
                              const QHash<QByteArray, QMap<quint32, uint>> &toUnicode)
{
    const QString output = target.path();
    if (pages.isEmpty() && toUnicode.isEmpty()) return true;
#ifdef HAVE_QPDF
    const QString temp = output + QStringLiteral(".original");
    try {
        QPDF src;
        const std::string password = PdfPwStore::forQpdf(source);
        src.processFile(source.toLocal8Bit().constData(),
                        password.empty() ? nullptr : password.c_str());
        QPDF out;
        out.processFile(output.toLocal8Bit().constData());
        std::vector<QPDFPageObjectHelper> srcPages = QPDFPageDocumentHelper(src).getAllPages();
        std::vector<QPDFPageObjectHelper> outPages = QPDFPageDocumentHelper(out).getAllPages();

        for (auto it = pages.cbegin(); it != pages.cend(); ++it) {
            const int index = it.key();
            if (index < 0 || index >= int(srcPages.size()) || index >= int(outPages.size()))
                continue;
            if (!restorePage(out, srcPages[size_t(index)], outPages[size_t(index)], *it))
                qWarning() << "[OriginalContent] Seite" << index << "bleibt neu erzeugt";
        }

        fixToUnicode(out, toUnicode);
        fixOpenTypeFonts(out, toUnicode);

        QPDFWriter writer(out, temp.toLocal8Bit().constData());
        writer.write();
    } catch (const std::exception &ex) {
        qWarning() << "[OriginalContent] konnte den Originalinhalt nicht zurückschreiben:"
                   << ex.what();
        QFile::remove(temp);
        return false;
    }
    QFile::remove(output);
    return QFile::rename(temp, output);
#else
    Q_UNUSED(source)
    Q_UNUSED(output)
    Q_UNUSED(toUnicode)
    return pages.isEmpty();
#endif
}
