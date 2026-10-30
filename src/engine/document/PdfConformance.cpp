#include "engine/document/PdfConformance.hpp"

#include <QDebug>
#include <QFile>

#ifdef HAVE_QPDF
#  include <qpdf/Pl_Discard.hh>
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFExc.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFWriter.hh>

#  include <memory>
#  include <set>
#endif

QString PdfConformance::Version::text() const
{
    QString out = QStringLiteral("%1.%2").arg(major).arg(minor);
    if (extension > 0) out += QStringLiteral(" (extension level %1)").arg(extension);
    return out;
}

bool PdfConformance::Version::operator<(const Version &other) const
{
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    return extension < other.extension;
}

#ifdef HAVE_QPDF
namespace {

using Version = PdfConformance::Version;

struct Need {
    Version      version;
    QStringList *why { nullptr };

    void at(const Version &v, const char *feature)
    {
        if (version < v) version = v;
        const QString reason = QStringLiteral("%1 needs PDF %2").arg(QString::fromLatin1(feature), v.text());
        if (why && !why->contains(reason)) why->append(reason);
    }
};

bool nameIs(QPDFObjectHandle object, const char *name)
{
    return object.isName() && object.getName() == name;
}

bool hasFilter(QPDFObjectHandle dict, const char *name)
{
    QPDFObjectHandle filter = dict.getKey("/Filter");
    if (nameIs(filter, name)) return true;
    if (!filter.isArray()) return false;
    for (int i = 0; i < filter.getArrayNItems(); ++i)
        if (nameIs(filter.getArrayItem(i), name)) return true;
    return false;
}

// What the features one dictionary uses ask of the version, by ISO 32000.
void look(QPDFObjectHandle dict, Need &need)
{
    QPDFObjectHandle type = dict.getKey("/Type");
    if (nameIs(type, "/ObjStm"))  need.at({ 1, 5 }, "Object streams");
    if (nameIs(type, "/XRef"))    need.at({ 1, 5 }, "Cross-reference streams");

    QPDFObjectHandle smask = dict.getKey("/SMask");
    if (!smask.isNull() && !nameIs(smask, "/None")) need.at({ 1, 4 }, "Soft masks");
    for (const char *alpha : { "/CA", "/ca" }) {
        QPDFObjectHandle value = dict.getKey(alpha);
        if (value.isNumber() && value.getNumericValue() < 1.0) need.at({ 1, 4 }, "Transparency");
    }
    QPDFObjectHandle blend = dict.getKey("/BM");
    if (blend.isName() && !nameIs(blend, "/Normal") && !nameIs(blend, "/Compatible"))
        need.at({ 1, 4 }, "Blend modes");
    if (nameIs(dict.getKey("/S"), "/Transparency")) need.at({ 1, 4 }, "Transparency groups");

    QPDFObjectHandle subtype = dict.getKey("/Subtype");
    if (nameIs(subtype, "/Screen"))     need.at({ 1, 5 }, "Screen annotations");
    if (nameIs(subtype, "/RichMedia"))  need.at({ 1, 7, 3 }, "Rich media");
    if (nameIs(subtype, "/3D"))         need.at({ 1, 6 }, "3D annotations");
    if (nameIs(subtype, "/Redact"))     need.at({ 1, 7 }, "Redaction annotations");
    if (nameIs(subtype, "/Watermark"))  need.at({ 1, 6 }, "Watermark annotations");
    if (nameIs(subtype, "/Projection")) need.at({ 2, 0 }, "Projection annotations");
    if (nameIs(subtype, "/OpenType"))   need.at({ 1, 6 }, "OpenType font files");

    QPDFObjectHandle action = dict.getKey("/S");
    if (nameIs(action, "/Rendition"))        need.at({ 1, 5 }, "Rendition actions");
    if (nameIs(action, "/RichMediaExecute")) need.at({ 1, 7, 3 }, "Rich media actions");
    if (nameIs(action, "/GoTo3DView"))       need.at({ 1, 6 }, "3D view actions");

    if (hasFilter(dict, "/JPXDecode"))   need.at({ 1, 5 }, "JPEG 2000 images");
    if (hasFilter(dict, "/JBIG2Decode")) need.at({ 1, 4 }, "JBIG2 images");
    if (hasFilter(dict, "/Crypt"))       need.at({ 1, 5 }, "Crypt filters");

    if (dict.hasKey("/OCProperties") || dict.hasKey("/OC")) need.at({ 1, 5 }, "Optional content");
    if (dict.hasKey("/UserUnit"))  need.at({ 1, 6 }, "User units");
    if (dict.hasKey("/AF"))        need.at({ 2, 0 }, "Associated files");
}

Version headerOf(QPDF &pdf)
{
    Version v;
    const std::string text = pdf.getPDFVersion();
    const size_t dot = text.find('.');
    try {
        v.major = std::stoi(text.substr(0, dot));
        v.minor = dot == std::string::npos ? 0 : std::stoi(text.substr(dot + 1));
    } catch (...) {
        v = { 1, 0 };
    }
    v.extension = pdf.getExtensionLevel();
    return v;
}

Version neededBy(QPDF &pdf, QStringList *why)
{
    Need need { { 1, 0 }, why };
    for (QPDFObjectHandle object : pdf.getAllObjects()) {
        if (object.isStream()) look(object.getDict(), need);
        else if (object.isDictionary()) look(object, need);
    }
    QPDFObjectHandle encrypt = pdf.getTrailer().getKey("/Encrypt");
    if (encrypt.isDictionary()) {
        const int v = encrypt.getKey("/V").isInteger() ? int(encrypt.getKey("/V").getIntValue()) : 0;
        if (v >= 5) need.at({ 1, 7, 8 }, "AES-256 encryption");
        else if (v == 4) need.at({ 1, 5 }, "Crypt filter encryption");
    }
    return need.version;
}

void open(QPDF &pdf, const QString &path, const std::string &password)
{
    pdf.setSuppressWarnings(true);
    pdf.processFile(path.toLocal8Bit().constData(), password.empty() ? nullptr : password.c_str());
}

bool isNumberArray(QPDFObjectHandle object, int size)
{
    if (!object.isArray() || object.getArrayNItems() != size) return false;
    for (int i = 0; i < size; ++i)
        if (!object.getArrayItem(i).isNumber()) return false;
    return true;
}

// Every resource a content stream names must be in its resources, the
// forms it draws are read the same way.
class ContentCheck : public QPDFObjectHandle::ParserCallbacks
{
public:
    ContentCheck(QPDFObjectHandle resources, const QString &where, QStringList *found,
                 std::set<QPDFObjGen> *seen, int depth)
        : m_resources(resources), m_where(where), m_found(found), m_seen(seen), m_depth(depth) {}

    void handleObject(QPDFObjectHandle object) override
    {
        if (!object.isOperator()) {
            m_operands.push_back(object);
            return;
        }
        const std::string op = object.getOperatorValue();
        if (op == "Tf") need(0, "/Font");
        else if (op == "Do") drawn(need(0, "/XObject"));
        else if (op == "gs") need(0, "/ExtGState");
        else if (op == "sh") need(0, "/Shading");
        else if (op == "cs" || op == "CS") colourSpace();
        else if (op == "scn" || op == "SCN") pattern();
        else if (op == "BDC" || op == "DP") properties();
        m_operands.clear();
    }

    void handleEOF() override {}

private:
    QPDFObjectHandle need(size_t index, const char *category)
    {
        if (index >= m_operands.size() || !m_operands.at(index).isName()) return {};
        const std::string name = m_operands.at(index).getName();
        QPDFObjectHandle dict = m_resources.isDictionary() ? m_resources.getKey(category)
                                                          : QPDFObjectHandle::newNull();
        QPDFObjectHandle found = dict.isDictionary() ? dict.getKey(name) : QPDFObjectHandle::newNull();
        if (found.isNull())
            m_found->append(QStringLiteral("%1: %2 %3 is not in the resources")
                                .arg(m_where, QString::fromLatin1(category),
                                     QString::fromStdString(name)));
        return found;
    }

    void colourSpace()
    {
        static const std::set<std::string> device { "/DeviceGray", "/DeviceRGB", "/DeviceCMYK",
                                                    "/Pattern" };
        if (!m_operands.empty() && m_operands.front().isName()
                && !device.count(m_operands.front().getName()))
            need(0, "/ColorSpace");
    }

    void pattern()
    {
        if (!m_operands.empty() && m_operands.back().isName())
            need(m_operands.size() - 1, "/Pattern");
    }

    void properties()
    {
        if (m_operands.size() >= 2 && m_operands.at(1).isName()) need(1, "/Properties");
    }

    void drawn(QPDFObjectHandle xobject)
    {
        if (!xobject.isStream() || m_depth > 12) return;
        QPDFObjectHandle dict = xobject.getDict();
        if (!dict.getKey("/Subtype").isName() || dict.getKey("/Subtype").getName() != "/Form") return;
        if (!m_seen->insert(xobject.getObjGen()).second) return;
        QPDFObjectHandle own = dict.getKey("/Resources");
        ContentCheck inner(own.isDictionary() ? own : m_resources, m_where + QStringLiteral(" > form"),
                           m_found, m_seen, m_depth + 1);
        QPDFObjectHandle::parseContentStream(xobject, &inner);
    }

    QPDFObjectHandle              m_resources;
    QString                       m_where;
    QStringList                  *m_found;
    std::set<QPDFObjGen>         *m_seen;
    int                           m_depth;
    std::vector<QPDFObjectHandle> m_operands;
};

void checkAnnotations(QPDFPageObjectHelper &page, const QString &where, QStringList *found,
                      std::set<QPDFObjGen> *seen)
{
    QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
    if (annots.isNull()) return;
    if (!annots.isArray()) {
        found->append(where + QStringLiteral(": /Annots is not an array"));
        return;
    }
    for (int i = 0; i < annots.getArrayNItems(); ++i) {
        QPDFObjectHandle annot = annots.getArrayItem(i);
        const QString at = QStringLiteral("%1, annotation %2").arg(where).arg(i + 1);
        if (!annot.isDictionary()) { found->append(at + QStringLiteral(": not a dictionary")); continue; }
        if (!annot.getKey("/Subtype").isName()) found->append(at + QStringLiteral(": no /Subtype"));
        if (!isNumberArray(annot.getKey("/Rect"), 4)) found->append(at + QStringLiteral(": no /Rect"));
        QPDFObjectHandle normal = annot.getKey("/AP").isDictionary()
                                ? annot.getKey("/AP").getKey("/N") : QPDFObjectHandle::newNull();
        std::vector<QPDFObjectHandle> streams;
        if (normal.isStream()) streams.push_back(normal);
        else if (normal.isDictionary())
            for (const std::string &key : normal.getKeys())
                if (normal.getKey(key).isStream()) streams.push_back(normal.getKey(key));
        for (QPDFObjectHandle stream : streams) {
            if (!seen->insert(stream.getObjGen()).second) continue;
            ContentCheck content(stream.getDict().getKey("/Resources"), at + QStringLiteral(" appearance"),
                                 found, seen, 1);
            QPDFObjectHandle::parseContentStream(stream, &content);
        }
    }
}

}
#endif

bool PdfConformance::available()
{
#ifdef HAVE_QPDF
    return true;
#else
    return false;
#endif
}

PdfConformance::Version PdfConformance::needed(const QString &path, const std::string &password,
                                               QStringList *why)
{
#ifdef HAVE_QPDF
    try {
        QPDF pdf;
        open(pdf, path, password);
        return neededBy(pdf, why);
    } catch (const std::exception &ex) {
        qWarning() << "[Conformance] cannot read" << path << ex.what();
    }
#else
    Q_UNUSED(path) Q_UNUSED(password) Q_UNUSED(why)
#endif
    return {};
}

// The header is raised to what the features in the file need, never lowered.
bool PdfConformance::raiseVersion(const WriteGrant &file, const std::string &password)
{
#ifdef HAVE_QPDF
    const WriteGrant raised = file.beside(QStringLiteral(".version"));
    try {
        QPDF pdf;
        open(pdf, file.path(), password);
        const Version header = headerOf(pdf);
        QStringList why;
        const Version need = neededBy(pdf, &why);
        if (!(header < need)) return true;
        qInfo() << "[Conformance] header" << header.text() << "raised to" << need.text() << why;
        QPDFWriter writer(pdf, raised.path().toLocal8Bit().constData());
        writer.setMinimumPDFVersion(QStringLiteral("%1.%2").arg(need.major).arg(need.minor).toStdString(),
                                    need.extension);
        writer.write();
    } catch (const std::exception &ex) {
        qWarning() << "[Conformance] cannot raise the version of" << file.path() << ex.what();
        QFile::remove(raised.path());
        return false;
    }
    QFile::remove(file.path());
    return QFile::rename(raised.path(), file.path());
#else
    Q_UNUSED(file) Q_UNUSED(password)
    return true;
#endif
}

QStringList PdfConformance::problems(const QString &path, const std::string &password)
{
    QStringList found;
#ifdef HAVE_QPDF
    try {
        QPDF pdf;
        open(pdf, path, password);
        const Version header = headerOf(pdf);
        QStringList why;
        const Version need = neededBy(pdf, &why);
        if (header < need)
            found << QStringLiteral("header says PDF %1, the content needs %2: %3")
                         .arg(header.text(), need.text(), why.join(QStringLiteral(", ")));

        QPDFObjectHandle root = pdf.getRoot();
        if (!nameIs(root.getKey("/Type"), "/Catalog")) found << QStringLiteral("catalog without /Type /Catalog");
        std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(pdf).getAllPages();
        if (pages.empty()) found << QStringLiteral("no pages");
        QPDFObjectHandle count = root.getKey("/Pages").getKey("/Count");
        if (!count.isInteger() || count.getIntValue() != qint64(pages.size()))
            found << QStringLiteral("page tree /Count does not match its pages");
        std::set<QPDFObjGen> seen;
        for (size_t i = 0; i < pages.size(); ++i) {
            QPDFPageObjectHelper &page = pages[i];
            const QString where = QStringLiteral("page %1").arg(i + 1);
            QPDFObjectHandle box = page.getAttribute("/MediaBox", false);
            if (!isNumberArray(box, 4)
                    || box.getArrayItem(0).getNumericValue() == box.getArrayItem(2).getNumericValue()
                    || box.getArrayItem(1).getNumericValue() == box.getArrayItem(3).getNumericValue())
                found << where + QStringLiteral(": no usable /MediaBox");
            ContentCheck content(page.getAttribute("/Resources", false), where, &found, &seen, 0);
            page.parseContents(&content);
            checkAnnotations(page, where, &found, &seen);
        }

        // Writing every object with its streams decoded reads all of them, as qpdf --check does.
        Pl_Discard discard;
        QPDFWriter writer(pdf);
        writer.setOutputPipeline(&discard);
        writer.setDecodeLevel(qpdf_dl_generalized);
        writer.setCompressStreams(false);
        writer.write();
        // Damage qpdf had to work around; its positions differ from file to file.
        if (!pdf.getWarnings().empty()) found << QStringLiteral("damaged file structure");
    } catch (const std::exception &ex) {
        found << QString::fromStdString(ex.what());
    }
#else
    Q_UNUSED(path) Q_UNUSED(password)
#endif
    found.removeDuplicates();
    return found;
}
