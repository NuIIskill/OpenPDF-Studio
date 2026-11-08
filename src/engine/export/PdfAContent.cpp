#include "engine/export/PdfAContent.hpp"

#ifdef HAVE_QPDF

#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <QDebug>

#include <vector>

namespace {

using PdfAContent::Use;

bool nameIs(QPDFObjectHandle object, const char *name)
{
    return object.isName() && object.getName() == name;
}

bool isCmyk(QPDFObjectHandle space, QPDFObjectHandle resources, int depth = 0)
{
    if (depth > 8) return false;
    if (space.isName()) {
        const std::string name = space.getName();
        if (name == "/DeviceCMYK" || name == "/CMYK") return true;
        QPDFObjectHandle named = resources.isDictionary() ? resources.getKey("/ColorSpace")
                                                          : QPDFObjectHandle::newNull();
        named = named.isDictionary() ? named.getKey(name) : QPDFObjectHandle::newNull();
        return !named.isNull() && isCmyk(named, resources, depth + 1);
    }
    if (!space.isArray() || space.getArrayNItems() < 2) return false;
    const QPDFObjectHandle family = space.getArrayItem(0);
    if (nameIs(family, "/Indexed") || nameIs(family, "/I") || nameIs(family, "/Pattern"))
        return isCmyk(space.getArrayItem(1), resources, depth + 1);
    if ((nameIs(family, "/Separation") || nameIs(family, "/DeviceN")) && space.getArrayNItems() > 2)
        return isCmyk(space.getArrayItem(2), resources, depth + 1);
    return false;
}

struct Context {
    QPDF                 *pdf;
    Use                  *use;
    std::set<QPDFObjGen> *seen;
    int                   page;
};

struct TextState {
    QPDFObjectHandle font;
    int              render { 0 };
};

void walk(Context &ctx, QPDFObjectHandle stream, QPDFObjectHandle inherited, int depth,
          const TextState &state);

class Walker : public QPDFObjectHandle::ParserCallbacks
{
public:
    Walker(Context &ctx, QPDFObjectHandle resources, int depth, const TextState &state)
        : m_ctx(ctx), m_resources(resources), m_depth(depth), m_state(state) {}

    void handleObject(QPDFObjectHandle object) override
    {
        if (!object.isOperator()) {
            m_operands.push_back(object);
            return;
        }
        const std::string op = object.getOperatorValue();
        if (op == "q") {
            m_stack.push_back(m_state);
        } else if (op == "Q") {
            if (!m_stack.empty()) {
                m_state = m_stack.back();
                m_stack.pop_back();
            }
        } else if (op == "Tf") {
            selectFont();
        } else if (op == "Tr") {
            if (!m_operands.empty() && m_operands.front().isInteger())
                m_state.render = int(m_operands.front().getIntValue());
        } else if (op == "Tj" || op == "'") {
            if (!m_operands.empty()) show(m_operands.back());
        } else if (op == "\"") {
            if (m_operands.size() == 3) show(m_operands.back());
        } else if (op == "TJ") {
            if (!m_operands.empty() && m_operands.front().isArray())
                for (const QPDFObjectHandle &item : m_operands.front().getArrayAsVector())
                    show(item);
        } else if (op == "k" || op == "K") {
            m_ctx.use->cmykPages.insert(m_ctx.page);
        } else if (op == "cs" || op == "CS") {
            if (!m_operands.empty() && isCmyk(m_operands.front(), m_resources))
                m_ctx.use->cmykPages.insert(m_ctx.page);
        } else if (op == "sh") {
            shading(named(0, "/Shading"));
        } else if (op == "scn" || op == "SCN") {
            if (!m_operands.empty() && m_operands.back().isName())
                pattern(named(m_operands.size() - 1, "/Pattern"));
        } else if (op == "Do") {
            xobject(named(0, "/XObject"));
        } else if (op == "ID") {
            inlineImage();
        }
        m_operands.clear();
    }

    void handleEOF() override {}

private:
    QPDFObjectHandle named(size_t index, const char *category)
    {
        if (index >= m_operands.size() || !m_operands.at(index).isName()
                || !m_resources.isDictionary())
            return QPDFObjectHandle::newNull();
        QPDFObjectHandle dict = m_resources.getKey(category);
        return dict.isDictionary() ? dict.getKey(m_operands.at(index).getName())
                                   : QPDFObjectHandle::newNull();
    }

    void selectFont()
    {
        m_state.font = QPDFObjectHandle::newNull();
        if (m_operands.empty() || !m_operands.front().isName() || !m_resources.isDictionary())
            return;
        QPDFObjectHandle fonts = m_resources.getKey("/Font");
        if (!fonts.isDictionary()) return;
        const std::string key = m_operands.front().getName();
        QPDFObjectHandle font = fonts.getKey(key);
        if (!font.isDictionary()) return;
        // Fonts are told apart by object, so a direct one gets an object of its own.
        if (!font.isIndirect()) {
            font = m_ctx.pdf->makeIndirectObject(font);
            fonts.replaceKey(key, font);
        }
        m_state.font = font;
        if (nameIs(font.getKey("/Subtype"), "/Type3")) type3(font);
    }

    void show(QPDFObjectHandle text)
    {
        if (!text.isString() || !m_state.font.isDictionary()) return;
        PdfAContent::FontUse &use = m_ctx.use->fonts[m_state.font.getObjGen()];
        use.font = m_state.font;
        if (m_state.render == 3) return;
        use.visible = true;
        if (nameIs(m_state.font.getKey("/Subtype"), "/Type0")) use.strings.insert(text.getStringValue());
        else for (const unsigned char c : text.getStringValue()) use.codes.insert(c);
    }

    void type3(QPDFObjectHandle font)
    {
        QPDFObjectHandle procs = font.getKey("/CharProcs");
        if (!procs.isDictionary()) return;
        QPDFObjectHandle own = font.getKey("/Resources");
        for (const std::string &key : procs.getKeys())
            walk(m_ctx, procs.getKey(key), own.isDictionary() ? own : m_resources, m_depth + 1,
                 {});
    }

    void shading(QPDFObjectHandle shading)
    {
        QPDFObjectHandle dict = shading.isStream() ? shading.getDict() : shading;
        if (dict.isDictionary() && isCmyk(dict.getKey("/ColorSpace"), m_resources))
            m_ctx.use->cmykPages.insert(m_ctx.page);
    }

    void pattern(QPDFObjectHandle pattern)
    {
        QPDFObjectHandle dict = pattern.isStream() ? pattern.getDict() : pattern;
        if (!dict.isDictionary()) return;
        if (pattern.isStream()) walk(m_ctx, pattern, m_resources, m_depth + 1, {});
        else shading(dict.getKey("/Shading"));
    }

    void xobject(QPDFObjectHandle xobject)
    {
        if (!xobject.isStream()) return;
        QPDFObjectHandle dict = xobject.getDict();
        if (nameIs(dict.getKey("/Subtype"), "/Image")) {
            if (isCmyk(dict.getKey("/ColorSpace"), QPDFObjectHandle::newNull()))
                m_ctx.use->cmykPages.insert(m_ctx.page);
        } else if (nameIs(dict.getKey("/Subtype"), "/Form")) {
            walk(m_ctx, xobject, m_resources, m_depth + 1, m_state);
        }
    }

    void inlineImage()
    {
        for (size_t i = 0; i + 1 < m_operands.size(); i += 2)
            if ((nameIs(m_operands[i], "/CS") || nameIs(m_operands[i], "/ColorSpace"))
                    && isCmyk(m_operands[i + 1], m_resources))
                m_ctx.use->cmykPages.insert(m_ctx.page);
    }

    Context                      &m_ctx;
    QPDFObjectHandle              m_resources;
    int                           m_depth;
    TextState                     m_state;
    std::vector<TextState>        m_stack;
    std::vector<QPDFObjectHandle> m_operands;
};

void walk(Context &ctx, QPDFObjectHandle stream, QPDFObjectHandle inherited, int depth,
          const TextState &state)
{
    if (!stream.isStream() || depth > 16 || !ctx.seen->insert(stream.getObjGen()).second) return;
    QPDFObjectHandle dict = stream.getDict();
    QPDFObjectHandle own = dict.getKey("/Resources");
    QPDFObjectHandle resources = own.isDictionary() ? own : inherited;
    QPDFObjectHandle group = dict.getKey("/Group");
    if (group.isDictionary() && isCmyk(group.getKey("/CS"), resources))
        ctx.use->cmykPages.insert(ctx.page);
    Walker walker(ctx, resources, depth, state);
    try {
        QPDFObjectHandle::parseContentStream(stream, &walker);
    } catch (const std::exception &ex) {
        qWarning() << "[PDF/A] cannot read a content stream:" << ex.what();
    }
}

}

PdfAContent::Use PdfAContent::scan(QPDF &pdf)
{
    Use use;
    std::set<QPDFObjGen> seen;
    std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(pdf).getAllPages();
    for (size_t i = 0; i < pages.size(); ++i) {
        Context ctx { &pdf, &use, &seen, int(i) + 1 };
        QPDFPageObjectHelper &page = pages[i];
        QPDFObjectHandle resources = page.getAttribute("/Resources", false);
        QPDFObjectHandle group = page.getObjectHandle().getKey("/Group");
        if (group.isDictionary() && isCmyk(group.getKey("/CS"), resources))
            use.cmykPages.insert(ctx.page);
        Walker walker(ctx, resources, 0, {});
        try {
            page.parseContents(&walker);
        } catch (const std::exception &ex) {
            qWarning() << "[PDF/A] cannot read page" << ctx.page << ex.what();
        }

        QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
        if (!annots.isArray()) continue;
        for (const QPDFObjectHandle &annot : annots.getArrayAsVector()) {
            QPDFObjectHandle ap = annot.isDictionary() ? annot.getKey("/AP")
                                                       : QPDFObjectHandle::newNull();
            if (!ap.isDictionary()) continue;
            for (const std::string &key : ap.getKeys()) {
                QPDFObjectHandle look = ap.getKey(key);
                if (look.isStream()) walk(ctx, look, QPDFObjectHandle::newNull(), 1, {});
                else if (look.isDictionary())
                    for (const std::string &state : look.getKeys())
                        walk(ctx, look.getKey(state), QPDFObjectHandle::newNull(), 1, {});
            }
        }
    }
    return use;
}

#endif
