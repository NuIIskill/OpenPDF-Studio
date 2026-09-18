#include "drm/LicenseNotice.hpp"

#include "drm/LicenseStore.hpp"

#include <QMessageBox>
#include <QPushButton>

void LicenseNotice::askUsageIfUnknown(QWidget *parent)
{
    if (!License::usage().isEmpty())
        return;

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

    });
    box->open();
}

void LicenseNotice::showExpiryReminderIfDue(QWidget *parent,
                                           std::function<void()> onEnterKey)
{

    if (!License::isEvaluationOver())
        return;

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
