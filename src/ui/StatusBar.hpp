#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

class IconButton;

/// Bottom page-navigation bar — 48 px.
///
/// Center: ‹ previous | page input | / total | › next
/// Right: panel-toggle icon
class StatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr);

    void setPageInfo(int current, int total);
    void refreshTheme();

Q_SIGNALS:
    void previousPageRequested();
    void nextPageRequested();

private:
    void buildLayout();

    QLabel     *m_totalLabel { nullptr };
    QLineEdit  *m_pageInput  { nullptr };
    IconButton *m_prevBtn    { nullptr };
    IconButton *m_nextBtn    { nullptr };
    IconButton *m_panelBtn   { nullptr };
};
