#include "engine/sign/SignatureVerifier.hpp"
#include "app/TrustedSigners.hpp"
#include "engine/document/DocumentInfo.hpp"

#include <QFile>

#include <QHash>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#endif

#ifdef HAVE_QPDF
namespace {

QString text(const QPDFObjectHandle &obj)
{
    return obj.isString() ? QString::fromStdString(obj.getUTF8Value()) : QString();
}

struct Page {
    int                         index { -1 };
    QPDFObjectHandle::Rectangle box;
    int                         rotate { 0 };
    QPDFObjectHandle            object;
};

using Pages = QHash<QPair<int, int>, Page>;

QPair<int, int> key(const QPDFObjectHandle &obj)
{
    return { obj.getObjectID(), obj.getGeneration() };
}

Pages widgetPages(QPDF &pdf)
{
    Pages pages;
    const std::vector<QPDFPageObjectHelper> all = QPDFPageDocumentHelper(pdf).getAllPages();
    for (int i = 0; i < int(all.size()); ++i) {
        QPDFPageObjectHelper page = all[size_t(i)];
        const QPDFObjectHandle rotation = page.getAttribute("/Rotate", false);
        const int rotate = rotation.isInteger() ? int(((rotation.getIntValue() % 360) + 360) % 360)
                                                : 0;
        const Page entry { i, page.getCropBox().getArrayAsRectangle(), rotate,
                           page.getObjectHandle() };
        const QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
        if (!annots.isArray()) continue;
        for (const QPDFObjectHandle &annot : annots.getArrayAsVector())
            if (annot.isIndirect()) pages.insert(key(annot), entry);
    }
    return pages;
}

// The inverse of PdfSignatureWriter's userRect: a /Rect in the page's top-left display space.
QRectF displayRect(const QPDFObjectHandle::Rectangle &r, const Page &page)
{
    const auto &box = page.box;
    const qreal llx = std::min(box.llx, box.urx), urx = std::max(box.llx, box.urx);
    const qreal lly = std::min(box.lly, box.ury), ury = std::max(box.lly, box.ury);
    const qreal w = urx - llx, h = ury - lly;
    const qreal rx = std::min(r.llx, r.urx), ry = std::min(r.lly, r.ury);
    const qreal rw = std::abs(r.urx - r.llx), rh = std::abs(r.ury - r.lly);

    switch (page.rotate) {
    case 90:  return { ry - lly,           rx - llx,           rh, rw };
    case 180: return { llx + w - rx - rw,  ry - lly,           rw, rh };
    case 270: return { lly + h - ry - rh,  llx + w - rx - rw,  rh, rw };
    default:  return { rx - llx,           lly + h - ry - rh,  rw, rh };
    }
}

// Follows the transform through the page content up to where a signature's stamp is drawn:
// PdfSignatureWriter's form /OPDFSig<n>, or its image on its own once an edit took it out of
// that form, which the image's /OPDFSignature names.
class StampDrawn : public QPDFObjectHandle::ParserCallbacks
{
public:
    StampDrawn(const QPDFObjectHandle &signature, const QPDFObjectHandle &xobjects)
        : m_signature(signature)
        , m_xobjects(xobjects)
        , m_form("/OPDFSig" + std::to_string(signature.getObjectID()))
    {
    }

    void handleObject(QPDFObjectHandle obj, size_t, size_t) override
    {
        if (!obj.isOperator()) {
            m_operands.push_back(obj);
            return;
        }
        const std::string op = obj.getOperatorValue();
        if (op == "q") {
            m_saved.push_back(m_ctm);
        } else if (op == "Q" && !m_saved.empty()) {
            m_ctm = m_saved.back();
            m_saved.pop_back();
        } else if (op == "cm" && m_operands.size() == 6) {
            double v[6];
            bool numbers = true;
            for (int i = 0; i < 6; ++i) numbers = numbers && m_operands[size_t(i)].getValueAsNumber(v[i]);
            if (numbers) m_ctm = QTransform(v[0], v[1], v[2], v[3], v[4], v[5]) * m_ctm;
        } else if (op == "Do" && bounds.isEmpty() && m_operands.size() == 1 && m_operands[0].isName()) {
            drawn(m_operands[0].getName());
        }
        m_operands.clear();
    }
    void handleEOF() override {}

    QRectF bounds;

private:
    void drawn(const std::string &name)
    {
        const QPDFObjectHandle x = m_xobjects.isDictionary() ? m_xobjects.getKey(name)
                                                             : QPDFObjectHandle::newNull();
        if (!x.isStream()) return;
        const QPDFObjectHandle dict = x.getDict();
        if (name == m_form && dict.getKey("/BBox").isRectangle()) {
            QTransform matrix;
            if (dict.getKey("/Matrix").isMatrix()) {
                const QPDFObjectHandle::Matrix v = dict.getKey("/Matrix").getArrayAsMatrix();
                matrix = QTransform(v.a, v.b, v.c, v.d, v.e, v.f);
            }
            const QPDFObjectHandle::Rectangle b = dict.getKey("/BBox").getArrayAsRectangle();
            bounds = (matrix * m_ctm).mapRect(QRectF(QPointF(b.llx, b.lly), QPointF(b.urx, b.ury))
                                                  .normalized());
            return;
        }
        const QPDFObjectHandle owner = dict.getKey("/OPDFSignature");
        if (dict.getKey("/Subtype").isNameAndEquals("/Image") && owner.isIndirect()
                && owner.getObjGen() == m_signature.getObjGen())
            bounds = m_ctm.mapRect(QRectF(0, 0, 1, 1));
    }

    QPDFObjectHandle              m_signature;
    QPDFObjectHandle              m_xobjects;
    std::string                   m_form;
    std::vector<QPDFObjectHandle> m_operands;
    std::vector<QTransform>       m_saved;
    QTransform                    m_ctm;
};

bool stampRect(const QPDFObjectHandle &signature, const Page &page,
               QPDFObjectHandle::Rectangle &out)
{
    if (!signature.isIndirect() || !page.object.isDictionary()) return false;
    QPDFPageObjectHelper helper(page.object);
    const QPDFObjectHandle resources = helper.getAttribute("/Resources", false);
    StampDrawn stamp(signature, resources.isDictionary() ? resources.getKey("/XObject")
                                                         : QPDFObjectHandle::newNull());
    try {
        helper.parseContents(&stamp);
    } catch (const std::exception &) {
        return false;
    }
    if (stamp.bounds.isEmpty()) return false;
    out = { stamp.bounds.left(), stamp.bounds.top(), stamp.bounds.right(), stamp.bounds.bottom() };
    return true;
}

void place(SignatureStatus &status, const QPDFObjectHandle &field,
           const QPDFObjectHandle &signature, const Pages &pages)
{
    QPDFObjectHandle widget = field;
    if (!widget.getKey("/Rect").isArray()) {
        const QPDFObjectHandle kids = field.getKey("/Kids");
        if (!kids.isArray() || kids.getArrayNItems() == 0) return;
        widget = kids.getArrayItem(0);
    }
    const QPDFObjectHandle rect = widget.getKey("/Rect");
    const auto page = pages.constFind(key(widget));
    if (!rect.isRectangle() || page == pages.cend()) return;
    QPDFObjectHandle::Rectangle drawn = rect.getArrayAsRectangle();
    stampRect(signature, *page, drawn);
    status.page   = page->index;
    status.bounds = displayRect(drawn, *page);
}

bool isOwn(CertificateProvider &provider, const QByteArray &fingerprint)
{
    const QList<Certificate> own = provider.certificates();
    return !fingerprint.isEmpty()
        && std::any_of(own.begin(), own.end(),
                       [&](const Certificate &c) { return c.fingerprint == fingerprint; });
}

SignatureStatus check(const QString &name, const QPDFObjectHandle &sig,
                      const QByteArray &data, CertificateProvider &provider, bool trustOwn)
{
    SignatureStatus status;
    status.field    = name;
    status.reason   = text(sig.getKey("/Reason"));
    status.location = text(sig.getKey("/Location"));
    status.time     = DocumentInfo::parseDate(text(sig.getKey("/M")));
    status.signer   = text(sig.getKey("/Name"));

    const QPDFObjectHandle range    = sig.getKey("/ByteRange");
    const QPDFObjectHandle contents = sig.getKey("/Contents");
    if (!range.isArray() || range.getArrayNItems() != 4 || !contents.isString()) return status;

    qint64 r[4];
    for (int i = 0; i < 4; ++i) {
        const QPDFObjectHandle item = range.getArrayItem(i);
        if (!item.isInteger()) return status;
        r[i] = item.getIntValue();
    }
    // The gap between the two ranges must be exactly the /Contents hex string.
    if (r[0] != 0 || r[1] <= 0 || r[2] <= r[1] || r[3] < 0 || r[2] + r[3] > data.size()
            || data[r[1]] != '<' || data[r[2] - 1] != '>')
        return status;

    const QByteArrayView all(data);
    const CmsCheck cms = provider.check(QByteArray::fromStdString(contents.getStringValue()),
                                        { all.first(r[1]), all.sliced(r[2], r[3]) });
    if (!cms.signer.isEmpty()) status.signer = cms.signer;
    status.issuer          = cms.issuer;
    status.validTo         = cms.validTo;
    status.fingerprint     = cms.fingerprint;
    status.coversWholeFile = r[2] + r[3] == data.size();
    const bool trusted = cms.trusted || TrustedSigners::contains(cms.fingerprint)
                      || (trustOwn && cms.intact && isOwn(provider, cms.fingerprint));
    status.state = !cms.intact ? SignatureStatus::State::Invalid
                 : trusted     ? SignatureStatus::State::Valid
                               : SignatureStatus::State::Untrusted;
    return status;
}

void collect(const QPDFObjectHandle &field, const QString &parentName, QString type,
             const QByteArray &data, CertificateProvider &provider, bool trustOwn,
             const Pages &pages, QList<SignatureStatus> &out, int depth)
{
    if (!field.isDictionary() || depth > 32) return;
    const QString part = text(field.getKey("/T"));
    const QString name = parentName.isEmpty() ? part
                       : part.isEmpty()       ? parentName
                                              : parentName + QLatin1Char('.') + part;
    if (field.getKey("/FT").isName())
        type = QString::fromStdString(field.getKey("/FT").getName());

    const QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle &kid : kids.getArrayAsVector())
            collect(kid, name, type, data, provider, trustOwn, pages, out, depth + 1);
    }
    const QPDFObjectHandle value = field.getKey("/V");
    if (type == QLatin1String("/Sig") && value.isDictionary()) {
        SignatureStatus status = check(name, value, data, provider, trustOwn);
        place(status, field, value, pages);
        out << status;
    }
}

}
#endif

namespace SignatureVerifier {

QList<SignatureStatus> verify(const QString &path, CertificateProvider &provider,
                              const std::string &password, bool trustOwn)
{
    QList<SignatureStatus> out;
#ifdef HAVE_QPDF
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return out;
    const QByteArray data = file.readAll();
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("verify", data.constData(), size_t(data.size()),
                              password.empty() ? nullptr : password.c_str());
        const QPDFObjectHandle acroForm = pdf.getRoot().getKey("/AcroForm");
        const QPDFObjectHandle fields = acroForm.isDictionary() ? acroForm.getKey("/Fields")
                                                                : QPDFObjectHandle::newNull();
        if (fields.isArray()) {
            const Pages pages = widgetPages(pdf);
            for (const QPDFObjectHandle &field : fields.getArrayAsVector())
                collect(field, {}, {}, data, provider, trustOwn, pages, out, 0);
        }
    } catch (const std::exception &) {
        return {};
    }
#else
    Q_UNUSED(path)
    Q_UNUSED(provider)
    Q_UNUSED(password)
    Q_UNUSED(trustOwn)
#endif
    return out;
}

}
