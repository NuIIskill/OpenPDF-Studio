#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QComboBox;
QT_END_NAMESPACE

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
    QWidget *buildNavItem(const QString &iconName, const QString &label, int pageIndex);
    QWidget *buildGeneralPage();
    QWidget *buildPlaceholderPage(const QString &title);
    QWidget *buildAboutPage();
    QWidget *buildThemeCard(const QString &icon, const QString &title,
                            const QString &desc, const QString &id);
    void     selectThemeCard(const QString &id);
    void     applyAndClose();

    AppSettings    *m_settings    { nullptr };
    QStackedWidget *m_pages       { nullptr };
    QComboBox      *m_langCombo   { nullptr };

    QString m_pendingTheme;
    QString m_pendingLang;
    QString m_originalTheme;
    QString m_originalLang;

    QList<QWidget *> m_themeCards;
    QList<QString>   m_themeIds;
};
