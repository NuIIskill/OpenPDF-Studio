#pragma once

#include <QDialog>
#include <QList>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QScrollArea;
class QComboBox;
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

    // navIndex: which sidebar item to highlight
    // anchorIndex: -1=About page, 0=top, 1=Language, 2=Media, 3=Advanced
    void selectPage(int navIndex, int anchorIndex, bool scrollToAnchor = false);
    void applyNavItemStyle(int i, bool selected);
    void refreshThemeColors();

    // Section builders
    void buildSection_Appearance(QWidget *parent, QVBoxLayout *layout);
    void buildSection_Language(QWidget *parent, QVBoxLayout *layout);
    void buildSection_MediaPlayback(QWidget *parent, QVBoxLayout *layout);
    void buildSection_Advanced(QWidget *parent, QVBoxLayout *layout);
    QWidget *buildAboutPage();

    // Helpers
    QLabel *makeSectionTitle(QWidget *parent, QVBoxLayout *layout, const QString &text);
    void    makeSectionDesc(QWidget *parent, QVBoxLayout *layout, const QString &text);
    void    makeSeparator(QWidget *parent, QVBoxLayout *layout);

    QWidget *buildOptionCard(const QString &icon, const QString &title,
                             const QString &desc,  const QString &id,
                             QList<QWidget*> &group, QList<QString> &ids,
                             bool isThemeGroup);
    void selectCardGroup(const QString &id,
                         QList<QWidget*> &cards, QList<QString> &ids);

    void applyAndClose();

    AppSettings    *m_settings      { nullptr };
    QStackedWidget *m_pages         { nullptr };
    QScrollArea    *m_generalScroll { nullptr };
    QComboBox      *m_langCombo     { nullptr };

    // Scroll-to anchors
    QLabel *m_langAnchor  { nullptr };
    QLabel *m_mediaAnchor { nullptr };
    QLabel *m_advAnchor   { nullptr };

    struct NavItem {
        QPushButton *btn;
        QLabel      *iconLabel;
        QLabel      *textLabel;
        QString      iconName;
        int          anchorIndex; // scroll target (-1 = About page)
    };
    QList<NavItem> m_navItems;
    int m_currentNav { 0 };

    QList<QWidget *> m_themeCards;
    QList<QString>   m_themeIds;

    QList<QWidget *> m_mediaCards;
    QList<QString>   m_mediaIds;

    QString m_pendingTheme;
    QString m_pendingLang;
    QString m_originalTheme;
    QString m_originalLang;
};
