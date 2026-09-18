#pragma once

#include <QDialog>
#include <QList>

QT_BEGIN_NAMESPACE
class QAbstractButton;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QScrollArea;
class QPushButton;
class QLabel;
class QVBoxLayout;
QT_END_NAMESPACE

class AppSettings;
class LicensePage;
class ShortcutRow;
class UpdateChecker;
struct UpdateCheckResult;

class SettingsPanel : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsPanel(AppSettings *settings, QWidget *parent = nullptr);

    void retranslateUi();

    void showLicensePage();

    void selectPageForTest(const QString &navLabel);

Q_SIGNALS:
    void themeChangeRequested(const QString &mode);
    void languageChangeRequested(const QString &lang);
    void shortcutsChanged();
    void zoomSettingsChanged();
    void panelLayoutSettingChanged();

protected:
    void changeEvent(QEvent *e) override;

private:
    void buildUi();
    void buildNav(QWidget *parent, QVBoxLayout *layout);

    void selectPage(int navIndex);
    void applyNavItemStyle(int i, bool selected);
    void refreshThemeColors();

    QWidget *buildAppearancePage();
    QWidget *buildLanguagePage();
    QWidget *buildMediaPage();
    QWidget *buildShortcutsPage();
    QWidget *buildZoomPage();
    QWidget *buildAdvancedPage();

    QVBoxLayout *buildScrollPage(QWidget *&page, const QString &title,
                                 const QString &desc);

    void finishScrollPage(QVBoxLayout *contentLayout);

    QVBoxLayout *addSettingsCard(QVBoxLayout *into, const QString &cardTitle);

    static QAbstractButton *addSettingsCheck(QVBoxLayout *cl, const QString &label,
                                             const QString &explain, bool checked);

    static QComboBox *addSettingsCombo(QVBoxLayout *cl, const QString &label);

    static void selectComboData(QComboBox *combo, const QString &value);

    void buildAdvancedUpdates(QVBoxLayout *vl);
    void buildAdvancedInterface(QVBoxLayout *vl);
    void buildAdvancedPerformance(QVBoxLayout *vl);
    void buildAdvancedDiagnostics(QVBoxLayout *vl);
    void buildAdvancedReset(QVBoxLayout *vl);
    QWidget *buildAboutPage();

    QWidget *buildOptionCard(const QString &icon, const QString &title,
                             const QString &desc,  const QString &id,
                             QList<QWidget*> &group, QList<QString> &ids,
                             bool isThemeGroup);
    void selectCardGroup(const QString &id,
                         QList<QWidget*> &cards, QList<QString> &ids);

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
        const char  *labelKey;
    };
    QList<NavItem> m_navItems;
    int m_currentNav { 0 };

    LicensePage *m_licensePage { nullptr };

    QList<QWidget *> m_themeCards;
    QList<QString>   m_themeIds;

    QList<QWidget *> m_mediaCards;
    QList<QString>   m_mediaIds;
    QString          m_pendingMedia;
    QLineEdit       *m_customPlayerEdit { nullptr };
    QWidget         *m_customPlayerRow  { nullptr };

    QList<QWidget *>   m_langRows;
    QList<QString>     m_langCodes;

    QList<ShortcutRow*> m_shortcutRows;
    QList<QString>      m_shortcutKeys;

    UpdateChecker *m_updateChecker   { nullptr };
    QPushButton   *m_updateCheckBtn  { nullptr };
    QLabel        *m_updateStatus    { nullptr };
    void showUpdateResult(const UpdateCheckResult &result);

    QSpinBox        *m_zoomStepSpin     { nullptr };
    QAbstractButton *m_ctrlWheelToggle  { nullptr };
    QAbstractButton *m_zoomPtrToggle    { nullptr };
    QComboBox       *m_wheelActionCombo { nullptr };
    QLabel          *m_zoomExampleLabel { nullptr };

    QAbstractButton *m_autoUpdateCheck     { nullptr };
    QComboBox       *m_updateIntervalCombo { nullptr };
    QAbstractButton *m_preserveLayoutCheck { nullptr };
    QAbstractButton *m_hwAccelCheck        { nullptr };
    QAbstractButton *m_limitMemoryCheck    { nullptr };
    QAbstractButton *m_debugLogCheck       { nullptr };
    QComboBox       *m_logLevelCombo       { nullptr };

    QString m_pendingTheme;
    QString m_pendingLang;
    QString m_originalTheme;
    QString m_originalLang;
};
