#pragma once

#include <QDialog>

class AppSettings;

class SettingsPanel : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsPanel(AppSettings *settings, QWidget *parent = nullptr);

    void retranslateUi();

Q_SIGNALS:
    void themeChangeRequested(const QString &mode);
    void languageChangeRequested(const QString &lang);

protected:
    void changeEvent(QEvent *e) override;

private:
    void buildUi();

    AppSettings *m_settings;
};
