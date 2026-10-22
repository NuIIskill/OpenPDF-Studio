#include "engine/document/DocumentInfo.hpp"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFAcroFormDocumentHelper.hh>
#  include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#endif

namespace {

QString headerVersion(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    static const QRegularExpression header(QStringLiteral("%PDF-(\\d\\.\\d)"));
    return header.match(QString::fromLatin1(file.read(1024))).captured(1);
}

#ifdef HAVE_QPDF
QString encryptionName(QPDF &pdf)
{
    int R = 0, P = 0, V = 0;
    QPDF::encryption_method_e stream = QPDF::e_none, string = QPDF::e_none, file = QPDF::e_none;
    if (!pdf.isEncrypted(R, P, V, stream, string, file)) return {};
    const QPDF::encryption_method_e method = V < 4 ? QPDF::e_rc4
                                           : stream != QPDF::e_none ? stream : string;
    switch (method) {
    case QPDF::e_aesv3: return QStringLiteral("AES-256");
    case QPDF::e_aes:   return QStringLiteral("AES-128");
    case QPDF::e_rc4: {
        QPDFObjectHandle length = pdf.getTrailer().getKey("/Encrypt").getKey("/Length");
        const int bits = V < 2 ? 40 : length.isInteger() ? int(length.getIntValue()) : 40;
        return QStringLiteral("RC4 %1-bit").arg(bits);
    }
    default:            return {};
    }
}

QString pdfALevel(QPDF &pdf)
{
    QPDFObjectHandle metadata = pdf.getRoot().getKey("/Metadata");
    if (!metadata.isStream()) return {};
    const auto data = metadata.getStreamData(qpdf_dl_generalized);
    const QString xmp = QString::fromUtf8(reinterpret_cast<const char *>(data->getBuffer()),
                                          qsizetype(data->getSize()));
    static const QRegularExpression part(
        QStringLiteral("pdfaid:part(?:=\"|>)\\s*(\\d)"));
    static const QRegularExpression conformance(
        QStringLiteral("pdfaid:conformance(?:=\"|>)\\s*([A-Za-z])"));
    const QString level = part.match(xmp).captured(1);
    if (level.isEmpty()) return {};
    return QStringLiteral("PDF/A-%1%2").arg(level,
                                             conformance.match(xmp).captured(1).toLower());
}

void readWithQpdf(DocumentInfo &info, const std::string &password)
{
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processFile(info.path.toLocal8Bit().constData(),
                    password.empty() ? nullptr : password.c_str());

    QString version = QString::fromStdString(pdf.getPDFVersion());
    const QPDFObjectHandle catalogVersion = pdf.getRoot().getKey("/Version");
    if (catalogVersion.isName()) {
        const QString declared = QString::fromStdString(catalogVersion.getName()).mid(1);
        if (declared > version) version = declared;
    }
    info.pdfVersion = version;

    info.encrypted = pdf.isEncrypted();
    if (info.encrypted) {
        info.encryption   = encryptionName(pdf);
        info.passwordUsed = !password.empty();
        info.permissions.print         = pdf.allowPrintLowRes();
        info.permissions.printHighRes  = pdf.allowPrintHighRes();
        info.permissions.modify        = pdf.allowModifyOther();
        info.permissions.copy          = pdf.allowExtractAll();
        info.permissions.annotate      = pdf.allowModifyAnnotation();
        info.permissions.fillForms     = pdf.allowModifyForm();
        info.permissions.assemble      = pdf.allowModifyAssembly();
        info.permissions.accessibility = pdf.allowAccessibility();
    }

    QPDFAcroFormDocumentHelper forms(pdf);
    for (QPDFFormFieldObjectHelper &field : forms.getFormFields()) {
        if (field.getFieldType() != "/Sig") ++info.formFields;
        else if (field.getValue().isDictionary()) ++info.signedFields;
    }

    QPDFEmbeddedFileDocumentHelper files(pdf);
    info.attachments = int(files.getEmbeddedFiles().size());
    info.pdfA = pdfALevel(pdf);
    info.complete = true;
}
#endif

}

bool DocumentInfo::restricted() const
{
    const Permissions &p = permissions;
    return encrypted && !(p.print && p.printHighRes && p.modify && p.copy && p.annotate
                          && p.fillForms && p.assemble && p.accessibility);
}

int DocumentInfo::signatureCount() const
{
    return qMax(signedFields, int(signatures.size()));
}

bool DocumentInfo::changedAfterSigning() const
{
    return std::none_of(signatures.cbegin(), signatures.cend(),
                        [](const SignatureStatus &s) { return s.coversWholeFile; });
}

DocumentInfo::SignatureState DocumentInfo::signatureState() const
{
    using State = SignatureStatus::State;
    if (signatureCount() == 0) return SignatureState::None;
    if (!signaturesChecked || signatures.isEmpty()) return SignatureState::Unchecked;
    const auto any = [this](State state) {
        return std::any_of(signatures.cbegin(), signatures.cend(),
                           [state](const SignatureStatus &s) { return s.state == state; });
    };
    if (any(State::Invalid))   return SignatureState::Invalid;
    if (changedAfterSigning()) return SignatureState::Changed;
    if (any(State::Untrusted)) return SignatureState::Untrusted;
    return SignatureState::Valid;
}

DocumentInfo DocumentInfo::read(const QString &path, const std::string &password)
{
    DocumentInfo info;
    info.path       = path;
    info.fileSize   = QFileInfo(path).size();
    info.pdfVersion = headerVersion(path);
#ifdef HAVE_QPDF
    try {
        readWithQpdf(info, password);
    } catch (const std::exception &) {
    }
#else
    Q_UNUSED(password)
#endif
    return info;
}

QDateTime DocumentInfo::parseDate(const QString &value)
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
