#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QIntValidator;
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
    // Emitted when the user types a page number (1-based) and confirms it.
    void pageRequested(int page);
    void panelToggleRequested();

private:
    void buildLayout();
    void commitPageInput();

    int  m_currentPage { 1 };
    int  m_totalPages  { 1 };

    QLabel        *m_totalLabel { nullptr };
    QLineEdit     *m_pageInput  { nullptr };
    QIntValidator *m_validator  { nullptr };
    IconButton *m_prevBtn    { nullptr };
    IconButton *m_nextBtn    { nullptr };
    IconButton *m_panelBtn   { nullptr };
};
