#pragma once

#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
QT_END_NAMESPACE

class IconButton;

class TopToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit TopToolbar(QWidget *parent = nullptr);

    int  addTab(const QString &label = {});
    void removeTab(int index);
    void setTabLabel(int index, const QString &label);
    void setCurrentTab(int index);
    int  currentTab()  const { return m_currentTab; }
    int  tabCount()    const { return m_tabBtns.size(); }

    void setFileName(const QString &name);
    void setZoom(int percent);
    void setViewMode(bool gridView);
    void refreshTheme();
    void retranslateUi();

Q_SIGNALS:
    void tabActivated(int index);
    void tabCloseRequested(int index);
    void newTabRequested();
    void openFileRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void undoRequested();
    void redoRequested();
    void saveRequested();
    void printRequested();
    void viewModeChanged(bool gridView);

private:
    void     buildLayout();
    QWidget *makeSeparator();
    void     syncTabBarWidth();
    void     scrollToTab(int index);

    QScrollArea *m_tabScroll { nullptr };
    QWidget     *m_tabBar    { nullptr };
    QHBoxLayout *m_tabLayout { nullptr };
    IconButton  *m_newTabBtn { nullptr };

    QList<QPushButton*> m_tabBtns;
    QList<QLabel*>      m_tabLabels;
    QList<bool>         m_tabEmpty;
    int m_currentTab { -1 };

    QLabel     *m_zoomLabel     { nullptr };
    IconButton *m_zoomInBtn     { nullptr };
    IconButton *m_zoomOutBtn    { nullptr };
    IconButton *m_saveBtn       { nullptr };
    IconButton *m_printBtn      { nullptr };
    IconButton *m_undoBtn       { nullptr };
    IconButton *m_redoBtn       { nullptr };
    IconButton *m_viewSingleBtn { nullptr };
    IconButton *m_viewGridBtn   { nullptr };
};
