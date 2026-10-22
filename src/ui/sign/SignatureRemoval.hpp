#pragma once

#include <QCoreApplication>

class DocumentView;
class QWidget;
struct SignatureStatus;

/// Asks before taking a digital signature off the document; the change is written when the document is saved.
class SignatureRemoval
{
    Q_DECLARE_TR_FUNCTIONS(SignatureRemoval)

public:
    static void run(DocumentView *view, const SignatureStatus &signature, QWidget *parent);
};
