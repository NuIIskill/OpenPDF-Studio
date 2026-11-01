#include "engine/sign/PdfSignatureWriter.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QSet>

#ifdef HAVE_QPDF
#  include <qpdf/BufferInputSource.hh>
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFCryptoProvider.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFTokenizer.hh>
#endif

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef HAVE_QPDF
namespace {

constexpr QByteArrayView kByteRangePlaceholder = "[0 0000000000 0000000000 0000000000]";
constexpr int kLockedPrintFlags = 132;
constexpr int kSigFlags         = 3;

struct Object {
    int                       id;
    int                       gen;
    QByteArray                body;
    std::optional<QByteArray> stream;
};

// The document's security handler, applied to the strings and streams the update adds.
class Encryption
{
public:
    bool load(QPDF &pdf)
    {
        int permissions = 0;
        QPDF::encryption_method_e files = QPDF::e_none;
        if (!pdf.isEncrypted(m_R, permissions, m_V, m_streams, m_strings, files)) return true;
        // Before crypt filters qpdf names no method, but everything is RC4.
        if (m_V < 4) m_streams = m_strings = QPDF::e_rc4;
        m_key = pdf.getEncryptionKey();
        const auto writable = [](QPDF::encryption_method_e m) {
            return m == QPDF::e_none || m == QPDF::e_rc4 || m == QPDF::e_aes || m == QPDF::e_aesv3;
        };
        return !m_key.empty() && writable(m_streams) && writable(m_strings);
    }

    QByteArray strings(const QByteArray &body, int id, int gen, bool keepContents) const
    {
        if (m_strings == QPDF::e_none) return body;
        const std::string text = body.toStdString();
        BufferInputSource input("object", text);
        QPDFTokenizer tokenizer;
        QByteArray out;
        qsizetype copied = 0;
        bool afterContents = false;
        for (;;) {
            const QPDFTokenizer::Token token = tokenizer.readToken(input, "object", true);
            const QPDFTokenizer::token_type_e type = token.getType();
            if (type == QPDFTokenizer::tt_eof || type == QPDFTokenizer::tt_bad) {
                if (input.tell() < qpdf_offset_t(text.size()))
                    throw std::runtime_error("unreadable object");
                break;
            }
            // A signature's /Contents is never encrypted.
            if (type == QPDFTokenizer::tt_string && !(keepContents && afterContents)) {
                out += body.mid(copied, qsizetype(input.getLastOffset()) - copied);
                out += '<' + apply(m_strings, QByteArray::fromStdString(token.getValue()), id, gen)
                                 .toHex().toUpper() + '>';
                copied = qsizetype(input.tell());
            }
            afterContents = type == QPDFTokenizer::tt_name && token.getValue() == "/Contents";
        }
        return out + body.mid(copied);
    }

    QByteArray stream(const QByteArray &data, int id, int gen) const
    {
        return apply(m_streams, data, id, gen);
    }

private:
    QByteArray apply(QPDF::encryption_method_e method, const QByteArray &plain, int id, int gen) const
    {
        if (method == QPDF::e_none) return plain;
        const bool aes = method != QPDF::e_rc4;
        const std::string key = QPDF::compute_data_key(m_key, id, gen, aes, m_V, m_R);
        const auto *keyData = reinterpret_cast<const unsigned char *>(key.data());
        const std::shared_ptr<QPDFCryptoImpl> crypto = QPDFCryptoProvider::getImpl();

        if (!aes) {
            QByteArray out(plain.size(), Qt::Uninitialized);
            crypto->RC4_init(keyData, int(key.size()));
            crypto->RC4_process(reinterpret_cast<const unsigned char *>(plain.constData()),
                                size_t(plain.size()), reinterpret_cast<unsigned char *>(out.data()));
            crypto->RC4_finalize();
            return out;
        }

        constexpr qsizetype block = QPDFCryptoImpl::rijndael_buf_size;
        const qsizetype pad = block - plain.size() % block;
        QByteArray padded = plain + QByteArray(pad, char(pad));
        QByteArray out(block + padded.size(), Qt::Uninitialized);
        auto *outData = reinterpret_cast<unsigned char *>(out.data());
        crypto->provideRandomData(outData, size_t(block));
        QByteArray chain = out.first(block);
        crypto->rijndael_init(true, keyData, key.size(), true,
                              reinterpret_cast<unsigned char *>(chain.data()));
        for (qsizetype at = 0; at < padded.size(); at += block)
            crypto->rijndael_process(reinterpret_cast<unsigned char *>(padded.data() + at),
                                     outData + block + at);
        crypto->rijndael_finalize();
        return out;
    }

    std::string               m_key;
    int                       m_V { 0 };
    int                       m_R { 0 };
    QPDF::encryption_method_e m_strings { QPDF::e_none };
    QPDF::encryption_method_e m_streams { QPDF::e_none };
};

QByteArray num(qreal v)
{
    QByteArray s = QByteArray::number(v, 'f', 3);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    return s == "-0" ? QByteArray("0") : s;
}

QByteArray ref(int id)
{
    return QByteArray::number(id) + " 0 R";
}

QByteArray unparsed(const QPDFObjectHandle &obj)
{
    return QByteArray::fromStdString(obj.unparse());
}

QByteArray textString(const QString &text)
{
    const bool ascii = std::all_of(text.begin(), text.end(), [](QChar c) {
        return c.unicode() >= 0x20 && c.unicode() < 0x7F;
    });
    if (ascii) {
        QByteArray out = "(";
        for (QChar c : text) {
            if (c == QLatin1Char('\\') || c == QLatin1Char('(') || c == QLatin1Char(')'))
                out += '\\';
            out += char(c.unicode());
        }
        return out + ')';
    }
    QByteArray utf16("\xFE\xFF", 2);
    for (QChar c : text) {
        utf16 += char(c.unicode() >> 8);
        utf16 += char(c.unicode() & 0xFF);
    }
    return '<' + utf16.toHex().toUpper() + '>';
}

QByteArray pdfDate(const QDateTime &time)
{
    QByteArray out = "(D:" + time.toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1();
    const int offset = time.offsetFromUtc() / 60;
    if (offset == 0) return out + "Z)";
    out += offset > 0 ? '+' : '-';
    out += QString::asprintf("%02d'%02d')", std::abs(offset) / 60, std::abs(offset) % 60).toLatin1();
    return out;
}

qsizetype lastStartXref(const QByteArray &data)
{
    const qsizetype at = data.lastIndexOf("startxref");
    if (at < 0) return -1;
    qsizetype i = at + 9;
    while (i < data.size() && std::isspace(uchar(data[i]))) ++i;
    const qsizetype start = i;
    while (i < data.size() && std::isdigit(uchar(data[i]))) ++i;
    bool ok = false;
    const qsizetype offset = data.mid(start, i - start).toLongLong(&ok);
    return ok && offset < data.size() ? offset : -1;
}

bool usesXrefTable(const QByteArray &data, qsizetype offset)
{
    while (offset < data.size() && std::isspace(uchar(data[offset]))) ++offset;
    return QByteArrayView(data).sliced(offset).startsWith("xref");
}

QByteArray arrayWith(const QPDFObjectHandle &array, const QByteArray &item)
{
    QByteArray out = "[";
    if (array.isArray()) {
        for (const QPDFObjectHandle &entry : array.getArrayAsVector())
            out += ' ' + unparsed(entry);
    }
    return out + ' ' + item + " ]";
}

QByteArray dictWith(QPDFObjectHandle dict, const QByteArray &entries,
                    const std::vector<const char *> &replaced)
{
    QPDFObjectHandle copy = dict.shallowCopy();
    for (const char *key : replaced) copy.removeKey(key);
    QByteArray body = unparsed(copy).trimmed();
    body.chop(2);
    return body.trimmed() + ' ' + entries + " >>";
}

QRectF userRect(const QRectF &display, const QPDFObjectHandle::Rectangle &box, int rotate)
{
    const qreal llx = std::min(box.llx, box.urx), urx = std::max(box.llx, box.urx);
    const qreal lly = std::min(box.lly, box.ury), ury = std::max(box.lly, box.ury);
    const qreal w = urx - llx, h = ury - lly;
    const qreal x = display.x(), y = display.y(), dw = display.width(), dh = display.height();

    switch (rotate) {
    case 90:  return { llx + y,            lly + x,            dh, dw };
    case 180: return { llx + w - (x + dw), lly + y,            dw, dh };
    case 270: return { llx + w - (y + dh), lly + h - (x + dw), dh, dw };
    default:  return { llx + x,            lly + h - (y + dh), dw, dh };
    }
}

QByteArray rotationMatrix(int rotate)
{
    switch (rotate) {
    case 90:  return "[0 1 -1 0 0 0]";
    case 180: return "[-1 0 0 -1 0 0]";
    case 270: return "[0 -1 1 0 0 0]";
    default:  return "[1 0 0 1 0 0]";
    }
}

// Where the stamp's form, turned by the page rotation, must be moved to fill the rect.
QPointF stampOffset(const QRectF &rect, const QSizeF &size, int rotate)
{
    switch (rotate) {
    case 90:  return { rect.left() + size.height(), rect.top() };
    case 180: return { rect.left() + size.width(),  rect.top() + size.height() };
    case 270: return { rect.left(),                 rect.top() + size.width() };
    default:  return { rect.left(),                 rect.top() };
    }
}

// How the page content moves the graphics state stack: how far below its start it pops and where it ends.
class StateDepth : public QPDFObjectHandle::ParserCallbacks
{
public:
    void handleObject(QPDFObjectHandle obj, size_t, size_t) override
    {
        if (!obj.isOperator()) return;
        const std::string op = obj.getOperatorValue();
        if (op == "q") ++depth;
        else if (op == "Q") lowest = std::min(lowest, --depth);
    }
    void handleEOF() override {}

    int depth  { 0 };
    int lowest { 0 };
};

// The stamp is drawn in default user space only if the page content can neither pop the
// state saved before it nor leave one open, so it gets enough saves before and restores after.
std::pair<QByteArray, QByteArray> stampWrapper(QPDFPageObjectHelper page)
{
    StateDepth states;
    try {
        page.parseContents(&states);
    } catch (const std::exception &) {
        states = StateDepth();
    }
    const int saves = 1 - states.lowest;
    return { QByteArray("q ").repeated(saves).trimmed(),
             QByteArray("Q ").repeated(saves + states.depth) };
}

Object streamObject(int id, const QByteArray &dictEntries, const QByteArray &content)
{
    return { id, 0, "<< " + dictEntries + " >>", content };
}

QByteArray serialized(const Object &obj, const Encryption &encryption, bool keepContents)
{
    QByteArray body = encryption.strings(obj.body, obj.id, obj.gen, keepContents);
    if (!obj.stream) return body;
    const QByteArray data = encryption.stream(*obj.stream, obj.id, obj.gen);
    body.chop(2);
    return body + "/Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data
         + "\nendstream";
}

QByteArray deflated(const QByteArray &raw)
{
    return qCompress(raw, 6).mid(4);
}

// The image carries its signature, so the stamp can still be found after an edit takes it out of its form.
void imageObjects(const QImage &image, int imageId, int maskId, int sigId, QList<Object> &objects)
{
    const QImage img = image.convertToFormat(QImage::Format_RGBA8888);
    QByteArray rgb, alpha;
    rgb.reserve(qsizetype(img.width()) * img.height() * 3);
    alpha.reserve(qsizetype(img.width()) * img.height());
    for (int y = 0; y < img.height(); ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            rgb.append(reinterpret_cast<const char *>(line + x * 4), 3);
            alpha.append(char(line[x * 4 + 3]));
        }
    }
    const QByteArray size = "/Width " + QByteArray::number(img.width())
                          + " /Height " + QByteArray::number(img.height())
                          + " /BitsPerComponent 8 /Filter /FlateDecode";
    objects << streamObject(imageId,
        "/Type /XObject /Subtype /Image " + size + " /ColorSpace /DeviceRGB /SMask " + ref(maskId)
            + " /OPDFSignature " + ref(sigId),
        deflated(rgb));
    objects << streamObject(maskId,
        "/Type /XObject /Subtype /Image " + size + " /ColorSpace /DeviceGray", deflated(alpha));
}

QString freeFieldName(const QPDFObjectHandle &fields)
{
    QSet<QString> used;
    if (fields.isArray()) {
        for (const QPDFObjectHandle &f : fields.getArrayAsVector()) {
            if (f.isDictionary() && f.getKey("/T").isString())
                used << QString::fromStdString(f.getKey("/T").getUTF8Value());
        }
    }
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("Signature%1").arg(n);
        if (!used.contains(name)) return name;
    }
}

QByteArray fileId(const QPDFObjectHandle &trailer, const QByteArray &data)
{
    const QByteArray fresh = QCryptographicHash::hash(
        data + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toLatin1(),
        QCryptographicHash::Md5).toHex().toUpper();
    QByteArray first = fresh;
    const QPDFObjectHandle id = trailer.getKey("/ID");
    if (id.isArray() && id.getArrayNItems() == 2 && id.getArrayItem(0).isString())
        first = QByteArray::fromStdString(id.getArrayItem(0).getStringValue()).toHex().toUpper();
    return "[<" + first + "> <" + fresh + ">]";
}

QByteArray xrefStreamEntries(const std::map<int, std::pair<qsizetype, int>> &offsets,
                             QByteArray *index)
{
    QByteArray bin;
    int runStart = -1, runLen = 0, last = -2;
    for (const auto &[id, entry] : offsets) {
        if (id != last + 1) {
            if (runStart >= 0) *index += QByteArray::number(runStart) + ' ' + QByteArray::number(runLen) + ' ';
            runStart = id;
            runLen = 0;
        }
        ++runLen;
        last = id;
        bin += char(1);
        for (int shift = 24; shift >= 0; shift -= 8) bin += char((entry.first >> shift) & 0xFF);
        bin += char((entry.second >> 8) & 0xFF);
        bin += char(entry.second & 0xFF);
    }
    if (runStart >= 0) *index += QByteArray::number(runStart) + ' ' + QByteArray::number(runLen);
    return bin;
}

}
#endif

SignError PdfSignatureWriter::prepare(const QString &source, const Field &field, int contentsBytes,
                                      const std::string &password)
{
    m_data.clear();
    m_contentsAt = -1;
    m_contentsLen = 0;
#ifndef HAVE_QPDF
    Q_UNUSED(source)
    Q_UNUSED(field)
    Q_UNUSED(contentsBytes)
    Q_UNUSED(password)
    return SignError::NotAvailable;
#else
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) return SignError::ReadFailed;
    const QByteArray data = file.readAll();
    const qsizetype prevXref = lastStartXref(data);
    if (prevXref < 0 || data.size() > 0xFFFFFFFFLL) return SignError::ReadFailed;

    QList<Object> objects;
    Encryption encryption;
    QByteArray trailerEntries;
    int size = 0;
    int sigId = 0;
    qsizetype byteRangeRel = 0, contentsRel = 0;

    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("source", data.constData(), size_t(data.size()),
                              password.empty() ? nullptr : password.c_str());
        if (!encryption.load(pdf)) return SignError::Encrypted;

        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(pdf).getAllPages();
        if (pages.empty()) return SignError::ReadFailed;
        QPDFPageObjectHelper page = pages[size_t(std::clamp(field.page, 0, int(pages.size()) - 1))];
        QPDFObjectHandle pageObj = page.getObjectHandle();
        QPDFObjectHandle trailer = pdf.getTrailer();
        QPDFObjectHandle root    = pdf.getRoot();

        int next = trailer.getKey("/Size").isInteger() ? int(trailer.getKey("/Size").getIntValue()) : 1;
        for (const QPDFObjectHandle &obj : pdf.getAllObjects())
            next = std::max(next, obj.getObjectID() + 1);

        const bool visible = !field.bounds.isEmpty() && !field.appearance.isNull();
        sigId = next++;
        const int widgetId = next++;
        const int apId     = next++;
        const int imageId  = visible ? next++ : 0;
        const int maskId   = visible ? next++ : 0;
        const int stampId  = visible ? next++ : 0;
        const int openId   = visible ? next++ : 0;
        const int drawId   = visible ? next++ : 0;

        QByteArray sig = "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /ETSI.CAdES.detached /ByteRange ";
        byteRangeRel = sig.size();
        sig += kByteRangePlaceholder.toByteArray();
        sig += " /Contents ";
        contentsRel = sig.size();
        sig += '<' + QByteArray(qsizetype(contentsBytes) * 2, '0') + '>';
        sig += " /M " + pdfDate(field.time);
        if (!field.name.isEmpty())     sig += " /Name " + textString(field.name);
        if (!field.reason.isEmpty())   sig += " /Reason " + textString(field.reason);
        if (!field.location.isEmpty()) sig += " /Location " + textString(field.location);
        objects << Object { sigId, 0, sig + " >>", std::nullopt };

        QRectF rect;
        QByteArray pageEntries;
        std::vector<const char *> pageReplaced;
        if (visible) {
            int rotate = page.getAttribute("/Rotate", false).isInteger()
                ? int(page.getAttribute("/Rotate", false).getIntValue()) : 0;
            rotate = ((rotate % 360) + 360) % 360;
            rect = userRect(field.bounds, page.getCropBox().getArrayAsRectangle(), rotate);

            // The stamp is drawn as page content: many viewers, PDFium among
            // them, do not draw widget appearances. The widget itself stays
            // empty so viewers that do draw them do not show it twice.
            const QByteArray w = num(field.bounds.width()), h = num(field.bounds.height());
            objects << streamObject(apId,
                "/Type /XObject /Subtype /Form /BBox [0 0 " + w + ' ' + h + "] /Resources << >>", {});
            objects << streamObject(stampId,
                "/Type /XObject /Subtype /Form /BBox [0 0 " + w + ' ' + h + "] /Matrix "
                    + rotationMatrix(rotate) + " /Resources << /XObject << /Img " + ref(imageId) + " >> >>",
                "q " + w + " 0 0 " + h + " 0 0 cm /Img Do Q");
            imageObjects(field.appearance, imageId, maskId, sigId, objects);

            const QByteArray name = "/OPDFSig" + QByteArray::number(sigId);
            const QPointF at = stampOffset(rect, field.bounds.size(), rotate);
            const auto [open, close] = stampWrapper(page);
            objects << streamObject(openId, {}, open);
            objects << streamObject(drawId, {},
                close + "q 1 0 0 1 " + num(at.x()) + ' ' + num(at.y()) + " cm " + name + " Do Q");

            const QPDFObjectHandle resources = page.getAttribute("/Resources", false);
            const QPDFObjectHandle resourceCopy = resources.isDictionary()
                ? QPDFObjectHandle(resources).shallowCopy() : QPDFObjectHandle::newDictionary();
            const QPDFObjectHandle xobjects = resourceCopy.getKey("/XObject");
            const QByteArray xobjectBody = dictWith(
                xobjects.isDictionary() ? QPDFObjectHandle(xobjects).shallowCopy()
                                        : QPDFObjectHandle::newDictionary(),
                name + ' ' + ref(stampId), { name.constData() });

            QByteArray contents;
            const QPDFObjectHandle oldContents = pageObj.getKey("/Contents");
            if (oldContents.isArray()) {
                for (const QPDFObjectHandle &item : oldContents.getArrayAsVector())
                    contents += ' ' + unparsed(item);
            } else if (oldContents.isStream()) {
                contents = ' ' + unparsed(oldContents);
            }
            pageEntries = "/Resources " + dictWith(resourceCopy, "/XObject " + xobjectBody, { "/XObject" })
                        + " /Contents [" + ref(openId) + contents + ' ' + ref(drawId) + " ]";
            pageReplaced = { "/Resources", "/Contents" };
        } else {
            objects << streamObject(apId, "/Type /XObject /Subtype /Form /BBox [0 0 0 0] /Resources << >>", {});
        }

        const QByteArray widgetRef = ref(widgetId);
        QPDFObjectHandle acroForm = root.getKey("/AcroForm");
        QPDFObjectHandle fields = acroForm.isDictionary() ? acroForm.getKey("/Fields")
                                                          : QPDFObjectHandle::newNull();
        objects << Object { widgetId, 0,
            "<< /Type /Annot /Subtype /Widget /FT /Sig /T " + textString(freeFieldName(fields))
            + " /V " + ref(sigId) + " /F " + QByteArray::number(kLockedPrintFlags)
            + " /P " + unparsed(pageObj)
            + " /Rect [" + num(rect.left()) + ' ' + num(rect.top()) + ' '
            + num(rect.right()) + ' ' + num(rect.bottom()) + ']'
            + " /AP << /N " + ref(apId) + " >> >>", std::nullopt };

        auto rewrite = [&objects](const QPDFObjectHandle &obj, const QByteArray &body) {
            objects << Object { obj.getObjectID(), obj.getGeneration(), body, std::nullopt };
        };

        QPDFObjectHandle annots = pageObj.getKey("/Annots");
        if (annots.isArray() && annots.isIndirect()) {
            rewrite(annots, arrayWith(annots, widgetRef));
        } else {
            pageEntries += " /Annots " + arrayWith(annots, widgetRef);
            pageReplaced.push_back("/Annots");
        }
        if (!pageEntries.isEmpty())
            rewrite(pageObj, dictWith(pageObj, pageEntries.trimmed(), pageReplaced));

        int sigFlags = kSigFlags;
        if (acroForm.isDictionary() && acroForm.getKey("/SigFlags").isInteger())
            sigFlags |= int(acroForm.getKey("/SigFlags").getIntValue());
        QByteArray acroEntries = "/SigFlags " + QByteArray::number(sigFlags);
        if (fields.isArray() && fields.isIndirect())
            rewrite(fields, arrayWith(fields, widgetRef));
        else
            acroEntries = "/Fields " + arrayWith(fields, widgetRef) + ' ' + acroEntries;

        if (acroForm.isDictionary() && acroForm.isIndirect()) {
            rewrite(acroForm, dictWith(acroForm, acroEntries, { "/Fields", "/SigFlags" }));
        } else {
            const QByteArray acroBody = acroForm.isDictionary()
                ? dictWith(acroForm, acroEntries, { "/Fields", "/SigFlags" })
                : "<< " + acroEntries + " >>";
            rewrite(root, dictWith(root, "/AcroForm " + acroBody, { "/AcroForm" }));
        }

        trailerEntries = "/Root " + unparsed(root);
        if (trailer.getKey("/Info").isIndirect())
            trailerEntries += " /Info " + unparsed(trailer.getKey("/Info"));
        if (trailer.hasKey("/Encrypt"))
            trailerEntries += " /Encrypt " + unparsed(trailer.getKey("/Encrypt"));
        trailerEntries += " /ID " + fileId(trailer, data)
                        + " /Prev " + QByteArray::number(prevXref);
        size = next;
    } catch (const std::exception &) {
        return SignError::ReadFailed;
    }

    QByteArray out = data;
    if (!out.endsWith('\n') && !out.endsWith('\r')) out += '\n';

    std::map<int, std::pair<qsizetype, int>> offsets;
    qsizetype sigAt = -1;
    try {
        for (const Object &obj : std::as_const(objects)) {
            const QByteArray header = QByteArray::number(obj.id) + ' ' + QByteArray::number(obj.gen) + " obj\n";
            offsets[obj.id] = { out.size(), obj.gen };
            if (obj.id == sigId) sigAt = out.size() + header.size();
            out += header + serialized(obj, encryption, obj.id == sigId) + "\nendobj\n";
        }
    } catch (const std::exception &) {
        return SignError::ReadFailed;
    }

    qsizetype xrefAt = out.size();
    if (usesXrefTable(data, prevXref)) {
        out += "xref\n";
        for (auto it = offsets.begin(); it != offsets.end();) {
            auto end = it;
            int count = 0;
            for (int id = it->first; end != offsets.end() && end->first == id; ++end, ++id) ++count;
            out += QByteArray::number(it->first) + ' ' + QByteArray::number(count) + '\n';
            for (; it != end; ++it)
                out += QString::asprintf("%010lld %05d n\r\n", qlonglong(it->second.first),
                                         it->second.second).toLatin1();
        }
        out += "trailer\n<< /Size " + QByteArray::number(size) + ' ' + trailerEntries + " >>\n";
    } else {
        const int xrefId = size++;
        offsets[xrefId] = { xrefAt, 0 };
        QByteArray index;
        const QByteArray entries = xrefStreamEntries(offsets, &index);
        out += QByteArray::number(xrefId) + " 0 obj\n"
             + serialized(streamObject(xrefId, "/Type /XRef /Size " + QByteArray::number(size)
                                           + " /W [1 4 2] /Index [" + index + "] " + trailerEntries,
                                       entries),
                          Encryption(), false)
             + "\nendobj\n";
    }
    out += "startxref\n" + QByteArray::number(xrefAt) + "\n%%EOF\n";

    m_contentsAt  = sigAt + contentsRel;
    m_contentsLen = qsizetype(contentsBytes) * 2;
    const qsizetype afterContents = m_contentsAt + m_contentsLen + 2;
    QByteArray byteRange = "[0 " + QByteArray::number(m_contentsAt) + ' '
                         + QByteArray::number(afterContents) + ' '
                         + QByteArray::number(out.size() - afterContents);
    byteRange = byteRange.leftJustified(kByteRangePlaceholder.size() - 1, ' ') + ']';
    out.replace(sigAt + byteRangeRel, byteRange.size(), byteRange);

    m_data = out;
    return SignError::None;
#endif
}

QList<QByteArrayView> PdfSignatureWriter::signedRanges() const
{
    if (m_contentsAt < 0) return {};
    const QByteArrayView all(m_data);
    return { all.first(m_contentsAt), all.sliced(m_contentsAt + m_contentsLen + 2) };
}

bool PdfSignatureWriter::embed(const QByteArray &cms)
{
    const QByteArray hex = cms.toHex().toUpper();
    if (m_contentsAt < 0 || hex.isEmpty() || hex.size() > m_contentsLen) return false;
    std::copy(hex.cbegin(), hex.cend(), m_data.begin() + m_contentsAt + 1);
    return true;
}
