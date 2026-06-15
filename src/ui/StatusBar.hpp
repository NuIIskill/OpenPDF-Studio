#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

class IconButton;

class StatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr);

    void setPageInfo(int current, int total);
    void refreshTheme();
    void retranslateUi();

Q_SIGNALS:
    void previousPageRequested();
    void nextPageRequested();
    void panelToggleRequested();

private:
    void buildLayout();

    QLabel     *m_totalLabel { nullptr };
    QLineEdit  *m_pageInput  { nullptr };
    IconButton *m_prevBtn    { nullptr };
    IconButton *m_nextBtn    { nullptr };
    IconButton *m_panelBtn   { nullptr };
};
