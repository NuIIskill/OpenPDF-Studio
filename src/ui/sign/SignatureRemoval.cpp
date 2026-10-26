#include "ui/sign/SignatureRemoval.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/sign/SignatureRemover.hpp"
#include "engine/sign/SignatureVerifier.hpp"
#include "ui/DocumentView.hpp"
#include "ui/protect/PermissionGate.hpp"

#include <QMessageBox>
#include <QPushButton>

void SignatureRemoval::run(DocumentView *view, const SignatureStatus &signature, QWidget *parent)
{
    if (!PermissionGate::request(view, PermissionGate::Action::Modify, parent)) return;
    if (!view) return;
    const QString signer = signature.signer.isEmpty() ? tr("Unknown signer") : signature.signer;

    QMessageBox box(parent);
    box.setWindowTitle(tr("Remove signature"));
    if (view->hasUnsavedEdits()) {
        box.setIcon(QMessageBox::Information);
        box.setText(tr("Save or undo your changes first."));
        box.setInformativeText(tr("A signature can only be removed from the saved document."));
        box.addButton(QMessageBox::Ok);
        box.exec();
        return;
    }

    const QString source = view->contentFile();
    const SignatureRemover::Removal removal =
        SignatureRemover::plan(source, signature.field, PdfPwStore::forQpdf(source));
    if (!removal.possible) {
        box.setIcon(QMessageBox::Information);
        box.setText(tr("The signature of %1 cannot be removed.").arg(signer));
        box.setInformativeText(tr("The file could not be read."));
        box.addButton(QMessageBox::Ok);
        box.exec();
        return;
    }

    QStringList also;
    if (removal.signatures == 2)
        also << tr("The signature added after it is removed as well.");
    else if (removal.signatures > 2)
        also << tr("The %1 signatures added after it are removed as well.")
                    .arg(removal.signatures - 1);
    if (removal.laterChanges)
        also << tr("Changes saved after it are undone as well.");
    also << (removal.inPlace
                 ? tr("The signature is taken out of the document, which otherwise stays as it is. "
                      "The change is written when you save and can be undone until then.")
                 : tr("The document goes back to how it was before this signature. "
                      "The change is written when you save and can be undone until then."));

    box.setIcon(QMessageBox::Question);
    box.setText(tr("Remove the signature of %1?").arg(signer));
    box.setInformativeText(also.join(QStringLiteral("\n\n")));
    QPushButton *remove = box.addButton(tr("Remove"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != remove) return;

    if (!view->removeSignature(signature.field))
        QMessageBox::warning(parent, tr("Remove signature"),
                             tr("The signature could not be removed."));
}
