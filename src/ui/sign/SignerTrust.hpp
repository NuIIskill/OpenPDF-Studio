#pragma once

#include <QCoreApplication>

class QWidget;
struct SignatureStatus;

/// Trusting a signer on this computer: the question before it and the list that removes trusted signers again.
class SignerTrust
{
    Q_DECLARE_TR_FUNCTIONS(SignerTrust)

public:
    static bool     ask(const SignatureStatus &signature, QWidget *parent);
    static void     manage(QWidget *parent);
};
