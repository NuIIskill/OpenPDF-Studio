#include "drm/LicenseNotice.hpp"

#include "drm/LicenseStore.hpp"

#include <QMessageBox>
#include <QPushButton>

void LicenseNotice::askUsageIfUnknown(QWidget *parent)
{
    if (!License::usage().isEmpty())
        return;

    // Eine Frage, einmal, und "privat" ist die Antwort, die nichts kostet —
    // also Vorgabe, und der Dialog darf einfach geschlossen werden.
    auto *box = new QMessageBox(parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Question);
    box->setWindowTitle(tr("How do you use OpenPDF Studio?"));
    box->setText(tr("Is this a private or a business installation?"));
    box->setInformativeText(tr("Personal use is free. Business use needs a Business "
                               "License after a 30-day evaluation. The answer is kept "
                               "in the configuration file and can be changed there."));
    QPushButton *personalBtn = box->addButton(tr("Personal"), QMessageBox::AcceptRole);
    QPushButton *businessBtn = box->addButton(tr("Business"), QMessageBox::AcceptRole);
    box->setDefaultButton(personalBtn);
    QObject::connect(box, &QMessageBox::finished, box,
                     [box, personalBtn, businessBtn]() {
        if (box->clickedButton() == businessBtn)
            License::setUsage(QStringLiteral("business"));
        else if (box->clickedButton() == personalBtn)
            License::setUsage(QStringLiteral("personal"));
        // Ohne Antwort geschlossen: beim nächsten Start erneut fragen, statt zu raten.
    });
    box->open();
}

void LicenseNotice::showExpiryReminderIfDue(QWidget *parent,
                                           std::function<void()> onEnterKey)
{
    // Bei jedem Start, solange die Frist um ist und kein Schlüssel vorliegt.
    // Der Hinweis erinnert, er sperrt nichts — geprüft wird bis heute nur das
    // Datum, und kein Programmteil fragt das Ergebnis ab.
    if (!License::isEvaluationOver())
        return;

    // open() statt exec(): ein Hinweis beim Start darf das Fenster nicht
    // blockieren, vor dem er steht.
    auto *box = new QMessageBox(parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Information);
    box->setWindowTitle(tr("License"));
    box->setText(tr("Your %1-day evaluation for business use has ended.")
                     .arg(License::kEvaluationDays));
    box->setInformativeText(tr("Business use requires a license. "
                               "Please enter a license key."));
    QPushButton *enterBtn = box->addButton(tr("Enter license key"),
                                           QMessageBox::AcceptRole);
    box->setDefaultButton(enterBtn);
    QObject::connect(box, &QMessageBox::finished, box,
                     [box, enterBtn, onEnterKey = std::move(onEnterKey)]() {
        if (box->clickedButton() == enterBtn && onEnterKey)
            onEnterKey();
    });
    box->open();
}
