#pragma once

#include "engine/document/ProtectionWriter.hpp"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QVBoxLayout;
QT_END_NAMESPACE

class DocumentView;
class IconTabBar;

/// The protect dialog: open password, encryption level and reader permissions, applied on the next save.
class ProtectDialog : public QDialog
{
    Q_OBJECT

public:
    static void run(DocumentView *view, QWidget *parent);

    ProtectDialog(const PdfProtection &current, bool isProtected, int signatures,
                  bool hasDocument = true, QWidget *parent = nullptr);

    PdfProtection protection() const;

private:
    QWidget *buildBanner(bool isProtected, bool hasDocument);
    QWidget *buildPasswordPage(bool isProtected, bool hasDocument);
    QWidget *buildPermissionsPage();
    QWidget *buildFooter(bool hasDocument);
    QLineEdit *passwordField(const QString &placeholder);
    void addSection(QVBoxLayout *layout, const QString &title, const QString &subtitle);
    void fill(const PdfProtection &current);
    void updateStates();
    void onApply();
    void applyDialogStyle();

    IconTabBar     *m_tabBar        { nullptr };
    QStackedWidget *m_pages         { nullptr };
    QCheckBox      *m_passwordChk   { nullptr };
    QLineEdit      *m_password      { nullptr };
    QLineEdit      *m_confirm       { nullptr };
    QComboBox      *m_cipher        { nullptr };
    QLabel         *m_cipherNote    { nullptr };
    QCheckBox      *m_restrictChk   { nullptr };
    QLineEdit      *m_owner         { nullptr };
    QLineEdit      *m_ownerConfirm  { nullptr };
    QList<QCheckBox *> m_allow;
    bool            m_remove        { false };
};
