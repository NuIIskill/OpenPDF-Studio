#include "ui/info/InfoText.hpp"

#include "ui/theme/Theme.hpp"

#include <QCoreApplication>

namespace {

class Words
{
    Q_DECLARE_TR_FUNCTIONS(InfoText)
};

}

namespace InfoText {

QString signatureSummary(const DocumentInfo &info)
{
    using State = DocumentInfo::SignatureState;
    const int n = info.signatureCount();
    const QString count = QString::number(n);
    switch (info.signatureState()) {
    case State::None:
        return Words::tr("No digital signatures");
    case State::Valid:
        return n == 1 ? Words::tr("1 valid signature")
                      : Words::tr("%1 valid signatures").arg(count);
    case State::Untrusted:
        return n == 1 ? Words::tr("1 signature, signer not trusted")
                      : Words::tr("%1 signatures, signer not trusted").arg(count);
    case State::Changed:
        return Words::tr("Document changed after signing");
    case State::Invalid:
        return Words::tr("Signature invalid");
    case State::Unchecked:
        return n == 1 ? Words::tr("1 signature, not checked")
                      : Words::tr("%1 signatures, not checked").arg(count);
    }
    return {};
}

QString signatureIcon(const DocumentInfo &info)
{
    using State = DocumentInfo::SignatureState;
    const State state = info.signatureState();
    return state == State::Valid || state == State::None ? QStringLiteral("shield-check")
                                                         : QStringLiteral("shield-alert");
}

QColor signatureColor(const DocumentInfo &info)
{
    using State = DocumentInfo::SignatureState;
    switch (info.signatureState()) {
    case State::Valid:     return Theme::Success;
    case State::Untrusted:
    case State::Changed:   return warningColor();
    case State::Invalid:   return errorColor();
    default:               return Theme::IconMuted;
    }
}

QString encryptionSummary(const DocumentInfo &info)
{
    if (!info.encrypted) return Words::tr("Not encrypted");
    const QString base = info.encryption.isEmpty()
        ? Words::tr("Encrypted") : Words::tr("Encrypted (%1)").arg(info.encryption);
    if (info.passwordUsed) return Words::tr("%1, opened with password").arg(base);
    if (info.restricted()) return Words::tr("%1, permissions restricted").arg(base);
    return base;
}

QColor warningColor()
{
    return Theme::DarkMode ? QColor(0xFB, 0xBF, 0x24) : QColor(0xD9, 0x77, 0x06);
}

QColor errorColor()
{
    return Theme::DarkMode ? QColor(0xF8, 0x71, 0x71) : QColor(0xDC, 0x26, 0x26);
}

}
