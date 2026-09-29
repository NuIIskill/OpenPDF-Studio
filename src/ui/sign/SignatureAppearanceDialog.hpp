#pragma once

#include "ui/sign/SignatureAppearance.hpp"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

class SignOption;

/// Edits what a visible digital signature shows, with a preview of the stamp.
class SignatureAppearanceDialog : public QDialog
{
    Q_OBJECT

public:
    static constexpr int Deleted = 2;

    static QStringList reasons();

    SignatureAppearanceDialog(const SignatureAppearance &appearance, const QString &profileName,
                              const QString &defaultName, bool deletable,
                              QWidget *parent = nullptr);

    SignatureAppearance appearance() const;
    QString profileName() const;

private:
    QWidget *buildFields(const SignatureAppearance &appearance);
    QWidget *buildOptions(const SignatureAppearance &appearance);
    QWidget *buildLogoRow();
    QWidget *buildPreview();
    QWidget *buildButtons(bool deletable);
    void confirmDelete();

    void chooseLogo();
    void updatePreview();

    QString    m_defaultName;
    QImage     m_logo;
    QString    m_logoName;
    QLineEdit *m_profileName  { nullptr };
    QLineEdit *m_name         { nullptr };
    QComboBox *m_reason       { nullptr };
    QLineEdit *m_location     { nullptr };
    SignOption *m_showName     { nullptr };
    SignOption *m_showDate     { nullptr };
    SignOption *m_showReason   { nullptr };
    SignOption *m_showLocation { nullptr };
    SignOption *m_showLogo     { nullptr };
    QLineEdit *m_logoField    { nullptr };
    QLabel    *m_preview      { nullptr };
};
