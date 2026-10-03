#include "engine/document/Type3Text.hpp"

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

class Type3Filter : public QPDFObjectHandle::TokenFilter
{
public:
    Type3Filter(const std::set<std::string> &type3Fonts, const Type3Text::Kept &kept,
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
        else handleOperator(op, token);
        m_operands.clear();
    }

private:
    std::vector<Token> values() const
    {
        std::vector<Token> out;
        for (const Token &t : m_operands)
            if (t.getType() != QPDFTokenizer::tt_space) out.push_back(t);
        return out;
    }

    std::string text(const std::vector<Token> &operands, const std::string &op) const
    {
        std::string out;
        for (const Token &t : operands) out += t.getRawValue() + " ";
        return out + op + "\n";
    }

    void emit(const std::string &op) { write(text(values(), op)); }

    void emitRenamed(const std::string &op, const char *category, size_t index)
    {
        std::vector<Token> v = values();
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
            for (const Token &t : v)
                if (t.getType() == QPDFTokenizer::tt_string) return true;
            return false;
        }
        return !v.empty() && v.back().getType() == QPDFTokenizer::tt_string
            && !v.back().getValue().empty();
    }

    void handleOperator(const std::string &op, const Token &)
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
        if (op == "gs") { emitRenamed(op, "/ExtGState", 0); return; }
        if (op == "cs" || op == "CS") { emitRenamed(op, "/ColorSpace", 0); return; }
        if (op == "scn" || op == "SCN") {
            emitRenamed(op, "/Pattern", values().empty() ? 0 : values().size() - 1);
            return;
        }
        if (op == "Tf") {
            const std::vector<Token> v = values();
            m_font = v.empty() ? std::string() : v.front().getValue();
            if (m_type3.count(m_font)) emitRenamed(op, "/Font", 0);
            return;
        }
        if (op != "Tj" && op != "TJ" && op != "'" && op != "\"") {
            emit(op);
            return;
        }

        bool keep = false;
        if (m_type3.count(m_font) && showsText(op)) {
            keep = m_keepAll || m_kept.indices.contains(m_index);
            ++m_index;
        }
        if (keep) {
            emit(op);
        } else if (op == "'") {
            write("T*\n");
        } else if (op == "\"") {
            const std::vector<Token> v = values();
            if (v.size() >= 2)
                write(v.at(0).getRawValue() + " Tw " + v.at(1).getRawValue() + " Tc ");
            write("T*\n");
        }
    }

    const std::set<std::string> &m_type3;
    const Type3Text::Kept       &m_kept;
    const bool                   m_keepAll;
    Resources                   &m_resources;
    std::vector<Token>           m_operands;
    std::string                  m_font;
    std::string                  m_path;
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
                 QPDFPageObjectHelper &target, const Type3Text::Kept &kept)
{
    QPDFObjectHandle resources = source.getAttribute("/Resources", false);
    const std::set<std::string> type3 = type3FontNames(resources);
    if (type3.empty()) return;

    QPDFObjectHandle targetResources = target.getAttribute("/Resources", true);
    if (!targetResources.isDictionary()) {
        targetResources = QPDFObjectHandle::newDictionary();
        target.getObjectHandle().replaceKey("/Resources", targetResources);
    }

    for (const bool keepAll : { false, true }) {
        Resources used(resources, targetResources);
        Pl_Buffer buffer("type3");
        Type3Filter filter(type3, kept, keepAll, used);
        source.filterContents(&filter, &buffer);
        if (!keepAll && filter.shows() != kept.total) {
            qWarning() << "[Type3Text]" << filter.shows() << "Type3 text operations,"
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

bool Type3Text::restore(const QString &source, const QString &output,
                        const QHash<int, Kept> &pages)
{
    if (pages.isEmpty()) return true;
#ifdef HAVE_QPDF
    const QString temp = output + QStringLiteral(".type3");
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
        qWarning() << "[Type3Text] konnte Type3-Text nicht wiederherstellen:" << ex.what();
        QFile::remove(temp);
        return false;
    }
    QFile::remove(output);
    return QFile::rename(temp, output);
#else
    Q_UNUSED(source)
    Q_UNUSED(output)
    qWarning() << "[Type3Text] ohne qpdf geht Type3-Text beim Speichern verloren";
    return false;
#endif
}
