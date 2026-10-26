#include "engine/sign/SignatureRemover.hpp"

#include <QFile>
#include <QList>

#include <algorithm>
#include <optional>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFWriter.hh>
#endif

namespace {

struct Signed {
    QString name;
    qint64  contents { 0 };
    qint64  end { 0 };
};

struct Cut {
    qsizetype at { 0 };
    bool      inPlace { false };
    int       removed { 0 };
    bool      laterChanges { false };
};

#ifdef HAVE_QPDF
struct SignedField {
    QString          name;
    QPDFObjectHandle field;
};

QString text(const QPDFObjectHandle &obj)
{
    return obj.isString() ? QString::fromStdString(obj.getUTF8Value()) : QString();
}

void collect(const QPDFObjectHandle &field, const QString &parentName, bool isSig,
             QList<SignedField> &out, int depth)
{
    if (!field.isDictionary() || depth > 32) return;
    const QString part = text(field.getKey("/T"));
    const QString name = parentName.isEmpty() ? part
                       : part.isEmpty()       ? parentName
                                              : parentName + QLatin1Char('.') + part;
    const QPDFObjectHandle type = field.getKey("/FT");
    if (type.isName()) isSig = type.getName() == "/Sig";

    const QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle &kid : kids.getArrayAsVector())
            collect(kid, name, isSig, out, depth + 1);
    }
    if (isSig && field.getKey("/V").isDictionary()) out.append({ name, field });
}

QList<SignedField> signedFields(QPDF &pdf)
{
    QList<SignedField> out;
    const QPDFObjectHandle acroForm = pdf.getRoot().getKey("/AcroForm");
    const QPDFObjectHandle fields = acroForm.isDictionary() ? acroForm.getKey("/Fields")
                                                            : QPDFObjectHandle::newNull();
    if (fields.isArray()) {
        for (const QPDFObjectHandle &field : fields.getArrayAsVector())
            collect(field, {}, false, out, 0);
    }
    return out;
}

void open(QPDF &pdf, const QByteArray &data, const std::string &password, bool strict)
{
    pdf.setSuppressWarnings(true);
    pdf.setAttemptRecovery(!strict);
    pdf.processMemoryFile("signatures", data.constData(), size_t(data.size()),
                          password.empty() ? nullptr : password.c_str());
}

qint64 offset(QPDF &pdf, const QPDFObjectHandle &obj)
{
    if (!obj.isIndirect()) return -1;
    const std::map<QPDFObjGen, QPDFXRefEntry> table = pdf.getXRefTable();
    auto it = table.find(obj.getObjGen());
    if (it != table.end() && it->second.getType() == 2)
        it = table.find(QPDFObjGen(it->second.getObjStreamNumber(), 0));
    return it != table.end() && it->second.getType() == 1 ? it->second.getOffset() : -1;
}
#endif

std::optional<QList<Signed>> signatures(const QByteArray &data, const std::string &password,
                                        bool strict)
{
#ifdef HAVE_QPDF
    try {
        QPDF pdf;
        open(pdf, data, password, strict);
        QList<Signed> out;
        for (const SignedField &f : signedFields(pdf)) {
            const QPDFObjectHandle value = f.field.getKey("/V");
            const QPDFObjectHandle range = value.getKey("/ByteRange");
            qint64 r[4] {};
            bool numbers = range.isArray() && range.getArrayNItems() == 4;
            for (int i = 0; numbers && i < 4; ++i) {
                const QPDFObjectHandle item = range.getArrayItem(i);
                numbers = item.isInteger();
                r[i] = numbers ? item.getIntValue() : 0;
            }
            if (numbers && r[1] > 0 && r[1] < data.size() && data[r[1]] == '<') {
                out.append({ f.name, r[1], r[2] + r[3] });
                continue;
            }
            // A file written anew after signing keeps the old ByteRange, so the
            // signature is placed by where its object sits now.
            const qint64 at = offset(pdf, value.isIndirect() ? value : f.field);
            if (at >= 0) out.append({ f.name, at, at });
        }
        return out;
    } catch (const std::exception &) {
        return std::nullopt;
    }
#else
    Q_UNUSED(data)
    Q_UNUSED(password)
    Q_UNUSED(strict)
    return std::nullopt;
#endif
}

qsizetype revisionEnd(const QByteArray &data, qsizetype eof)
{
    qsizetype at = eof + 5;
    while (at < data.size() && (data[at] == '\r' || data[at] == '\n')) ++at;
    return at;
}

std::optional<Cut> findCut(const QByteArray &data, const QString &field, const std::string &password)
{
    const auto all = signatures(data, password, false);
    if (!all) return std::nullopt;
    qint64 contents = -1;
    for (const Signed &s : *all)
        if (s.name == field) contents = s.contents;
    if (contents <= 0 || contents > data.size()) return std::nullopt;

    const auto laterThan = [&](qsizetype at) {
        int n = 0;
        for (const Signed &s : *all) n += s.contents >= at;
        return n;
    };

    // The signature usually arrived as an incremental update, so the revision
    // before it ends with the last %%EOF ahead of its /Contents. Without such a
    // revision it is cleared out of its own one instead.
    Cut cut;
    const qsizetype before = data.lastIndexOf("%%EOF", contents);
    if (before >= 0) {
        cut.at = revisionEnd(data, before);
        const auto earlier = signatures(data.first(cut.at), password, true);
        if (earlier && earlier->size() == all->size() - laterThan(cut.at)) {
            cut.removed = laterThan(cut.at);
        } else {
            cut.inPlace = true;
        }
    }
    if (before < 0 || cut.inPlace) {
        const qsizetype own = data.indexOf("%%EOF", contents);
        if (own < 0) return std::nullopt;
        cut.at = revisionEnd(data, own);
        const auto kept = signatures(data.first(cut.at), password, false);
        if (!kept || std::none_of(kept->cbegin(), kept->cend(),
                                  [&](const Signed &s) { return s.name == field; }))
            return std::nullopt;
        cut.inPlace = true;
        cut.removed = laterThan(cut.at) + 1;
    }

    qint64 signedEnd = cut.at;
    for (const Signed &s : *all)
        if (s.contents >= cut.at) signedEnd = qMax(signedEnd, qMin(s.end, qint64(data.size())));
    const int revisions = int(data.sliced(cut.at).count("%%EOF"));
    cut.laterChanges = revisions > laterThan(cut.at)
                    || !data.sliced(signedEnd).trimmed().isEmpty();
    return cut;
}

bool clearField(const QByteArray &data, const QString &field, const std::string &password,
                const QString &output)
{
#ifdef HAVE_QPDF
    try {
        QPDF pdf;
        open(pdf, data, password, false);
        QPDFObjectHandle target;
        for (const SignedField &f : signedFields(pdf))
            if (f.name == field) target = f.field;
        if (!target.isDictionary()) return false;

        const QPDFObjectHandle value = target.getKey("/V");
        target.removeKey("/V");
        const QPDFObjectHandle kids = target.getKey("/Kids");
        QList<QPDFObjectHandle> widgets { target };
        if (kids.isArray())
            for (const QPDFObjectHandle &kid : kids.getArrayAsVector()) widgets << kid;
        for (QPDFObjectHandle &widget : widgets)
            if (widget.isDictionary() && widget.getKey("/Subtype").isNameAndEquals("/Widget"))
                widget.removeKey("/AP");

        QPDFObjectHandle root = pdf.getRoot();
        QPDFObjectHandle perms = root.getKey("/Perms");
        if (perms.isDictionary()) {
            for (const char *key : { "/DocMDP", "/UR3" })
                if (perms.getKey(key).isSameObjectAs(value)) perms.removeKey(key);
            if (perms.getKeys().empty()) root.removeKey("/Perms");
        }
        QPDFObjectHandle acroForm = root.getKey("/AcroForm");
        if (signedFields(pdf).isEmpty() && acroForm.isDictionary()
                && acroForm.getKey("/SigFlags").isInteger()) {
            const long long flags = acroForm.getKey("/SigFlags").getIntValue();
            acroForm.replaceKey("/SigFlags", QPDFObjectHandle::newInteger(flags & ~2LL));
        }

        QPDFWriter writer(pdf, output.toLocal8Bit().constData());
        writer.setPreserveEncryption(true);
        writer.setCompressStreams(true);
        writer.write();
        return true;
    } catch (const std::exception &) {
        return false;
    }
#else
    Q_UNUSED(data)
    Q_UNUSED(field)
    Q_UNUSED(password)
    Q_UNUSED(output)
    return false;
#endif
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}

namespace SignatureRemover {

Removal plan(const QString &source, const QString &field, const std::string &password)
{
    const QByteArray data = readAll(source);
    const auto cut = data.isEmpty() ? std::nullopt : findCut(data, field, password);
    if (!cut) return {};
    return { true, cut->removed, cut->laterChanges, cut->inPlace };
}

bool remove(const QString &source, const QString &output, const QString &field,
            const std::string &password)
{
    const QByteArray data = readAll(source);
    const auto cut = data.isEmpty() ? std::nullopt : findCut(data, field, password);
    if (!cut) return false;

    const QByteArray kept = data.first(cut->at);
    if (cut->inPlace) return clearField(kept, field, password, output);

    QFile out(output);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return out.write(kept) == kept.size() && out.flush();
}

}
