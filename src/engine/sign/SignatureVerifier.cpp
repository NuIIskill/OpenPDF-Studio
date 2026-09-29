#include "engine/sign/SignatureVerifier.hpp"

#include <QFile>
#include <QTimeZone>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#endif

#ifdef HAVE_QPDF
namespace {

QString text(const QPDFObjectHandle &obj)
{
    return obj.isString() ? QString::fromStdString(obj.getUTF8Value()) : QString();
}

QDateTime pdfDate(const QString &value)
{
    QString s = value.startsWith(QLatin1String("D:")) ? value.mid(2) : value;
    if (s.size() < 14) return {};
    QDateTime time = QDateTime::fromString(s.left(14), QStringLiteral("yyyyMMddHHmmss"));
    if (!time.isValid()) return {};
    const QString zone = s.mid(14);
    if (zone.startsWith(QLatin1Char('+')) || zone.startsWith(QLatin1Char('-'))) {
        const int hours   = zone.mid(1, 2).toInt();
        const int minutes = zone.mid(4, 2).toInt();
        const int sign    = zone.startsWith(QLatin1Char('-')) ? -1 : 1;
        time.setTimeZone(QTimeZone::fromSecondsAheadOfUtc(sign * (hours * 3600 + minutes * 60)));
    } else {
        time.setTimeZone(QTimeZone::UTC);
    }
    return time;
}

SignatureStatus check(const QString &name, const QPDFObjectHandle &sig,
                      const QByteArray &data, CertificateProvider &provider)
{
    SignatureStatus status;
    status.field    = name;
    status.reason   = text(sig.getKey("/Reason"));
    status.location = text(sig.getKey("/Location"));
    status.time     = pdfDate(text(sig.getKey("/M")));
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
    status.coversWholeFile = r[2] + r[3] == data.size();
    status.state = !cms.intact ? SignatureStatus::State::Invalid
                 : cms.trusted ? SignatureStatus::State::Valid
                               : SignatureStatus::State::Untrusted;
    return status;
}

void collect(const QPDFObjectHandle &field, const QString &parentName, QString type,
             const QByteArray &data, CertificateProvider &provider, QList<SignatureStatus> &out,
             int depth)
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
            collect(kid, name, type, data, provider, out, depth + 1);
    }
    const QPDFObjectHandle value = field.getKey("/V");
    if (type == QLatin1String("/Sig") && value.isDictionary())
        out << check(name, value, data, provider);
}

}
#endif

namespace SignatureVerifier {

QList<SignatureStatus> verify(const QString &path, CertificateProvider &provider)
{
    QList<SignatureStatus> out;
#ifdef HAVE_QPDF
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return out;
    const QByteArray data = file.readAll();
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("verify", data.constData(), size_t(data.size()));
        if (pdf.isEncrypted()) return out;
        const QPDFObjectHandle acroForm = pdf.getRoot().getKey("/AcroForm");
        const QPDFObjectHandle fields = acroForm.isDictionary() ? acroForm.getKey("/Fields")
                                                                : QPDFObjectHandle::newNull();
        if (fields.isArray()) {
            for (const QPDFObjectHandle &field : fields.getArrayAsVector())
                collect(field, {}, {}, data, provider, out, 0);
        }
    } catch (const std::exception &) {
        return {};
    }
#else
    Q_UNUSED(path)
    Q_UNUSED(provider)
#endif
    return out;
}

}
