#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

/// Creates a self-signed signing certificate in the platform's store.
class CreateCertificateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateCertificateDialog(QWidget *parent = nullptr);

    QString certificateId() const { return m_certificateId; }

private:
    void create();

    QString      m_certificateId;
    QLineEdit   *m_name         { nullptr };
    QLineEdit   *m_organization { nullptr };
    QLineEdit   *m_email        { nullptr };
    QComboBox   *m_years        { nullptr };
    QPushButton *m_createBtn    { nullptr };
};
