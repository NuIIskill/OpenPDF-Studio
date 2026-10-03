#include "ui/sign/DigitalSigning.hpp"
#include "ui/DocumentView.hpp"

#include <QFileInfo>
#include <QPointer>

void DigitalSigning::run(DocumentView *view, const SignRequest &request,
                         const SignatureAppearance &appearance)
{
    if (!view) return;
    if (request.appearance.isNull()) {
        view->addDigitalSignature(request, appearance);
        return;
    }

    QPointer<DocumentView> guard(view);
    view->placeSignatureField(request.appearance,
        [guard, request, appearance](int page, const QRectF &bounds) {
            if (!guard) return;
            SignRequest placed = request;
            placed.page = page;
            placed.bounds = bounds;
            guard->addDigitalSignature(placed, appearance);
        });
}

QString DigitalSigning::message(SignError error, const QString &target)
{
    switch (error) {
    case SignError::NotAvailable:
        return tr("This build cannot sign digitally.");
    case SignError::CertificateNotFound:
        return tr("The certificate or its private key was not found. Check that the "
                  "card or token is connected.");
    case SignError::PinRequired:
        return tr("The card or token asks for a PIN. The document was not signed.");
    case SignError::WrongPin:
        return tr("The PIN is wrong. The document was not signed.");
    case SignError::PinLocked:
        return tr("The PIN is locked. The card or token has to be unlocked first.");
    case SignError::Encrypted:
        return tr("Password-protected documents cannot be signed yet.");
    case SignError::ReadFailed:
        return tr("The document could not be prepared for signing.");
    case SignError::WriteFailed:
        return tr("Could not write \"%1\".").arg(QFileInfo(target).fileName());
    case SignError::SigningFailed:
    case SignError::None:
        break;
    }
    return tr("Signing failed. The document was not signed.");
}
