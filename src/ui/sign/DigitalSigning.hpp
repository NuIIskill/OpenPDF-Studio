#pragma once

#include "engine/sign/SignatureManager.hpp"
#include "ui/sign/SignatureAppearance.hpp"

#include <QCoreApplication>

class DocumentView;

/// Places a digital signature on the document; it is signed when the document is saved.
class DigitalSigning
{
    Q_DECLARE_TR_FUNCTIONS(DigitalSigning)

public:
    static void run(DocumentView *view, const SignRequest &request,
                    const SignatureAppearance &appearance);
    static QString message(SignError error, const QString &target);
};
