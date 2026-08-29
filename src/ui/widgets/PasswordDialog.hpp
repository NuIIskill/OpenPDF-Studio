#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

/// Asks for the password of an encrypted document.
class PasswordDialog : public QDialog
{
    Q_OBJECT

public:

    explicit PasswordDialog(const QString &fileName, bool retry = false,
                            QWidget *parent = nullptr);

    QString password() const;

private:
    QLineEdit *m_edit { nullptr };
};
