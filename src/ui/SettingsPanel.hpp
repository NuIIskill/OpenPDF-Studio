#pragma once

#include <QDialog>
#include <QList>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QScrollArea;
class QPushButton;
class QLabel;
class QVBoxLayout;
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
    void buildNav(QWidget *parent, QVBoxLayout *layout);

    void selectPage(int navIndex);
    void applyNavItemStyle(int i, bool selected);
    void refreshThemeColors();

    // One widget per nav page
    QWidget *buildAppearancePage();
    QWidget *buildLanguagePage();
    QWidget *buildMediaPage();
    QWidget *buildAdvancedPage();
    QWidget *buildAboutPage();

    // Card helpers
    QWidget *buildOptionCard(const QString &icon, const QString &title,
                             const QString &desc,  const QString &id,
                             QList<QWidget*> &group, QList<QString> &ids,
                             bool isThemeGroup);
    void selectCardGroup(const QString &id,
                         QList<QWidget*> &cards, QList<QString> &ids);

    // Language row helpers
    void addLangRow(QWidget *parent, QVBoxLayout *layout,
                    const QString &code, const QString &display);
    void selectLangCode(const QString &code);

    void applyAndClose();

    AppSettings    *m_settings  { nullptr };
    QStackedWidget *m_pages     { nullptr };

    struct NavItem {
        QPushButton *btn;
        QLabel      *iconLabel;
        QLabel      *textLabel;
        QString      iconName;
    };
    QList<NavItem> m_navItems;
    int m_currentNav { 0 };

    QList<QWidget *> m_themeCards;
    QList<QString>   m_themeIds;

    QList<QWidget *> m_mediaCards;
    QList<QString>   m_mediaIds;

    QList<QWidget *> m_langRows;   // LangRow* cast to QWidget*
    QList<QString>   m_langCodes;

    QString m_pendingTheme;
    QString m_pendingLang;
    QString m_originalTheme;
    QString m_originalLang;
};
