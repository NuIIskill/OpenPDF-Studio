#include "ui/sign/CreateCertificateDialog.hpp"
#include "engine/sign/SignatureManager.hpp"

#include <QApplication>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QLabel *fieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("SField"));
    return label;
}

QLineEdit *input(const QString &placeholder)
{
    auto *edit = new QLineEdit;
    edit->setObjectName(QStringLiteral("SInput"));
    edit->setPlaceholderText(placeholder);
    return edit;
}

}

CreateCertificateDialog::CreateCertificateDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New certificate - OpenPDF Studio"));
    setModal(true);
    setMinimumWidth(560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    auto *intro = new QLabel(tr("Creates a self-signed certificate for signing. Viewers show "
                                "signatures made with it as intact, but cannot confirm who "
                                "signed."));
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_name = input(tr("Your full name"));
    m_organization = input(tr("Optional"));
    m_email = input(tr("Optional"));
    m_years = new QComboBox;
    m_years->setObjectName(QStringLiteral("SInput"));
    m_years->addItem(tr("1 year"), 1);
    m_years->addItem(tr("3 years"), 3);
    m_years->addItem(tr("5 years"), 5);
    m_years->setCurrentIndex(1);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->addWidget(fieldLabel(tr("Name")), 0, 0);
    grid->addWidget(fieldLabel(tr("Organization")), 0, 1);
    grid->addWidget(m_name, 1, 0);
    grid->addWidget(m_organization, 1, 1);
    grid->addWidget(fieldLabel(tr("Email")), 2, 0);
    grid->addWidget(fieldLabel(tr("Valid for")), 2, 1);
    grid->addWidget(m_email, 3, 0);
    grid->addWidget(m_years, 3, 1);
    grid->setRowMinimumHeight(2, 28);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);

#ifdef _WIN32
    auto *where = new QLabel(tr("The certificate is stored in your Windows certificate store."));
#else
    auto *where = new QLabel(tr("The certificate and its key are stored in the OpenPDF Studio "
                                "settings folder, readable only by you."));
#endif
    where->setObjectName(QStringLiteral("SMuted"));
    where->setWordWrap(true);
    root->addWidget(where);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("SCancel"));
    m_createBtn = new QPushButton(tr("Create"));
    m_createBtn->setObjectName(QStringLiteral("SPrimary"));
    m_createBtn->setDefault(true);
    m_createBtn->setEnabled(false);
    for (QPushButton *btn : { cancel, m_createBtn }) {
        btn->setFixedSize(116, 40);
        btn->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(btn);
    }
    root->addSpacing(4);
    root->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_createBtn, &QPushButton::clicked, this, &CreateCertificateDialog::create);
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_createBtn->setEnabled(!text.trimmed().isEmpty());
    });
}

void CreateCertificateDialog::create()
{
    NewCertificate request;
    request.name         = m_name->text().trimmed();
    request.organization = m_organization->text().trimmed();
    request.email        = m_email->text().trimmed();
    request.years        = m_years->currentData().toInt();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    SignError error = SignError::None;
    m_certificateId = SignatureManager().createCertificate(request, &error);
    QApplication::restoreOverrideCursor();

    if (m_certificateId.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("The certificate could not be created."));
        return;
    }
    accept();
}
