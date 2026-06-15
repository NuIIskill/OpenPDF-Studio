#pragma once
#include <QDialog>

class AppSettings;

class SettingsPanel : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsPanel(AppSettings *settings, QWidget *parent = nullptr);
    void showNear(QWidget *anchor);

Q_SIGNALS:
    void themeChangeRequested(const QString &mode);

private:
    void buildUi(const QString &currentMode);
    AppSettings *m_settings;
};
