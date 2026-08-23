#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

// Asks for the password of an encrypted document.
//
// Purpose-built rather than QInputDialog::getText, which came with a resize
// grip nobody wants on a one-field prompt and whose line edit lost every
// visible outline as soon as focus moved on — tab once and the field was gone.
class PasswordDialog : public QDialog
{
    Q_OBJECT

public:
    // retry phrases the prompt as a second attempt after a wrong password.
    explicit PasswordDialog(const QString &fileName, bool retry = false,
                            QWidget *parent = nullptr);

    QString password() const;

private:
    QLineEdit *m_edit { nullptr };
};
