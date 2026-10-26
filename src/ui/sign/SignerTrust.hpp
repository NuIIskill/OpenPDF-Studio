#pragma once

#include <QCoreApplication>

class QWidget;
struct SignatureStatus;

/// Who signed and whether to trust them: certificate details, the question before trusting and the list of trusted signers.
class SignerTrust
{
    Q_DECLARE_TR_FUNCTIONS(SignerTrust)

public:
    static bool     ask(const SignatureStatus &signature, QWidget *parent);
    static void     details(const SignatureStatus &signature, QWidget *parent);
    static void     manage(QWidget *parent);
};
