#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QAction;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

/// "License Key" page of the settings dialog.
class LicensePage : public QWidget
{
    Q_OBJECT

public:
    explicit LicensePage(QWidget *parent = nullptr);

    void retranslateUi();
    void applyTheme();

protected:
    void changeEvent(QEvent *e) override;

private:
    void buildUi();
    void refreshStatus();
    void onActivateClicked();

    QLabel      *m_title        { nullptr };
    QLabel      *m_desc         { nullptr };
    QLabel      *m_statusGroup  { nullptr };
    QFrame      *m_statusCard   { nullptr };
    QLabel      *m_statusIcon   { nullptr };
    QLabel      *m_statusTitle  { nullptr };
    QLabel      *m_statusDesc   { nullptr };
    QLabel      *m_activateGroup{ nullptr };
    QLabel      *m_fieldLabel   { nullptr };
    QLineEdit   *m_keyInput     { nullptr };
    QAction     *m_keyIcon      { nullptr };
    QPushButton *m_actionBtn    { nullptr };
    QLabel      *m_feedback     { nullptr };
    QLabel      *m_needGroup    { nullptr };
    QLabel      *m_needDesc     { nullptr };
    QPushButton *m_getBtn       { nullptr };

    bool m_hasKey { false };
};
