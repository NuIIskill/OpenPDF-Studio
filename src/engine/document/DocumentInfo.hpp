#pragma once

#include "engine/sign/SignatureVerifier.hpp"

#include <QDateTime>
#include <QList>
#include <QString>

#include <string>

/// What a PDF file says about itself: version, protection, signatures and features.
struct DocumentInfo
{
    enum class SignatureState { None, Valid, Untrusted, Changed, Invalid, Unchecked };

    struct Permissions {
        bool print         { true };
        bool printHighRes  { true };
        bool modify        { true };
        bool copy          { true };
        bool annotate      { true };
        bool fillForms     { true };
        bool assemble      { true };
        bool accessibility { true };
    };

    QString     path;
    bool        complete { false };
    qint64      fileSize { 0 };
    QString     pdfVersion;

    bool        encrypted    { false };
    QString     encryption;
    bool        passwordUsed { false };
    Permissions permissions;

    int                    signedFields { 0 };
    QList<SignatureStatus> signatures;
    bool                   signaturesChecked { false };

    int         formFields  { 0 };
    int         attachments { 0 };
    QString     pdfA;

    bool           restricted() const;
    int            signatureCount() const;
    bool           changedAfterSigning() const;
    SignatureState signatureState() const;

    static DocumentInfo read(const QString &path, const std::string &password);
    static QDateTime    parseDate(const QString &value);
};
