#include "engine/document/KeptText.hpp"

#include "app/PdfPwStore.hpp"

#include <QDebug>
#include <QFile>

#ifdef HAVE_QPDF
#  include <qpdf/Pl_Buffer.hh>
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFWriter.hh>

#  include <map>
#  include <set>
#  include <string>
#  include <vector>
#endif

#ifdef HAVE_QPDF
namespace {

using Token = QPDFTokenizer::Token;

class Resources
{
public:
    Resources(QPDFObjectHandle source, QPDFObjectHandle target)
        : m_source(source), m_target(target) {}

    bool has(const char *category, const std::string &name) const
    {
        if (!m_source.isDictionary()) return false;
        QPDFObjectHandle dict = m_source.getKey(category);
        return dict.isDictionary() && dict.hasKey(name);
    }

    std::string rename(const char *category, const std::string &name)
    {
        auto &names = m_renamed[category];
        const auto known = names.find(name);
        if (known != names.end()) return known->second;
        QPDFObjectHandle taken = m_target.isDictionary() ? m_target.getKey(category)
                                                         : QPDFObjectHandle::newNull();
        std::string fresh;
        do {
            fresh = "/Opd3R" + std::to_string(++m_counter);
        } while (taken.isDictionary() && taken.hasKey(fresh));
        names[name] = fresh;
        return fresh;
    }

    QPDFObjectHandle used(QPDF &source) const
    {
        QPDFObjectHandle out = QPDFObjectHandle::newDictionary();
        for (const auto &category : m_renamed) {
            QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
            QPDFObjectHandle from = m_source.getKey(category.first);
            for (const auto &name : category.second)
                dict.replaceKey(name.second, from.getKey(name.first));
            out.replaceKey(category.first, dict);
        }
        return source.makeIndirectObject(out);
    }

private:
    QPDFObjectHandle m_source;
    QPDFObjectHandle m_target;
    std::map<std::string, std::map<std::string, std::string>> m_renamed;
    int m_counter { 0 };
};

class KeptFilter : public QPDFObjectHandle::TokenFilter
{
public:
    KeptFilter(const std::set<std::string> &type3Fonts, const KeptText::Kept &kept,
               bool keepAll, Resources &resources)
        : m_type3(type3Fonts), m_kept(kept), m_keepAll(keepAll), m_resources(resources) {}

    int shows() const { return m_index; }

    void handleToken(const Token &token) override
    {
        if (m_inImage) {
            if (token.getType() == QPDFTokenizer::tt_inline_image) m_inImage = false;
            return;
        }
        if (token.getType() == QPDFTokenizer::tt_eof) return;
        if (token.getType() != QPDFTokenizer::tt_word) {
            if (token.getType() != QPDFTokenizer::tt_comment) m_operands.push_back(token);
            return;
        }
        const std::string op = token.getValue();
        if (op == "BI") m_inImage = true;
        else handleOperator(op);
        m_operands.clear();
    }

private:
    struct Spacing { double charSpacing { 0.0 }; double wordSpacing { 0.0 }; };
    struct Saved { Spacing spacing; std::string font; std::string renderMode; };
    struct Pending { std::vector<Token> fontOperands; std::string fontSet; std::string show; };

    std::vector<Token> values() const
    {
        std::vector<Token> out;
        for (const Token &t : m_operands)
            if (t.getType() != QPDFTokenizer::tt_space) out.push_back(t);
        return out;
    }

    static double number(const Token &t)
    {
        try { return std::stod(t.getValue()); } catch (...) { return 0.0; }
    }

    std::string text(const std::vector<Token> &operands, const std::string &op) const
    {
        std::string out;
        for (const Token &t : operands) out += t.getRawValue() + " ";
        return out + op + "\n";
    }

    void emit(const std::string &op) { write(text(values(), op)); }

    void emitRenamed(const std::vector<Token> &operands, const std::string &op,
                     const char *category, size_t index)
    {
        std::vector<Token> v = operands;
        if (index < v.size() && v[index].getType() == QPDFTokenizer::tt_name
                && m_resources.has(category, v[index].getValue()))
            v[index] = Token(QPDFTokenizer::tt_name,
                             m_resources.rename(category, v[index].getValue()));
        std::string out;
        for (const Token &t : v)
            out += (t.getRawValue().empty() ? t.getValue() : t.getRawValue()) + " ";
        write(out + op + "\n");
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

    void handleOperator(const std::string &op)
    {
        static const std::set<std::string> construction {
            "m", "l", "c", "v", "y", "h", "re" };
        static const std::set<std::string> painting {
            "S", "s", "f", "F", "f*", "B", "B*", "b", "b*", "n" };
        static const std::set<std::string> dropped {
            "Do", "sh", "BMC", "BDC", "EMC", "MP", "DP", "d0", "d1" };

        if (construction.count(op)) { m_path += text(values(), op); return; }
        if (op == "W" || op == "W*") { m_path += op + "\n"; m_clip = true; return; }
        if (painting.count(op)) {
            if (m_clip) write(m_path + "n\n");
            m_path.clear();
            m_clip = false;
            return;
        }
        if (dropped.count(op)) return;
        if (op == "q") {
            m_saved.push_back({ m_spacing, m_fontWritten, m_renderMode });
            emit(op);
            return;
        }
        if (op == "Q") {
            if (!m_saved.empty()) {
                m_spacing     = m_saved.back().spacing;
                m_fontWritten = m_saved.back().font;
                m_renderMode  = m_saved.back().renderMode;
                m_saved.pop_back();
            }
            emit(op);
            return;
        }
        if (op == "Tr") {
            const std::vector<Token> v = values();
            if (!v.empty()) m_renderMode = v.back().getRawValue();
            emit(op);
            return;
        }
        if (op == "BT" || op == "ET" || op == "Td" || op == "TD" || op == "Tm" || op == "T*")
            m_pending.clear();
        if (op == "Tc" || op == "Tw") {
            const std::vector<Token> v = values();
            if (!v.empty())
                (op == "Tc" ? m_spacing.charSpacing : m_spacing.wordSpacing) = number(v.back());
            emit(op);
            return;
        }
        if (op == "gs") { emitRenamed(values(), op, "/ExtGState", 0); return; }
        if (op == "cs" || op == "CS") { emitRenamed(values(), op, "/ColorSpace", 0); return; }
        if (op == "scn" || op == "SCN") {
            emitRenamed(values(), op, "/Pattern", values().empty() ? 0 : values().size() - 1);
            return;
        }
        if (op == "Tf") {
            m_fontOperands = values();
            m_font = m_fontOperands.empty() ? std::string() : m_fontOperands.front().getValue();
            m_fontSet = text(m_fontOperands, op);
            return;
        }
        if (op != "Tj" && op != "TJ" && op != "'" && op != "\"") {
            emit(op);
            return;
        }

        if (op == "\"") {
            const std::vector<Token> v = values();
            if (v.size() >= 2) {
                m_spacing.wordSpacing = number(v.at(0));
                m_spacing.charSpacing = number(v.at(1));
            }
        }
        bool keep = false;
        if (showsText(op)) {
            const bool type3 = m_type3.count(m_font) > 0;
            const bool spaced = !qFuzzyIsNull(m_spacing.charSpacing)
                             || !qFuzzyIsNull(m_spacing.wordSpacing);
            if (!m_kept.type3Only || type3) {
                keep = m_keepAll ? (type3 || (spaced && !m_kept.type3Only))
                                 : m_kept.indices.contains(m_index);
                ++m_index;
            }
        }
        if (keep) {
            if (op == "Tj" || op == "TJ") writePending();
            m_pending.clear();
            writeFont(m_fontOperands, m_fontSet);
            emit(op);
            return;
        }
        if (op == "'" || op == "\"") {
            const std::vector<Token> v = values();
            if (op == "\"" && v.size() >= 2)
                write(v.at(0).getRawValue() + " Tw " + v.at(1).getRawValue() + " Tc ");
            write("T*\n");
            m_pending.clear();
            if (!v.empty()) m_pending.push_back({ m_fontOperands, m_fontSet, text({ v.back() }, "Tj") });
            return;
        }
        m_pending.push_back({ m_fontOperands, m_fontSet, text(values(), op) });
    }

    void writeFont(const std::vector<Token> &operands, const std::string &set)
    {
        if (m_fontWritten == set || operands.empty()) return;
        emitRenamed(operands, "Tf", "/Font", 0);
        m_fontWritten = set;
    }

    void writePending()
    {
        // Text left out before a kept show is drawn invisibly, so the kept
        // text starts where the pen stood in the original.
        if (m_pending.empty()) return;
        for (const Pending &p : m_pending) {
            writeFont(p.fontOperands, p.fontSet);
            write("3 Tr\n" + p.show);
        }
        write((m_renderMode.empty() ? std::string("0") : m_renderMode) + " Tr\n");
    }

    const std::set<std::string> &m_type3;
    const KeptText::Kept        &m_kept;
    const bool                   m_keepAll;
    Resources                   &m_resources;
    std::vector<Token>           m_operands;
    std::vector<Token>           m_fontOperands;
    std::string                  m_font;
    std::string                  m_fontSet;
    std::string                  m_fontWritten;
    std::string                  m_path;
    Spacing                      m_spacing;
    std::string                  m_renderMode;
    std::vector<Saved>           m_saved;
    std::vector<Pending>         m_pending;
    bool                         m_clip    { false };
    int                          m_index   { 0 };
    bool                         m_inImage { false };
};

std::set<std::string> type3FontNames(QPDFObjectHandle resources)
{
    std::set<std::string> names;
    if (!resources.isDictionary()) return names;
    QPDFObjectHandle fonts = resources.getKey("/Font");
    if (!fonts.isDictionary()) return names;
    for (const std::string &key : fonts.getKeys()) {
        QPDFObjectHandle font = fonts.getKey(key);
        if (font.isDictionary() && font.getKey("/Subtype").isNameAndEquals("/Type3"))
            names.insert(key);
    }
    return names;
}

void restorePage(QPDF &src, QPDF &out, QPDFPageObjectHelper &source,
                 QPDFPageObjectHelper &target, const KeptText::Kept &kept)
{
    QPDFObjectHandle resources = source.getAttribute("/Resources", false);
    const std::set<std::string> type3 = type3FontNames(resources);
    if (kept.type3Only && type3.empty()) return;

    QPDFObjectHandle targetResources = target.getAttribute("/Resources", true);
    if (!targetResources.isDictionary()) {
        targetResources = QPDFObjectHandle::newDictionary();
        target.getObjectHandle().replaceKey("/Resources", targetResources);
    }

    for (const bool keepAll : { false, true }) {
        Resources used(resources, targetResources);
        Pl_Buffer buffer("kept");
        KeptFilter filter(type3, kept, keepAll, used);
        source.filterContents(&filter, &buffer);
        if (!keepAll && filter.shows() != kept.total) {
            qWarning() << "[KeptText]" << filter.shows() << "text operations,"
                       << "PDFium saw" << kept.total << "- keeping all of them";
            continue;
        }

        QPDFObjectHandle added = out.copyForeignObject(used.used(src));
        for (const std::string &category : added.getKeys()) {
            QPDFObjectHandle into = targetResources.getKey(category);
            if (!into.isDictionary()) {
                into = QPDFObjectHandle::newDictionary();
                targetResources.replaceKey(category, into);
            }
            QPDFObjectHandle entries = added.getKey(category);
            for (const std::string &name : entries.getKeys())
                into.replaceKey(name, entries.getKey(name));
        }
        target.addPageContents(
            QPDFObjectHandle::newStream(&out, "\nq\n" + buffer.getString() + "\nQ\n"), false);
        return;
    }
}

}
#endif

bool KeptText::restore(const QString &source, const QString &output,
                       const QHash<int, Kept> &pages)
{
    if (pages.isEmpty()) return true;
#ifdef HAVE_QPDF
    const QString temp = output + QStringLiteral(".kept");
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
            if (it->indices.isEmpty() || index < 0
                    || index >= int(srcPages.size()) || index >= int(outPages.size()))
                continue;
            restorePage(src, out, srcPages[size_t(index)], outPages[size_t(index)], *it);
        }

        QPDFWriter writer(out, temp.toLocal8Bit().constData());
        writer.write();
    } catch (const std::exception &ex) {
        qWarning() << "[KeptText] konnte den Text nicht wiederherstellen:" << ex.what();
        QFile::remove(temp);
        return false;
    }
    QFile::remove(output);
    return QFile::rename(temp, output);
#else
    Q_UNUSED(source)
    Q_UNUSED(output)
    qWarning() << "[KeptText] ohne qpdf geht Type3-Text beim Speichern verloren";
    return false;
#endif
}
