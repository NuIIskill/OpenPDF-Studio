#include "engine/sign/SignatureRemover.hpp"

#include <QFile>
#include <QList>

#include <optional>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#endif

namespace {

struct Signed {
    QString name;
    qint64  contents { 0 };
    qint64  end { 0 };
};

struct Cut {
    qsizetype at { 0 };
    int       removed { 0 };
    bool      laterChanges { false };
    int       total { 0 };
};

#ifdef HAVE_QPDF
QString text(const QPDFObjectHandle &obj)
{
    return obj.isString() ? QString::fromStdString(obj.getUTF8Value()) : QString();
}

void collect(const QPDFObjectHandle &field, const QString &parentName, bool isSig,
             QList<Signed> &out, int depth)
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
    const QPDFObjectHandle value = field.getKey("/V");
    if (!isSig || !value.isDictionary()) return;
    const QPDFObjectHandle range = value.getKey("/ByteRange");
    if (!range.isArray() || range.getArrayNItems() != 4) return;
    qint64 r[4];
    for (int i = 0; i < 4; ++i) {
        const QPDFObjectHandle item = range.getArrayItem(i);
        if (!item.isInteger()) return;
        r[i] = item.getIntValue();
    }
    out.append({ name, r[1], r[2] + r[3] });
}
#endif

std::optional<QList<Signed>> signatures(const QByteArray &data, const std::string &password)
{
#ifdef HAVE_QPDF
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("signatures", data.constData(), size_t(data.size()),
                              password.empty() ? nullptr : password.c_str());
        QList<Signed> out;
        const QPDFObjectHandle acroForm = pdf.getRoot().getKey("/AcroForm");
        const QPDFObjectHandle fields = acroForm.isDictionary() ? acroForm.getKey("/Fields")
                                                                : QPDFObjectHandle::newNull();
        if (fields.isArray()) {
            for (const QPDFObjectHandle &field : fields.getArrayAsVector())
                collect(field, {}, false, out, 0);
        }
        return out;
    } catch (const std::exception &) {
        return std::nullopt;
    }
#else
    Q_UNUSED(data)
    Q_UNUSED(password)
    return std::nullopt;
#endif
}

std::optional<Cut> findCut(const QByteArray &data, const QString &field, const std::string &password)
{
    const auto all = signatures(data, password);
    if (!all) return std::nullopt;
    qint64 contents = -1;
    for (const Signed &s : *all)
        if (s.name == field) contents = s.contents;
    if (contents <= 0 || contents > data.size()) return std::nullopt;

    // The signature arrived as an incremental update, so the revision before it
    // ends with the last %%EOF ahead of its /Contents.
    const qsizetype eof = data.lastIndexOf("%%EOF", contents);
    if (eof < 0) return std::nullopt;
    Cut cut;
    cut.at = eof + 5;
    while (cut.at < data.size() && (data[cut.at] == '\r' || data[cut.at] == '\n')) ++cut.at;
    cut.total = int(all->size());
    qint64 signedEnd = cut.at;
    for (const Signed &s : *all) {
        if (s.contents < cut.at) continue;
        ++cut.removed;
        signedEnd = qMax(signedEnd, qMin(s.end, qint64(data.size())));
    }
    const int revisions = int(data.sliced(cut.at).count("%%EOF"));
    cut.laterChanges = revisions > cut.removed || !data.sliced(signedEnd).trimmed().isEmpty();
    return cut;
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
    return { true, cut->removed, cut->laterChanges };
}

bool remove(const QString &source, const QString &output, const QString &field,
            const std::string &password)
{
    const QByteArray data = readAll(source);
    const auto cut = data.isEmpty() ? std::nullopt : findCut(data, field, password);
    if (!cut) return false;

    const QByteArray before = data.first(cut->at);
    const auto remaining = signatures(before, password);
    if (!remaining || remaining->size() != cut->total - cut->removed) return false;

    QFile out(output);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return out.write(before) == before.size() && out.flush();
}

}
